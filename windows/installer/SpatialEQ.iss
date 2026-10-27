; SpatialEQ for Windows: setup program for the app.
;   iscc /DVersion=1.1.1 /DAppExe=path\to\SpatialEQ.exe /DOutDir=out windows\installer\SpatialEQ.iss
#ifndef Version
  #define Version "1.1.1"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\windows\Release\SpatialEQ.exe"
#endif
#ifndef OutDir
  #define OutDir "..\..\out"
#endif
#define Branding "..\..\branding"

[Setup]
AppId={{6B0E1C52-7A0E-4C3B-9C11-5D2E8F3A9C41}
AppName=SpatialEQ
AppVersion={#Version}
AppPublisher=Savannah DSP
AppPublisherURL=https://savannahdsp.com
DefaultDirName={autopf}\Savannah DSP\SpatialEQ
DefaultGroupName=Savannah DSP
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir={#OutDir}
OutputBaseFilename=SpatialEQ-{#Version}-windows-x64-setup
SetupIconFile={#Branding}\icon\SpatialEQ.ico
UninstallDisplayIcon={app}\SpatialEQ.exe
WizardStyle=modern
WizardImageFile={#Branding}\installer\wizard-100.bmp,{#Branding}\installer\wizard-125.bmp,{#Branding}\installer\wizard-150.bmp,{#Branding}\installer\wizard-175.bmp,{#Branding}\installer\wizard-200.bmp
WizardSmallImageFile={#Branding}\installer\wizard-small-100.bmp,{#Branding}\installer\wizard-small-125.bmp,{#Branding}\installer\wizard-small-150.bmp,{#Branding}\installer\wizard-small-175.bmp,{#Branding}\installer\wizard-small-200.bmp
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SpatialEQ"; Filename: "{app}\SpatialEQ.exe"
Name: "{autodesktop}\SpatialEQ"; Filename: "{app}\SpatialEQ.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SpatialEQ.exe"; Description: "{cm:LaunchProgram,SpatialEQ}"; Flags: nowait postinstall skipifsilent
