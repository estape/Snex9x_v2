; Snex9x v2 - Inno Setup Installer Script
; Produces a modern standalone Windows Setup Wizard with custom directory selection

#define MyAppName "Snex9x v2"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "Rodrigo Estape"
#define MyAppURL "https://github.com/estape/Snex9x_v2"
#define MyAppExeName "Snex9x_v2.exe"

[Setup]
AppId={{D37E84B1-37E4-4D2A-B9B2-39E05B6088A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}

; Destination Directory & User Choice
DefaultDirName={autopf}\{#MyAppName}
DisableDirPage=no
DirExistsWarning=auto
AppendDefaultDirName=yes
UsePreviousAppDir=yes

; Allow choosing install scope (per-user or all users)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; Start Menu Group
DisableProgramGroupPage=no
DefaultGroupName={#MyAppName}
AllowNoIcons=yes

; Output Configuration
OutputDir=..\dist
OutputBaseFilename=Snex9x_v2_Setup_v{#MyAppVersion}
SetupIconFile=..\assets\snex9x.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; 64-bit Architecture
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate_roms"; Description: "Associate Super Nintendo ROM files (.sfc, .smc) with Snex9x v2"; GroupDescription: "File Associations:"

[Files]
; Main Executable & SDL3 Library
Source: "..\build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\SDL3.dll"; DestDir: "{app}"; Flags: ignoreversion

; Frontend UI Files
Source: "..\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs

; Database & Default Configurations (do not overwrite existing user settings on upgrade)
Source: "..\config\games_db.json"; DestDir: "{app}\config"; Flags: ignoreversion
Source: "..\config\controls.json"; DestDir: "{app}\config"; Flags: onlyifdoesntexist
Source: "..\config\settings.json"; DestDir: "{app}\config"; Flags: onlyifdoesntexist

; Documentation
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.md"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
; Ensure normal user accounts can write saves and cache even if installed to Program Files
Name: "{app}\config"; Permissions: users-modify
Name: "{app}\config\boxarts"; Permissions: users-modify
Name: "{app}\config\roms"; Permissions: users-modify
Name: "{app}\config\saves"; Permissions: users-modify

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Register .sfc association
Root: HKA; Subkey: "Software\Classes\.sfc"; ValueType: string; ValueName: ""; ValueData: "Snex9x.ROM"; Flags: uninsdeletevalue; Tasks: associate_roms
Root: HKA; Subkey: "Software\Classes\.smc"; ValueType: string; ValueName: ""; ValueData: "Snex9x.ROM"; Flags: uninsdeletevalue; Tasks: associate_roms
Root: HKA; Subkey: "Software\Classes\Snex9x.ROM"; ValueType: string; ValueName: ""; ValueData: "Super Nintendo ROM"; Flags: uninsdeletekey; Tasks: associate_roms
Root: HKA; Subkey: "Software\Classes\Snex9x.ROM\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate_roms
Root: HKA; Subkey: "Software\Classes\Snex9x.ROM\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate_roms

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
