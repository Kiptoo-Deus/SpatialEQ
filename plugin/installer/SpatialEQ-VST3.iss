; SpatialEQ VST3 plug-in for Windows: installs SpatialEQ.vst3 into the common VST3 folder.
;   iscc /DVersion=1.1.1 /DBundle=path\to\SpatialEQ.vst3 /DOutDir=out plugin\installer\SpatialEQ-VST3.iss
#ifndef Version
  #define Version "1.1.1"
#endif
#ifndef Bundle
  #define Bundle "..\..\build\plugin\VST3\Release\SpatialEQ.vst3"
#endif
#ifndef OutDir
  #define OutDir "..\..\out"
#endif
#define Branding "..\..\branding"

[Setup]
AppId={{3F7A2D90-1B64-4E85-A2C7-9E0D4B6F1A38}
AppName=SpatialEQ VST3
AppVersion={#Version}
AppPublisher=Savannah DSP
AppPublisherURL=https://savannahdsp.com
; The VST3 folder is fixed by the VST3 specification.
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=SpatialEQ-VST3-{#Version}-windows-x64-setup
SetupIconFile={#Branding}\icon\SpatialEQ.ico
UninstallDisplayName=SpatialEQ VST3 plug-in
UninstallDisplayIcon={app}\SpatialEQ.vst3\PlugIn.ico
WizardStyle=modern
WizardImageFile={#Branding}\installer\wizard-100.bmp,{#Branding}\installer\wizard-125.bmp,{#Branding}\installer\wizard-150.bmp,{#Branding}\installer\wizard-175.bmp,{#Branding}\installer\wizard-200.bmp
WizardSmallImageFile={#Branding}\installer\wizard-small-100.bmp,{#Branding}\installer\wizard-small-125.bmp,{#Branding}\installer\wizard-small-150.bmp,{#Branding}\installer\wizard-small-175.bmp,{#Branding}\installer\wizard-small-200.bmp
Compression=lzma2
SolidCompression=yes

[Files]
; The whole bundle folder, including desktop.ini + PlugIn.ico (the folder icon).
Source: "{#Bundle}\*"; DestDir: "{app}\SpatialEQ.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; Explorer shows a folder's custom icon only when the folder has the System attribute.
Filename: "{cmd}"; Parameters: "/c attrib +s ""{app}\SpatialEQ.vst3"" & attrib +h +s ""{app}\SpatialEQ.vst3\desktop.ini"""; Flags: runhidden

[Messages]
WelcomeLabel2=This will install the SpatialEQ VST3 plug-in into your VST3 folder:%n%nC:\Program Files\Common Files\VST3%n%nIt needs Microsoft Edge WebView2, which Windows 10 and 11 already include. Close your DAW before continuing.
