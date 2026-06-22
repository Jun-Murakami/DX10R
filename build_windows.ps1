# DX10R Windows Release Build Script
# WebUI 本番ビルド → CMake configure → VST3 / CLAP / Standalone / AAX コンパイル
# → AAX PACE 署名（任意）→ Authenticode 署名（任意）→ ZIP 梱包 → Inno Setup インストーラ生成
#
# 署名はすべて任意。証明書 / ツールが無ければ警告して未署名のまま続行し、必ず
# releases/<VERSION>/*.zip と *_Setup.exe を生成する。

param(
    [string]$Configuration = "Release",
    [switch]$SkipCodeSign
)

$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
$RootDir = $ScriptDir
$VersionFile = "$RootDir\VERSION"

if (Test-Path $VersionFile) {
    $Version = (Get-Content $VersionFile -Raw).Trim()
} else {
    Write-Error "VERSION file not found at: $VersionFile"
    exit 1
}

$ErrorActionPreference = "Stop"

function Write-Header { param([string]$Text)
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "   $Text" -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host ""
}
function Write-Step    { param([string]$Text) Write-Host ">> $Text" -ForegroundColor Yellow }
function Write-Success { param([string]$Text) Write-Host "[OK] $Text" -ForegroundColor Green }
function Write-Fail    { param([string]$Text) Write-Host "[FAIL] $Text" -ForegroundColor Red }

Write-Header "DX10R $Version Build Script"

# Load .env (KEY=VALUE)
$EnvFilePath = "$RootDir\.env"
if (Test-Path $EnvFilePath) {
    Write-Host "Loading environment variables from .env ..." -ForegroundColor Gray
    Get-Content $EnvFilePath | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#")) {
            $eqIdx = $line.IndexOf("=")
            if ($eqIdx -gt 0) {
                $key   = $line.Substring(0, $eqIdx).Trim()
                $value = $line.Substring($eqIdx + 1).Trim().Trim('"').Trim("'")
                if (-not (Get-Item "env:$key" -ErrorAction SilentlyContinue)) {
                    [Environment]::SetEnvironmentVariable($key, $value, "Process")
                }
            }
        }
    }
}

# ----------------------------------------------------------------------------
# Authenticode 署名 (signtool + dev pfx)。任意。
# ----------------------------------------------------------------------------
# 配布 PE (VST3 DLL / CLAP / Standalone exe / インストーラ) を dev pfx で署名する。
# .env の PACE_PFX_PATH (= dx10r-dev.pfx) と PACE_KEYPASSWORD を使う。証明書 /
# signtool が無ければ警告のみで未署名のまま続行 (= graceful)。
# 注: AAX (.aaxplugin) は PACE wraptool 側で一体署名するのでここでは触らない。
$TimestampUrl = if ($env:CODESIGN_TIMESTAMP_URL) { $env:CODESIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
$CodeSigningStatus = "unsigned"

function Resolve-DevPfx {
    $candidates = @($env:PACE_PFX_PATH, "$RootDir\dx10r-dev.pfx")
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    return $null
}

function Invoke-AuthenticodeSign {
    param([Parameter(Mandatory = $true)][string[]]$Paths)

    if ($SkipCodeSign) {
        Write-Host "Code signing skipped (-SkipCodeSign)" -ForegroundColor Yellow
        $script:CodeSigningStatus = "skipped"
        return
    }
    $existing = @($Paths | Where-Object { $_ -and (Test-Path $_) })
    if ($existing.Count -eq 0) { return }

    $signtool = Get-Command signtool -ErrorAction SilentlyContinue
    if (-not $signtool) {
        Write-Host "Warning: signtool not found on PATH - skipping Authenticode signing" -ForegroundColor Yellow
        $script:CodeSigningStatus = "tool_missing"
        return
    }
    $pfx = Resolve-DevPfx
    if (-not $pfx -or -not $env:PACE_KEYPASSWORD) {
        Write-Host "Warning: no dev pfx / PACE_KEYPASSWORD - skipping Authenticode signing" -ForegroundColor Yellow
        $script:CodeSigningStatus = "certificate_missing"
        return
    }

    foreach ($f in $existing) { Write-Step "Signing: $f" }
    & $signtool.Source sign /f $pfx /p $env:PACE_KEYPASSWORD /fd sha256 /tr $TimestampUrl /td sha256 $existing
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Authenticode signing succeeded ($($existing.Count) file(s))"
        $script:CodeSigningStatus = "signed"
    } else {
        Write-Host "Warning: Authenticode signing failed (exit $LASTEXITCODE)" -ForegroundColor Yellow
        $script:CodeSigningStatus = "signing_failed"
    }
}

