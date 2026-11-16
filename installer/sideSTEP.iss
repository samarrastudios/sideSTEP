; sideSTEP setup program (Inno Setup 6). Per-user install, no admin rights.
; Build with:  scripts\package.ps1   (builds, then runs ISCC with the right defines)
; Output:      build\installer\sideSTEP-Setup-<version>.exe
;
; Does what scripts\install.ps1 does: copies the files, registers the Explorer
; thumbnail and preview handlers, offers sideSTEP as an app for STEP files, and
; (if the user leaves the box ticked) turns on the preview helper.

#define AppName "sideSTEP"
#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#define Source "..\build\Release"
; Folder holding the Visual C++ runtime DLLs to ship beside the app, so it runs
; on PCs that do not have the runtime installed. scripts\package.ps1 passes it.
#ifndef VCRedist
  #error Pass /DVCRedist=<...\VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT>, or use scripts\package.ps1
#endif

[Setup]
AppId={{B6E0B0C2-7B5E-4D0C-9D0B-51DE57E90E11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName}
AppPublisher=Adrian Samarra
AppPublisherURL=https://github.com/samarrastudios/sideSTEP
AppSupportURL=https://github.com/samarrastudios/sideSTEP/issues
AppComments=Created by Adrian Samarra. Open source, for Windows, released on GitHub.
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
SetupIconFile=..\assets\brand\icon\sidestep.ico
UninstallDisplayIcon={app}\sidestep.ico
WizardStyle=modern
OutputDir=..\build\installer
OutputBaseFilename=sideSTEP-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
CloseApplications=no

[Tasks]
; Ticked by default; the wording says plainly what it does.
Name: "previewhelper"; Description: "Let File Explorer preview downloaded STEP files without Windows' warning. (Windows marks downloaded files and refuses to preview them. sideSTEP removes that mark from STEP files only, using a small helper that starts when you sign in. You can switch it off later in sideSTEP under File.)"; GroupDescription: "Preview pane:"

[Files]
Source: "{#Source}\sideSTEP.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\sideSTEPThumbs.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Source}\sidestep-thumb.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VCRedist}\msvcp140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VCRedist}\msvcp140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VCRedist}\vcruntime140.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#VCRedist}\vcruntime140_1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\brand\icon\sidestep.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\sideSTEP.exe"; Comment: "STEP files, seen without the CAD package."

[Run]
; Explorer thumbnails and preview pane (per-user COM registration).
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\sideSTEPThumbs.dll"""; Flags: runhidden
; File type: sideSTEP appears under Open with and in Settings > Default apps.
Filename: "{app}\sideSTEP.exe"; Parameters: "--register"; Flags: runhidden
; The preview helper, on or off as the user chose.
Filename: "{app}\sideSTEP.exe"; Parameters: "--helper-on"; Flags: runhidden nowait; Tasks: previewhelper
Filename: "{app}\sideSTEP.exe"; Parameters: "--helper-off"; Flags: runhidden; Tasks: not previewhelper
Filename: "{app}\sideSTEP.exe"; Description: "Open sideSTEP"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; --unregister also switches the helper off and stops it.
Filename: "{app}\sideSTEP.exe"; Parameters: "--unregister"; Flags: runhidden; RunOnceId: "unregister"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\sideSTEPThumbs.dll"""; Flags: runhidden; RunOnceId: "unregdll"

[Code]
// Stop a running helper and release the shell DLL so the files can be replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  if FileExists(ExpandConstant('{app}\sideSTEP.exe')) then
    Exec(ExpandConstant('{app}\sideSTEP.exe'), '--helper-off', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im sideSTEP.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im prevhost.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Result := '';
end;
