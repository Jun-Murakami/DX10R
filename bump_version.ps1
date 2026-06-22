<#
.SYNOPSIS
    DX10R のバージョン番号を全所在で一括同期するスクリプト。

.DESCRIPTION
    VERSION ファイルを正本 (Single Source of Truth) とし、ビルドに関わる全ての
    バージョン記述を 1 コマンドで揃える。手で書き換える箇所が多く drift しやすい
    ため、リリースタグを切る前に必ずこのスクリプトを通すこと。

    同期対象:
      - VERSION                              (ビルドの正本。build_windows.ps1 が読む)
      - webui/package.json                   ("version")
      - plugin/config.h                      (PLUG_VERSION_STR / PLUG_VERSION_HEX)
      - plugin/resources/main.rc             (FILEVERSION / PRODUCTVERSION + VALUE 文字列)
      - plugin/resources/*-Info.plist        (CFBundleShortVersionString /
                                              CFBundleVersion / CFBundleGetInfoString /
                                              AU "AudioUnit Version" hex / AudioComponents version 整数)

    installer.iss は build_windows.ps1 が VERSION から /DMyAppVersion で渡すため
    ここでは触らない (自動追従)。

    リリースノート:
      バージョン同期後、releases/changelog.txt の先頭へ新しいエントリを追記する。
      更新内容は対話入力 (複数行可、'.' だけの行で確定)。-DryRun 時はスキップ。

.PARAMETER Version
    設定するバージョン。明示指定 (1.2.3) / 相対指定 (major|minor|patch)。
    省略時は現在のバージョンと使い方を表示する。

.PARAMETER DryRun
    実際にファイルを書き換えず、変更内容のプレビューだけ表示する。

.EXAMPLE
    ./bump_version.ps1 0.2.0
    ./bump_version.ps1 patch
    ./bump_version.ps1 minor -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Version,

    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$RootDir = $PSScriptRoot
$VersionFile = Join-Path $RootDir 'VERSION'

# --- 現在バージョンの取得 -----------------------------------------------------
if (-not (Test-Path $VersionFile)) {
    Write-Error "VERSION file not found at: $VersionFile"
    exit 1
}
$CurrentVersion = (Get-Content $VersionFile -Raw).Trim()

function Show-Usage {
    Write-Host ""
    Write-Host "現在のバージョン: " -NoNewline
    Write-Host $CurrentVersion -ForegroundColor Cyan
    Write-Host ""
    Write-Host "使い方:"
    Write-Host "  ./bump_version.ps1 1.2.3        # 明示指定"
    Write-Host "  ./bump_version.ps1 patch        # 0.0.1 -> 0.0.2"
    Write-Host "  ./bump_version.ps1 minor        # 0.0.1 -> 0.1.0"
    Write-Host "  ./bump_version.ps1 major        # 0.0.1 -> 1.0.0"
    Write-Host "  ./bump_version.ps1 patch -DryRun"
    Write-Host ""
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    Show-Usage
    exit 0
}

# --- 新バージョンの決定 -------------------------------------------------------
function Parse-SemVer([string]$v) {
    if ($v -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
        Write-Error "バージョンは MAJOR.MINOR.PATCH 形式 (例 1.2.3) で指定してください: '$v'"
        exit 1
    }
    return [pscustomobject]@{
        Major = [int]$Matches[1]
        Minor = [int]$Matches[2]
        Patch = [int]$Matches[3]
    }
}

$cur = Parse-SemVer $CurrentVersion
switch ($Version.ToLower()) {
    'major' { $NewVersion = "$($cur.Major + 1).0.0" }
    'minor' { $NewVersion = "$($cur.Major).$($cur.Minor + 1).0" }
    'patch' { $NewVersion = "$($cur.Major).$($cur.Minor).$($cur.Patch + 1)" }
    default { $NewVersion = $Version }
}

$new = Parse-SemVer $NewVersion

# iPlug2 PLUG_VERSION_HEX 規約: 0x00MMmmpp (major byte2 / minor byte1 / patch byte0)
$VersionInt = ($new.Major -shl 16) -bor ($new.Minor -shl 8) -bor $new.Patch
$VersionHex = '0x{0:x8}' -f $VersionInt

Write-Host ""
Write-Host "DX10R version bump: " -NoNewline
Write-Host "$CurrentVersion" -ForegroundColor DarkGray -NoNewline
Write-Host "  ->  " -NoNewline
Write-Host "$NewVersion" -ForegroundColor Green
Write-Host "  hex = $VersionHex / int = $VersionInt"
if ($DryRun) { Write-Host "  (DryRun: ファイルは書き換えません)" -ForegroundColor Yellow }
Write-Host ""

# --- ファイル更新ヘルパ -------------------------------------------------------
$script:Changed = 0
$script:Touched = 0

# UTF-8 (BOM なし) で読み書きし、正規表現ベースで置換する。
function Update-File {
    param(
        [string]$Path,
        [array]$Edits
    )

    $full = Join-Path $RootDir $Path
    if (-not (Test-Path $full)) {
        Write-Host "  [skip] $Path (見つかりません)" -ForegroundColor Yellow
        return
    }

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $text = [System.IO.File]::ReadAllText($full)
    $orig = $text
    $hits = 0

    foreach ($e in $Edits) {
        $re = [regex]$e.Pattern
        $limit = if ($e.ContainsKey('Limit')) { $e.Limit } else { -1 }  # -1 = all
        $matches = $re.Matches($text)
        if ($matches.Count -eq 0) {
            Write-Host "  [warn] ${Path}: パターン未マッチ -> $($e.Pattern)" -ForegroundColor Yellow
            continue
        }
        if ($limit -lt 0) {
            $text = $re.Replace($text, $e.Replacement)
            $hits += $matches.Count
        }
        else {
            $text = $re.Replace($text, $e.Replacement, $limit)
            $hits += [Math]::Min($limit, $matches.Count)
        }
    }

    if ($text -ne $orig) {
        $script:Changed += $hits
        $script:Touched += 1
        if (-not $DryRun) {
            [System.IO.File]::WriteAllText($full, $text, $utf8)
        }
        Write-Host "  [ok]   $Path  ($hits 箇所)" -ForegroundColor Green
    }
    else {
        Write-Host "  [--]   $Path  (変更なし)" -ForegroundColor DarkGray
    }
}

# --- 1. VERSION ---------------------------------------------------------------
if (-not $DryRun) {
    $utf8nl = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($VersionFile, "$NewVersion`n", $utf8nl)
}
$script:Touched += 1
Write-Host "  [ok]   VERSION" -ForegroundColor Green

# --- 2. webui/package.json ----------------------------------------------------
Update-File -Path 'webui/package.json' -Edits @(
    @{ Pattern = '(?m)^(\s{2}"version":\s*")[^"]*(")'; Replacement = "`${1}$NewVersion`$2"; Limit = 1 }
)

# --- 3. plugin/config.h -------------------------------------------------------
Update-File -Path 'plugin/config.h' -Edits @(
    @{ Pattern = '(#define\s+PLUG_VERSION_HEX\s+)0x[0-9A-Fa-f]+'; Replacement = "`${1}$VersionHex" }
    @{ Pattern = '(#define\s+PLUG_VERSION_STR\s+")[^"]*(")';      Replacement = "`${1}$NewVersion`$2" }
)

# --- 4. plugin/resources/main.rc ---------------------------------------------
$rcComma = "$($new.Major),$($new.Minor),$($new.Patch),0"
Update-File -Path 'plugin/resources/main.rc' -Edits @(
    @{ Pattern = '(FILEVERSION\s+)\d+,\d+,\d+,\d+';    Replacement = "`${1}$rcComma" }
    @{ Pattern = '(PRODUCTVERSION\s+)\d+,\d+,\d+,\d+'; Replacement = "`${1}$rcComma" }
    @{ Pattern = '(VALUE\s+"FileVersion",\s+")[^"]*(")';    Replacement = "`${1}$NewVersion`$2" }
    @{ Pattern = '(VALUE\s+"ProductVersion",\s+")[^"]*(")'; Replacement = "`${1}$NewVersion`$2" }
)

# --- 5. plist 群 --------------------------------------------------------------
$plists = Get-ChildItem (Join-Path $RootDir 'plugin/resources') -Filter '*-Info.plist'
foreach ($pl in $plists) {
    $rel = "plugin/resources/$($pl.Name)"
    $edits = @(
        @{ Pattern = '(<string>[^<]*\bv)\d+\.\d+\.\d+(\b[^<]*Copyright[^<]*</string>)'; Replacement = "`${1}$NewVersion`$2" }
        @{ Pattern = '(<key>CFBundleShortVersionString</key>\s*<string>)[^<]*(</string>)'; Replacement = "`${1}$NewVersion`$2" }
        @{ Pattern = '(<key>CFBundleVersion</key>\s*<string>)[^<]*(</string>)'; Replacement = "`${1}$NewVersion`$2" }
    )
    # AU plist のみ: hex 文字列と AudioComponents の version 整数も同期
    if ($pl.Name -match 'AU-Info\.plist$') {
        $edits += @{ Pattern = '(<key>AudioUnit Version</key>\s*<string>)0x[0-9A-Fa-f]+(</string>)'; Replacement = "`${1}$VersionHex`$2" }
        $edits += @{ Pattern = '(<key>version</key>\s*<integer>)\d+(</integer>)'; Replacement = "`${1}$VersionInt`$2" }
    }
    Update-File -Path $rel -Edits $edits
}

# --- 6. releases/changelog.txt -------------------------------------------------
if (-not $DryRun) {
    $changelogDir  = Join-Path $RootDir 'releases'
    $changelogFile = Join-Path $changelogDir 'changelog.txt'

    $dateStr = (Get-Date).ToString('MMMM d, yyyy', [System.Globalization.CultureInfo]::InvariantCulture)
    $header  = "## Ver $NewVersion, $dateStr"

    Write-Host ""
    Write-Host "changelog (releases/changelog.txt) の更新内容を入力してください:" -ForegroundColor Cyan
    Write-Host "  $header" -ForegroundColor DarkGray
    Write-Host "  複数行可。改行して '.' だけの行 + Enter で確定。" -ForegroundColor DarkGray
    Write-Host ""

    $lines = New-Object System.Collections.Generic.List[string]
    while ($true) {
        $line = Read-Host
        if ($line -eq '.') { break }
        $lines.Add($line)
    }
    $body  = ($lines -join "`n").TrimEnd()
    $block = "$header`n$body".TrimEnd()

    $utf8nl = New-Object System.Text.UTF8Encoding($false)
    if (Test-Path $changelogFile) {
        $existing = [System.IO.File]::ReadAllText($changelogFile).TrimStart("`r", "`n")
        $final = ($block + "`n`n`n" + $existing).TrimEnd() + "`n"
    }
    else {
        if (-not (Test-Path $changelogDir)) {
            New-Item -ItemType Directory -Path $changelogDir -Force | Out-Null
        }
        $final = $block + "`n"
    }
    [System.IO.File]::WriteAllText($changelogFile, $final, $utf8nl)
    $script:Touched += 1
    Write-Host "  [ok]   releases/changelog.txt" -ForegroundColor Green
}
else {
    Write-Host "  [--]   releases/changelog.txt (DryRun のためスキップ)" -ForegroundColor DarkGray
}

# --- まとめ -------------------------------------------------------------------
Write-Host ""
Write-Host "更新ファイル数: $($script:Touched)" -ForegroundColor Cyan
if ($DryRun) {
    Write-Host "DryRun のため書き込みは行っていません。" -ForegroundColor Yellow
}
else {
    Write-Host "バージョンを $NewVersion に同期しました。" -ForegroundColor Green
    Write-Host ""
    Write-Host "次のステップ:" -ForegroundColor DarkGray
    Write-Host "  - git diff で差分を確認 (releases/changelog.txt も含む)" -ForegroundColor DarkGray
    Write-Host "  - ビルド: ./build_windows.ps1" -ForegroundColor DarkGray
}
Write-Host ""