$BuildDate = Get-Date -Format "yyyy-MM-dd"
$WebUIDir   = "$RootDir\webui"
# 配布ビルドは dev とは別ディレクトリ (build-dist) を使い、dev の build/ の deploy
# 設定と干渉させない。
$BuildDir   = "$RootDir\build-dist"
$OutputDir  = "$RootDir\releases\$Version"
$AAXSDKPath = "$RootDir\iPlug2\Dependencies\IPlug\AAX_SDK"

# AAX SDK presence check
Write-Step "Checking AAX SDK..."
if (Test-Path "$AAXSDKPath\Interfaces\AAX.h") {
    Write-Success "AAX SDK found - AAX will be built"
    $BuildAAX = $true

    # iPlug2 同梱の AAX SDK は cmake_minimum_required(3.12) で PUBLIC include path が
    # 相対パス。CMake 4.x は INTERFACE_INCLUDE_DIRECTORIES がソースツリー内を指すと
    # 拒否するので、PUBLIC の 2 行を $<BUILD_INTERFACE:...> でラップする。
    $AaxLibCMake = "$AAXSDKPath\Libs\AAXLibrary\CMakeLists.txt"
    if ((Test-Path $AaxLibCMake) -and -not (Select-String -Path $AaxLibCMake -SimpleMatch -Pattern 'BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces' -Quiet)) {
        Write-Step "Patching AAX SDK CMakeLists.txt for CMake 4.x compatibility..."
        $content = Get-Content -Path $AaxLibCMake -Raw
        $content = $content -replace '(?m)^    \$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./\.\./Interfaces$', '    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces>'
        $content = $content -replace '(?m)^    \$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./\.\./Interfaces/ACF$', '    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Interfaces/ACF>'
        Set-Content -Path $AaxLibCMake -Value $content -NoNewline
        Write-Success "AAX SDK patched"
    }
} else {
    Write-Host "AAX SDK not found at: $AAXSDKPath - AAX will be skipped" -ForegroundColor Yellow
    $BuildAAX = $false
}

Write-Step "Creating output directories..."
New-Item -ItemType Directory -Force -Path "$OutputDir\Windows" | Out-Null
Write-Success "Output directories created"

# ----------------------------------------------------------------------------
# Step 1: WebUI production build
# ----------------------------------------------------------------------------
Write-Header "Step 1: Building WebUI for production"

$WebOutDir = "$RootDir\plugin\resources\web"
if (Test-Path $WebOutDir) {
    Write-Step "Cleaning previous WebUI build..."
    Remove-Item -Path $WebOutDir -Recurse -Force
    Write-Success "Previous build cleaned"
}

Set-Location $WebUIDir
Write-Step "Installing npm dependencies..."
npm install
if ($LASTEXITCODE -ne 0) { Write-Fail "npm install failed"; exit 1 }

Write-Step "Building WebUI..."
npm run build
if ($LASTEXITCODE -ne 0) { Write-Fail "WebUI build failed"; exit 1 }
Write-Success "WebUI built successfully"

if (-not (Test-Path "$WebOutDir\index.html")) {
    Write-Fail "WebUI build output not found at $WebOutDir"
    exit 1
}

# ----------------------------------------------------------------------------
# Step 2: Native plugin build
# ----------------------------------------------------------------------------
Write-Header "Step 2: Building plugins ($Configuration)"

Set-Location $RootDir
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}

Write-Step "Configuring CMake for $Configuration build..."
# IPLUG_DEPLOY_PLUGINS=OFF: 配布ビルドは out から自前で収集 → 署名 → ZIP/installer 化
# するので、システム共有フォルダへの自動コピーは止める。
cmake -S $RootDir -B $BuildDir -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=$Configuration -DIPLUG_DEPLOY_PLUGINS=OFF
if ($LASTEXITCODE -ne 0) { Write-Fail "CMake configuration failed"; exit 1 }

$Targets = @("DX10R-vst3", "DX10R-clap", "DX10R-app")
if ($BuildAAX) { $Targets += "DX10R-aax" }

Write-Step "Building: $($Targets -join ', ')..."
cmake --build $BuildDir --config $Configuration --target $Targets
if ($LASTEXITCODE -ne 0) { Write-Fail "Plugin build failed"; exit 1 }
Write-Success "Plugins built successfully"

# ----------------------------------------------------------------------------
# Step 3: Packaging
# ----------------------------------------------------------------------------
Write-Header "Step 3: Packaging for distribution"

