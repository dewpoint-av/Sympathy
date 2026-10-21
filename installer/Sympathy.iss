; Sympathy — Inno Setup installer
; Packages the CLAP and the VST3 bundle to their standard system-wide
; locations. Run build.bat first so build\dist\ is populated.

#define MyAppName "Sympathy"
#define MyAppVersion "1.2.0"
#define MyAppPublisher "Dewpoint AV"
#define MyAppURL "https://dewpoint.live"

[Setup]
AppId={{8B4F5A2C-1D3E-4A6B-9C0D-7E8F2A3B4C5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Dewpoint AV\{#MyAppName}
DefaultGroupName=Dewpoint AV\{#MyAppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=output
OutputBaseFilename={#MyAppName}-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName={#MyAppName} (Dewpoint AV)

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 bundle -> C:\Program Files\Common Files\VST3\Sympathy.vst3
; {commoncf} is the system Common Files directory (COMMONPROGRAMFILES).
Source: "..\build\dist\Sympathy.vst3\*"; DestDir: "{commoncf}\VST3\{#MyAppName}.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; CLAP -> C:\Program Files\Common Files\CLAP\Sympathy.clap
; This is the standard system-wide CLAP search path (%COMMONPROGRAMFILES%\CLAP).
Source: "..\build\dist\Sympathy.clap"; DestDir: "{commoncf}\CLAP"; Flags: ignoreversion

[InstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\Sympathy.vst3"
Type: files; Name: "{commoncf}\CLAP\Sympathy.clap"

[Icons]
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

[Messages]
WelcomeLabel1=Sympathy Physical Modeling Resonator
WelcomeLabel2=This will install Sympathy (VST3 + CLAP) on your computer.%n%nClick Next to continue.
