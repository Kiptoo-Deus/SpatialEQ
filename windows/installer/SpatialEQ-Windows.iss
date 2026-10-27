; SpatialEQ for Windows: one setup program for the app and the VST3 plug-in (each selectable).
;   iscc /DVersion=1.2.0 /DAppExe=path\to\SpatialEQ.exe /DBundle=path\to\SpatialEQ.vst3 /DOutDir=out ^
;        windows\installer\SpatialEQ-Windows.iss
#ifndef Version
  #define Version "1.2.0"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\windows\Release\SpatialEQ.exe"
#endif
#ifndef Bundle
  #define Bundle "..\..\build\plugin\VST3\Release\SpatialEQ.vst3"
#endif
#ifndef OutDir
  #define OutDir "..\..\out"
#endif
#define Branding "..\..\branding"

[Setup]
; Same AppId as the earlier app-only installer, so this upgrades it.
AppId={{6B0E1C52-7A0E-4C3B-9C11-5D2E8F3A9C41}
AppName=SpatialEQ
AppVersion={#Version}
AppVerName=SpatialEQ {#Version}
AppPublisher=Savannah DSP
AppPublisherURL=https://savannahdsp.com
DefaultDirName={autopf}\Savannah DSP\SpatialEQ
DefaultGroupName=Savannah DSP
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; The VST3 folder is under Program Files.
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=SpatialEQ-{#Version}-windows-x64-setup
SetupIconFile={#Branding}\icon\SpatialEQ.ico
UninstallDisplayIcon={app}\SpatialEQ.exe
UninstallDisplayName=SpatialEQ {#Version} (app and VST3 plug-in)
WizardStyle=modern
; Show the Welcome page (Inno Setup 6 hides it by default): the box art and what gets installed.
DisableWelcomePage=no
WizardImageFile={#Branding}\installer\wizard-100.bmp,{#Branding}\installer\wizard-125.bmp,{#Branding}\installer\wizard-150.bmp,{#Branding}\installer\wizard-175.bmp,{#Branding}\installer\wizard-200.bmp
WizardSmallImageFile={#Branding}\installer\wizard-small-100.bmp,{#Branding}\installer\wizard-small-125.bmp,{#Branding}\installer\wizard-small-150.bmp,{#Branding}\installer\wizard-small-175.bmp,{#Branding}\installer\wizard-small-200.bmp
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Types]
Name: "full"; Description: "App and VST3 plug-in"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "app"; Description: "SpatialEQ app: EQ and spatial audio for every app (Windows 10 2004 or later)"; Types: full custom
Name: "vst3"; Description: "VST3 plug-in (Cubase, Ableton Live, REAPER, Studio One, Bitwig...)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Components: app; Flags: unchecked

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; The whole bundle folder, including desktop.ini + PlugIn.ico (the folder icon). The VST3 folder is fixed
; by the VST3 specification.
Source: "{#Bundle}\*"; DestDir: "{commoncf64}\VST3\SpatialEQ.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\SpatialEQ"; Filename: "{app}\SpatialEQ.exe"; Components: app
Name: "{autodesktop}\SpatialEQ"; Filename: "{app}\SpatialEQ.exe"; Tasks: desktopicon

[Run]
; Explorer shows a folder's custom icon only when the folder has the System attribute.
Filename: "{cmd}"; Parameters: "/c attrib +s ""{commoncf64}\VST3\SpatialEQ.vst3"" & attrib +h +s ""{commoncf64}\VST3\SpatialEQ.vst3\desktop.ini"""; Components: vst3; Flags: runhidden
Filename: "{app}\SpatialEQ.exe"; Description: "{cm:LaunchProgram,SpatialEQ}"; Components: app; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SpatialEQ.vst3"

[Messages]
WelcomeLabel2=This will install SpatialEQ {#Version} by Savannah DSP:%n%n- the SpatialEQ app, for every app on your PC%n- the VST3 plug-in, for your DAW%n%nUntick either one on the next page. The plug-in needs Microsoft Edge WebView2, which Windows 10 and 11 already include. Close your DAW before continuing.
