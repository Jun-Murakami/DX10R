; DX10R Installer Script for Inno Setup
; Requires Inno Setup 6.3.0 or later.
;
; DX10R is a single instrument product. Wizard:
;   1. Format selection page : VST3 / AAX / CLAP / Standalone (multi-select)
;   2. Install-path page      : per-format destinations
; Each [Files]/[Icons]/[Registry] entry is gated by a Check: function on format.

#define MyAppName "DX10R"
#define MyAppPublisher "Jun Murakami"
#define MyAppURL "https://junmurakami.com/dx10r"
#define MyAppExeName "DX10R.exe"
#define MyAppBundleVST3 "DX10R.vst3"
#define MyAppBundleAAX  "DX10R.aaxplugin"
#define MyAppClap "DX10R.clap"

; Version is supplied by build_windows.ps1 via /DMyAppVersion.
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

[Setup]
; Stable per-plugin AppId; do NOT regenerate per release or upgrade detection breaks.
AppId={{8F2C1A6E-4D3B-4E9A-9C7F-1B2D3E4F5A6B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; LicenseFile intentionally omitted: DX10R's license (mda DX10 is GPL/MIT-derived)
; is still being decided. Drop a LICENSE file in the repo root and add
; `LicenseFile=LICENSE` here to show an EULA page.
OutputDir=releases\{#MyAppVersion}
OutputBaseFilename=DX10R_{#MyAppVersion}_Windows_Setup
SetupIconFile=plugin\resources\DX10R.ico
UninstallDisplayIcon={uninstallexe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
DisableDirPage=no
DisableWelcomePage=no
ShowLanguageDialog=auto
UsePreviousAppDir=yes

[Languages]
Name: "english";  MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Check: FmtApp; Flags: unchecked

[Files]
; Sources point at the staged artefacts under releases\<Version>\Windows\,
; which build_windows.ps1 fills in during Step 3 (packaging).
Source: "releases\{#MyAppVersion}\Windows\{#MyAppExeName}"; DestDir: "{code:GetStandalonePath}"; Flags: ignoreversion; Check: FmtApp
Source: "releases\{#MyAppVersion}\Windows\{#MyAppBundleVST3}\*"; DestDir: "{code:GetVST3Path}\{#MyAppBundleVST3}"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: FmtVST3
Source: "releases\{#MyAppVersion}\Windows\{#MyAppClap}"; DestDir: "{code:GetClapPath}"; Flags: ignoreversion skipifsourcedoesntexist; Check: FmtClap
; AAX bundle (entire dir). PrepareToInstall wipes any existing bundle first.
Source: "releases\{#MyAppVersion}\Windows\{#MyAppBundleAAX}\*"; DestDir: "{code:GetAAXPath}\{#MyAppBundleAAX}"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist; Check: FmtAAX

; Documentation
Source: "releases\{#MyAppVersion}\Windows\ReadMe.txt"; DestDir: "{code:GetDocPath}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{code:GetDocPath}"; DestName: "LICENSE.txt"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{code:GetStandalonePath}\{#MyAppExeName}"; Check: FmtApp
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{code:GetStandalonePath}\{#MyAppExeName}"; Tasks: desktopicon; Check: FmtApp

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Redistributables..."; Check: NeedsVCRedist; Flags: waituntilterminated

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\DX10R"
Type: filesandordirs; Name: "{userappdata}\DX10R"

[Registry]
Root: HKLM64; Subkey: "Software\VST3"; ValueType: string; ValueName: "DX10R"; ValueData: "{code:GetVST3Path}\{#MyAppBundleVST3}"; Flags: uninsdeletevalue; Check: FmtVST3
Root: HKLM64; Subkey: "Software\Avid\ProTools\AAX"; ValueType: string; ValueName: "DX10R"; ValueData: "{code:GetAAXPath}\{#MyAppBundleAAX}"; Flags: uninsdeletevalue; Check: FmtAAX

[Code]
var
  DownloadPage: TDownloadWizardPage;
  VCRedistNeeded: Boolean;
  FormatPage: TInputOptionWizardPage;    // VST3 / AAX / CLAP / Standalone
  PathSelectionPage: TInputDirWizardPage;
  StandalonePath: String;
  VST3Path: String;
  ClapPath: String;
  AAXPath: String;

// --- format selection (safe False if page not yet created) ---
function FmtVST3(): Boolean; begin Result := Assigned(FormatPage) and FormatPage.Values[0]; end;
function FmtAAX(): Boolean;  begin Result := Assigned(FormatPage) and FormatPage.Values[1]; end;
function FmtClap(): Boolean; begin Result := Assigned(FormatPage) and FormatPage.Values[2]; end;
function FmtApp(): Boolean;  begin Result := Assigned(FormatPage) and FormatPage.Values[3]; end;

function NeedsVCRedist(): Boolean;
var
  Version: String;
begin
  Result := not (RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version) or
                 RegQueryStringValue(HKLM64, 'SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version));
  if not Result then
  begin
    if CompareStr(Version, 'v14.30') < 0 then Result := True;
  end;
  VCRedistNeeded := Result;
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
var
  FmtTitle, FmtSubtitle, FmtDesc, FmtV, FmtA, FmtC, FmtS: String;
  PathPageTitle, PathPageSubtitle, PathPageDesc: String;
  StandaloneLabel, Vst3Label, ClapLabel, AaxLabel: String;
begin
  VCRedistNeeded := False;
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);

  if ActiveLanguage = 'japanese' then
  begin
    FmtTitle := 'プラグイン形式の選択'; FmtSubtitle := 'インストールする形式を選んでください';
    FmtDesc := 'インストールするプラグイン形式を選んでください (複数選択可)。';
    FmtV := 'VST3 プラグイン'; FmtA := 'AAX プラグイン (Pro Tools)'; FmtC := 'CLAP プラグイン'; FmtS := 'スタンドアロン アプリケーション';
    PathPageTitle := 'インストール先の選択'; PathPageSubtitle := '各形式のインストール先を指定してください';
    PathPageDesc := '選択した各プラグイン形式の配置先を指定します。';
    StandaloneLabel := 'スタンドアロン:'; Vst3Label := 'VST3:'; ClapLabel := 'CLAP:'; AaxLabel := 'AAX (Pro Tools):';
  end
  else
  begin
    FmtTitle := 'Select Plugin Formats'; FmtSubtitle := 'Which formats would you like to install?';
    FmtDesc := 'Choose the plugin formats to install. You can pick more than one.';
    FmtV := 'VST3 Plugin'; FmtA := 'AAX Plugin (Pro Tools)'; FmtC := 'CLAP Plugin'; FmtS := 'Standalone Application';
    PathPageTitle := 'Select Installation Paths'; PathPageSubtitle := 'Where should each format be installed?';
    PathPageDesc := 'Select the installation folders for each plugin format you have chosen.';
    StandaloneLabel := 'Standalone:'; Vst3Label := 'VST3:'; ClapLabel := 'CLAP:'; AaxLabel := 'AAX (Pro Tools):';
  end;

  FormatPage := CreateInputOptionPage(wpLicense, FmtTitle, FmtSubtitle, FmtDesc, False, False);
  FormatPage.Add(FmtV); FormatPage.Add(FmtA); FormatPage.Add(FmtC); FormatPage.Add(FmtS);
  FormatPage.Values[0] := True; FormatPage.Values[1] := True; FormatPage.Values[2] := True; FormatPage.Values[3] := True;

  PathSelectionPage := CreateInputDirPage(FormatPage.ID, PathPageTitle, PathPageSubtitle, PathPageDesc, False, '');
  PathSelectionPage.Add(StandaloneLabel); PathSelectionPage.Values[0] := ExpandConstant('{autopf}\DX10R');
  PathSelectionPage.Add(Vst3Label);       PathSelectionPage.Values[1] := ExpandConstant('{commoncf64}\VST3');
  PathSelectionPage.Add(ClapLabel);       PathSelectionPage.Values[2] := ExpandConstant('{commoncf64}\CLAP');
  PathSelectionPage.Add(AaxLabel);        PathSelectionPage.Values[3] := ExpandConstant('{commoncf64}\Avid\Audio\Plug-Ins');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if PageID = wpSelectDir then
    Result := True
  else if PageID = PathSelectionPage.ID then
  begin
    PathSelectionPage.Edits[0].Visible := FmtApp;   PathSelectionPage.Buttons[0].Visible := FmtApp;   PathSelectionPage.PromptLabels[0].Visible := FmtApp;
    PathSelectionPage.Edits[1].Visible := FmtVST3;  PathSelectionPage.Buttons[1].Visible := FmtVST3;  PathSelectionPage.PromptLabels[1].Visible := FmtVST3;
    PathSelectionPage.Edits[2].Visible := FmtClap;  PathSelectionPage.Buttons[2].Visible := FmtClap;  PathSelectionPage.PromptLabels[2].Visible := FmtClap;
    PathSelectionPage.Edits[3].Visible := FmtAAX;   PathSelectionPage.Buttons[3].Visible := FmtAAX;   PathSelectionPage.PromptLabels[3].Visible := FmtAAX;
    Result := not (FmtVST3 or FmtAAX or FmtClap or FmtApp);
  end
  else if PageID = DownloadPage.ID then
    Result := not VCRedistNeeded;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = FormatPage.ID then
  begin
    if not (FormatPage.Values[0] or FormatPage.Values[1] or FormatPage.Values[2] or FormatPage.Values[3]) then
    begin
      MsgBox('Please select at least one plugin format to install.', mbError, MB_OK);
      Result := False;
    end;
  end
  else if CurPageID = PathSelectionPage.ID then
  begin
    StandalonePath := PathSelectionPage.Values[0];
    VST3Path := PathSelectionPage.Values[1];
    ClapPath := PathSelectionPage.Values[2];
    AAXPath := PathSelectionPage.Values[3];
  end
  else if CurPageID = wpReady then
  begin
    if NeedsVCRedist() then
    begin
      DownloadPage.Clear;
      DownloadPage.Add('https://aka.ms/vs/17/release/vc_redist.x64.exe', 'vc_redist.x64.exe', '');
      DownloadPage.Show;
      try
        DownloadPage.Download;
      except
        MsgBox('Failed to download Visual C++ Redistributables. Install manually from https://aka.ms/vs/17/release/vc_redist.x64.exe', mbError, MB_OK);
      end;
    end;
  end;
end;

function ProbeVST3Lock(): Boolean;
var
  TargetPath, TestFile: String;
begin
  Result := False;
  if VST3Path <> '' then TargetPath := VST3Path else TargetPath := ExpandConstant('{commoncf64}\VST3');
  TestFile := TargetPath + '\DX10R.vst3\Contents\x86_64-win\DX10R.vst3';
  if FileExists(TestFile) then Result := not DeleteFile(TestFile);
end;

function WipeAAXBundle(): Boolean;
var
  TargetPath: String;
begin
  Result := False;
  if AAXPath <> '' then TargetPath := AAXPath else TargetPath := ExpandConstant('{commoncf64}\Avid\Audio\Plug-Ins');
  if DirExists(TargetPath + '\DX10R.aaxplugin') then
    Result := not DelTree(TargetPath + '\DX10R.aaxplugin', True, True, True);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  FileLocked: Boolean;
begin
  Result := '';
  FileLocked := False;
  if FmtVST3 then FileLocked := ProbeVST3Lock();
  if FileLocked then
  begin
    if MsgBox('The VST3 plugin appears to be in use by a DAW. Close it and try again. Continue anyway?', mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := 'Plugin files are locked. Please close your DAW and try again.';
      Exit;
    end;
  end;
  if (not FileLocked) and FmtAAX then FileLocked := WipeAAXBundle();
  if FileLocked then
  begin
    if MsgBox('The AAX plugin appears to be in use by Pro Tools. Close it and try again. Continue anyway?', mbConfirmation, MB_YESNO) = IDNO then
      Result := 'Plugin files are locked. Please close Pro Tools and try again.';
  end;
end;

function GetStandalonePath(Param: String): String;
begin
  if StandalonePath = '' then Result := ExpandConstant('{autopf}\DX10R') else Result := StandalonePath;
end;
function GetVST3Path(Param: String): String;
begin
  if VST3Path = '' then Result := ExpandConstant('{commoncf64}\VST3') else Result := VST3Path;
end;
function GetClapPath(Param: String): String;
begin
  if ClapPath = '' then Result := ExpandConstant('{commoncf64}\CLAP') else Result := ClapPath;
end;
function GetAAXPath(Param: String): String;
begin
  if AAXPath = '' then Result := ExpandConstant('{commoncf64}\Avid\Audio\Plug-Ins') else Result := AAXPath;
end;
function GetDocPath(Param: String): String;
begin
  if FmtApp then Result := GetStandalonePath('') else Result := ExpandConstant('{autopf}\DX10R');
end;

procedure DeinitializeUninstall();
begin
  DelTree(ExpandConstant('{localappdata}\DX10R'), True, True, True);
  DelTree(ExpandConstant('{userappdata}\DX10R'), True, True, True);
end;