# iPlug2 stages artefacts flat at $BuildDir\out\DX10R.* on Windows.
$SrcVST3       = "$BuildDir\out\DX10R.vst3"
$SrcCLAP       = "$BuildDir\out\DX10R.clap"
$SrcStandalone = "$BuildDir\out\DX10R.exe"
$SrcAAX        = "$BuildDir\out\DX10R.aaxplugin"

$WinDir         = "$OutputDir\Windows"
$DestVST3       = "$WinDir\DX10R.vst3"
$DestCLAP       = "$WinDir\DX10R.clap"
$DestStandalone = "$WinDir\DX10R.exe"
$DestAAX        = "$WinDir\DX10R.aaxplugin"

Write-Step "Copying VST3..."
if (Test-Path $SrcVST3) {
    if (Test-Path $DestVST3) { Remove-Item -Path $DestVST3 -Recurse -Force }
    Copy-Item -Path $SrcVST3 -Destination $DestVST3 -Recurse -Force
    Write-Success "VST3 copied"
} else { Write-Fail "VST3 not found at: $SrcVST3"; exit 1 }

Write-Step "Copying CLAP..."
if (Test-Path $SrcCLAP) {
    if (Test-Path $DestCLAP) { Remove-Item -Path $DestCLAP -Recurse -Force }
    Copy-Item -Path $SrcCLAP -Destination $DestCLAP -Recurse -Force
    Write-Success "CLAP copied"
} else { Write-Host "Warning: CLAP not found at: $SrcCLAP" -ForegroundColor Yellow }

Write-Step "Copying Standalone..."
if (Test-Path $SrcStandalone) {
    if (Test-Path $DestStandalone) { Remove-Item -Path $DestStandalone -Force }
    Copy-Item -Path $SrcStandalone -Destination $DestStandalone -Force
    Write-Success "Standalone copied"
} else { Write-Fail "Standalone not found at: $SrcStandalone"; exit 1 }

