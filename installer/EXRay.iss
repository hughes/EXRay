; EXRay Inno Setup Script
; Build the installer with: iscc installer\EXRay.iss
;
; IMPORTANT: Before compiling the installer, build and stage the release binary:
;   bazelisk build -c opt //:EXRay
;   cp bazel-out/x64_windows-opt/bin/EXRay.exe installer/
;
; The cp step is required because bazel-out is a symlink that iscc can't follow.
; Copying from the -opt path guarantees we never package a debug build.

#define MyAppName "EXRay"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Matt Hughes"
#define MyAppURL "https://github.com/hughes/EXRay"
#define MyAppExeName "EXRay.exe"

[Setup]
AppId={{387a4a4c-d19e-4ae5-9bfc-bbaa59ceccb1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={userpf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=..\dist
OutputBaseFilename=EXRay-{#MyAppVersion}-setup
SetupIconFile=..\resources\exray-icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
MinVersion=10.0
PrivilegesRequired=lowest
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=EXRay - EXR Image Viewer
VersionInfoCopyright=Copyright (c) 2025 Matt Hughes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "fileassoc"; Description: "Associate .exr files with {#MyAppName}"; GroupDescription: "File associations:"; Flags: checkedonce

[Files]
Source: "{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "..\THIRD_PARTY_LICENSES"; DestDir: "{app}"; DestName: "THIRD_PARTY_LICENSES.txt"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; File association - only when the user selects the task
Root: HKA; Subkey: "Software\Classes\.exr"; ValueType: string; ValueName: ""; ValueData: "EXRay.EXRFile"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\EXRay.EXRFile"; ValueType: string; ValueName: ""; ValueData: "OpenEXR Image"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\EXRay.EXRFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\EXRay.EXRFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: fileassoc
; Friendly name in "Open with" menu
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#MyAppName}"; Flags: uninsdeletekey

[Code]
function SHChangeNotify(wEventId: Integer; uFlags: Integer; dwItem1: Integer; dwItem2: Integer): Integer;
  external 'SHChangeNotify@shell32.dll stdcall';

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dummy: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    { SHCNE_ASSOCCHANGED = $08000000, SHCNF_IDLIST = $0000 }
    Dummy := 0;
    SHChangeNotify($08000000, $0000, Dummy, Dummy);
  end;
end;
