; xMenuForge Inno Setup Script
#define AppName "xMenuForge"
#define AppVersion "1.0.0.0"
#define AppPublisher "xCONFLiCTiONx"

[Setup]
AppId={{7A4E9C10-5B2D-4F81-A310-9C6E2B410001}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\xMenuForge.ico
OutputDir=dist
OutputBaseFilename=xMenuForge_Setup
SetupIconFile=assets\xMenuForge.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "dist\xMenuForge\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\xMenuForge Settings"; Filename: "{app}\xMenuForge.Settings.exe"; IconFilename: "{app}\xMenuForge.ico"

[Run]
Filename: "{app}\xMenuForge.Host.exe"; Parameters: "--register"; StatusMsg: "Registering Windows 11 Explorer Integration..."; Flags: runhidden waituntilterminated
Filename: "{app}\xMenuForge.Settings.exe"; Description: "Launch xMenuForge Settings"; Flags: nowait postinstall skipifsilent shellexec

[UninstallRun]
Filename: "{app}\xMenuForge.Host.exe"; Parameters: "--unregister"; RunOnceId: "UnregisterXMenuForge"; StatusMsg: "Unregistering Explorer Integration..."; Flags: runhidden waituntilterminated