# AAX copy + PACE signing (iLok)
$AAXSigningStatus = "unsigned_developer"
if ($BuildAAX) {
    Write-Step "Copying AAX..."
    if (-not (Test-Path $SrcAAX)) { Write-Fail "AAX not found at: $SrcAAX"; exit 1 }
    if (Test-Path $DestAAX) { Remove-Item -Path $DestAAX -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $DestAAX | Out-Null
    Copy-Item -Path (Join-Path $SrcAAX '*') -Destination $DestAAX -Recurse -Force
    Write-Success "AAX copied (unsigned)"

    Write-Step "Signing AAX with PACE Eden wraptool..."
    $WrapToolPath = "C:\Program Files (x86)\PACEAntiPiracy\Eden\Fusion\Versions\5\wraptool.exe"
    $Pfx = Resolve-DevPfx
    $Wcguid = if ($env:WRAP_GUID) { $env:WRAP_GUID } else { $env:PACE_ORGANIZATION }
    if (-not (Test-Path $WrapToolPath)) {
        Write-Host "Warning: PACE Eden wraptool not found - AAX left unsigned" -ForegroundColor Yellow
        $AAXSigningStatus = "wraptool_missing"
    } elseif (-not $Pfx -or -not $env:PACE_KEYPASSWORD -or -not $env:PACE_USERNAME -or -not $env:PACE_PASSWORD -or -not $Wcguid) {
        Write-Host "Warning: missing PACE creds / dev pfx - AAX left unsigned" -ForegroundColor Yellow
        $AAXSigningStatus = "credentials_missing"
    } else {
        $SigningArgs = @(
            "sign", "--verbose",
            "--account",  $env:PACE_USERNAME,
            "--password", $env:PACE_PASSWORD,
            "--wcguid",   $Wcguid,
            "--keyfile",  $Pfx,
            "--keypassword", $env:PACE_KEYPASSWORD,
            "--in",  $DestAAX,
            "--out", $DestAAX
        )
        & $WrapToolPath $SigningArgs 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
        if ($LASTEXITCODE -eq 0) {
            Write-Success "AAX signed (dev pfx)"
            $AAXSigningStatus = "signed_devcert"
        } else {
            Write-Host "Warning: AAX signing failed (exit $LASTEXITCODE)" -ForegroundColor Yellow
            $AAXSigningStatus = "signing_failed"
        }
    }
}

# ----------------------------------------------------------------------------
# Step 3.5: Authenticode signing (VST3 DLL + CLAP + Standalone exe)
# ----------------------------------------------------------------------------
Write-Header "Step 3.5: Signing Windows binaries (Authenticode)"
$VST3InnerPE = Join-Path $DestVST3 "Contents\x86_64-win\DX10R.vst3"
Invoke-AuthenticodeSign -Paths @($VST3InnerPE, $DestCLAP, $DestStandalone)

# ReadMe
Write-Step "Creating documentation..."
$ReadmeContent = @"
DX10R $Version - Windows Installation Guide
====================================================

Important: Required Software
-------------------
This plugin requires the Microsoft Visual C++ 2019-2022 Redistributable.
If the plugin fails to load, install it from:
https://aka.ms/vs/17/release/vc_redist.x64.exe

Installation Steps
-------------------
1. Close your DAW before proceeding.

2. VST3 Plugin:   copy DX10R.vst3 to  C:\Program Files\Common Files\VST3\
3. CLAP Plugin:   copy DX10R.clap to  C:\Program Files\Common Files\CLAP\
4. Standalone:    copy DX10R.exe to any preferred location.
"@
if ($BuildAAX) {
    $ReadmeContent += @"


5. AAX Plugin (Pro Tools):
   copy DX10R.aaxplugin to  C:\Program Files\Common Files\Avid\Audio\Plug-Ins\
"@
}
$ReadmeContent += @"


Launch your DAW and rescan plugins.
"@
$ReadmeContent | Out-File -FilePath "$WinDir\ReadMe.txt" -Encoding UTF8
Write-Success "Documentation created"

# version.json
$formats = @("VST3", "CLAP", "Standalone")
if ($BuildAAX) { $formats += "AAX" }
$VersionInfo = @{
    name        = "DX10R"
    version     = $Version
    build_date  = $BuildDate
    platform    = "Windows"
    architecture = "x64"
    formats     = $formats
    webui       = "embedded"
    build_type  = $Configuration
    aax_signing = if ($BuildAAX) { $AAXSigningStatus } else { "N/A" }
    code_signing = $CodeSigningStatus
} | ConvertTo-Json
$VersionInfo | Out-File -FilePath "$WinDir\version.json" -Encoding UTF8
Write-Success "Version info created"

# LICENSE
if (Test-Path "$RootDir\LICENSE") {
    Copy-Item -Path "$RootDir\LICENSE" -Destination "$WinDir\LICENSE.txt" -Force
}

# ZIP archive
Write-Step "Creating ZIP archive..."
if ($BuildAAX) {
    $ZipName = "DX10R_${Version}_Windows_VST3_AAX_CLAP_Standalone.zip"
} else {
    $ZipName = "DX10R_${Version}_Windows_VST3_CLAP_Standalone.zip"
}
$ZipPath = "$OutputDir\$ZipName"
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path "$WinDir\*" -DestinationPath $ZipPath -CompressionLevel Optimal
Write-Success "ZIP archive created"

# ----------------------------------------------------------------------------
# Step 4: Inno Setup installer (optional)
# ----------------------------------------------------------------------------
Write-Header "Step 4: Creating installer with Inno Setup"

$InnoSetupPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
$InstallerScript = "$RootDir\installer.iss"

if ((Test-Path $InnoSetupPath) -and (Test-Path $InstallerScript)) {
    Write-Step "Building installer with Inno Setup..."
    & $InnoSetupPath /DMyAppVersion="$Version" /Q $InstallerScript
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Installer created successfully"
        $InstallerExe = "$OutputDir\DX10R_${Version}_Windows_Setup.exe"
        Invoke-AuthenticodeSign -Paths @($InstallerExe)
    } else {
        Write-Host "Warning: Installer creation failed (exit $LASTEXITCODE)" -ForegroundColor Yellow
    }
} else {
    Write-Host "Warning: Inno Setup or installer.iss not found - skipping installer" -ForegroundColor Yellow
}

# Final summary
$FileInfo = Get-Item $ZipPath
$SizeMB = [math]::Round($FileInfo.Length / 1MB, 2)
Write-Header "Build completed successfully!"
Write-Host "Package: $ZipPath" -ForegroundColor White
Write-Host "Size:    $SizeMB MB" -ForegroundColor White
Write-Host ""
if ($CodeSigningStatus -eq "signed") {
    Write-Host "[OK]  VST3 / CLAP / Standalone / Installer signed" -ForegroundColor Green
} else {
    Write-Host "[!!]  Authenticode signing: $CodeSigningStatus" -ForegroundColor Yellow
}
if ($BuildAAX) {
    if ($AAXSigningStatus -eq "signed_devcert") {
        Write-Host "[OK]  AAX signed via PACE Eden + dev cert (Authenticode root NOT trusted)" -ForegroundColor Yellow
    } else {
        Write-Host "[!!]  AAX: $AAXSigningStatus" -ForegroundColor Yellow
    }
}
