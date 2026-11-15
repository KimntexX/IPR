; One verified standalone payload; no second dependency deployment.
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef InstallerManifest
  #error InstallerManifest is required
#endif
#ifndef ReleaseLabel
  #error ReleaseLabel is required
#endif
#ifndef NumericVersion
  #error NumericVersion is required
#endif
#ifndef ArtifactName
  #error ArtifactName is required
#endif

[Setup]
AppId=org.ipr.ImageProcessingRoom.Windows.x64
AppName=IPR - Image Processing Room
AppVersion={#ReleaseLabel}
VersionInfoVersion={#NumericVersion}
AppPublisher=IPR contributors
AppPublisherURL=https://github.com/KimntexX/IPR
DefaultDirName={localappdata}\Programs\IPR
DefaultGroupName=IPR
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputBaseFilename={#ArtifactName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\appIPR.exe
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
UninstallLogMode=append
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "bin\config\ipr-ui.ini,package-manifest.json"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#InstallerManifest}"; DestDir: "{app}"; DestName: "package-manifest.json"; Flags: ignoreversion

[Icons]
Name: "{group}\IPR"; Filename: "{app}\bin\appIPR.exe"; WorkingDir: "{app}\bin"
Name: "{autodesktop}\IPR"; Filename: "{app}\bin\appIPR.exe"; WorkingDir: "{app}\bin"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\appIPR.exe"; Description: "{cm:LaunchProgram,IPR}"; Flags: nowait postinstall skipifsilent

; No deletion outside installed files: personal profiles, settings and photographs
; are not installer-owned and must survive upgrade and uninstall.
