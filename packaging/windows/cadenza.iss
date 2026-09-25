; Inno Setup script for the Windows build of Cadenza.
;
; It turns the folder build.ps1 gathered -- cadenza.exe, the Qt runtime and
; QML tree, libmpv-2.dll and libtag-2.dll with the rest of the MinGW runtime,
; and the helper tools in tools\ -- into the one file a user downloads and
; double-clicks: dist\cadenza-<version>-setup.exe.
;
; The translations are not part of that folder: they are inside the binary,
; which is where the app reads them from when there is no `locales\` beside
; the executable to edit them in.
;
; build.ps1 runs it as
;
;   ISCC.exe /DAppVersion=1.0.0 /DSourceDir=<repo>\dist\bin /DOutputDir=<repo>\dist cadenza.iss
;
; and the defaults below compile it by hand after a build:
;
;   ISCC.exe packaging\windows\cadenza.iss
;
; Needs Inno Setup 6.3 or newer, for the x64compatible architecture names.
; https://jrsoftware.org/isdl.php

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\bin"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
; Never change this id: it is how Windows tells one Cadenza from the next, and
; a different one would install a second copy beside the first.
AppId={{07C25C59-CF25-426F-9D0B-B3D54C613C34}
AppName=Cadenza
AppVersion={#AppVersion}
AppPublisher=zxuru
AppPublisherURL=https://github.com/zxuru/cadenza
DefaultDirName={autopf}\Cadenza
DefaultGroupName=Cadenza
UninstallDisplayIcon={app}\cadenza.exe
OutputDir={#OutputDir}
OutputBaseFilename=cadenza-{#AppVersion}-setup
LicenseFile=..\..\LICENSE
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Nothing here is a driver or a service: the whole application is one folder,
; so a per-user install is the default -- no administrator prompt -- and the
; dialog the wizard would show offers the machine-wide one for anyone who
; wants it.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DisableProgramGroupPage=yes
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
#if FileExists(AddBackslash(CompilerPath) + "Languages\Spanish.isl")
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; What build.ps1 gathered, subfolders and all: the Qt DLLs, platforms\,
; sqldrivers\, imageformats\, the qml\ tree and tools\.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Cadenza"; Filename: "{app}\cadenza.exe"
Name: "{autodesktop}\Cadenza"; Filename: "{app}\cadenza.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\cadenza.exe"; Description: "{cm:LaunchProgram,Cadenza}"; Flags: nowait postinstall skipifsilent
