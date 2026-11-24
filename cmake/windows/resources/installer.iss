; Windows installer for obs-bililive, compiled by .github/scripts/Package-Windows.ps1:
;   ISCC.exe /DMyVersion=<version> /DMySource=<release/<config>/obs-bililive> /DMyOutputDir=<dir> /DMyOutputName=<name> installer.iss
; ChineseSimplified.isl is the user-contributed translation from jrsoftware/issrc (is-6_7_1, Files/Languages/Unofficial),
; saved with a UTF-8 BOM so ISCC reads it as Unicode.

#ifndef MyVersion
  #error MyVersion is required
#endif

[Setup]
; Fixed for the life of the project: Windows matches upgrades and the uninstaller by it.
AppId={{6F0D2B6C-4F1E-4C6B-9C47-0B1C1B2E7A31}
AppName=BiliLive for OBS
AppVersion={#MyVersion}
AppVerName=BiliLive for OBS {#MyVersion}
AppPublisher=VOfficial404
AppPublisherURL=https://github.com/VOfficial404/obs-bililive
AppSupportURL=https://github.com/VOfficial404/obs-bililive/issues
AppUpdatesURL=https://github.com/VOfficial404/obs-bililive/releases/latest
; Where OBS 31+ (website and Steam builds) look for third-party plugins. Portable OBS users pick
; <OBS folder>\plugins\obs-bililive on the directory page instead.
DefaultDirName={commonappdata}\obs-studio\plugins\obs-bililive
UsePreviousAppDir=yes
DisableDirPage=auto
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; OBS keeps the plugin DLL open; Restart Manager finds obs64.exe through it and offers to close it.
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=BiliLive for OBS
UninstallFilesDir={app}\uninstall
OutputDir={#MyOutputDir}
OutputBaseFilename={#MyOutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
chinesesimplified.FinishedLabel=BiliLive for OBS 已安装。启动（或重启）OBS 后，在顶栏的「B站直播」菜单里打开面板。
english.FinishedLabel=BiliLive for OBS is installed. Start (or restart) OBS and open the panel from the Bilibili Live menu in the menu bar.

[Files]
Source: "{#MySource}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
; ProgramData lets every user create files in new folders, and OBS loads DLLs from this one for whoever starts it.
; An earlier install or another user may own files here: hand everything to Administrators and reset child ACLs to
; inherit-only, so the grant below reaches every file rather than just the folder.
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /setowner *S-1-5-32-544 /T /C"; Flags: runhidden waituntilterminated
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /reset /T /C"; Flags: runhidden waituntilterminated
; Drop the inherited ACL: only Administrators and SYSTEM can write, Users can only read and run.
Filename: "{sys}\icacls.exe"; Parameters: """{app}"" /inheritance:r /grant:r *S-1-5-32-544:(OI)(CI)F /grant:r *S-1-5-18:(OI)(CI)F /grant:r *S-1-5-32-545:(OI)(CI)RX"; Flags: runhidden waituntilterminated
