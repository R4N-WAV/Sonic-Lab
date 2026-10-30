; ATELIER by R4N - Windows installer (Inno Setup 6)
; Built by CI:  iscc installer\atelier.iss /DAppVersion=x.y.z /DBuildInfo="..." /DVst3Dir=path\to\ATELIER.vst3

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildInfo
  #define BuildInfo "local build"
#endif
#ifndef Vst3Dir
  #define Vst3Dir "..\build\plugin\Atelier_artefacts\Release\VST3\ATELIER.vst3"
#endif

[Setup]
AppId={{6E2C1B8A-4F7D-4C1E-9A3B-52A7F0D1A4E1}
AppName=ATELIER
AppVersion={#AppVersion}
AppVerName=ATELIER by R4N {#AppVersion}
AppPublisher=R4N
AppComments=Understand sound. Shape character. ({#BuildInfo})
VersionInfoVersion={#AppVersion}
VersionInfoProductName=ATELIER by R4N
VersionInfoDescription=ATELIER by R4N VST3 installer
; The "application directory" is the VST3 folder the bundle goes into.
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=no
DisableProgramGroupPage=yes
AppendDefaultDirName=no
UsePreviousAppDir=yes
; Keep the uninstaller out of the VST3 folder so hosts never scan it.
UninstallFilesDir={commonpf64}\R4N\ATELIER
UninstallDisplayName=ATELIER by R4N {#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
OutputDir=output
OutputBaseFilename=ATELIER_by_R4N_Setup_{#AppVersion}
Compression=lzma2
SolidCompression=yes
InfoBeforeFile=welcome.txt
InfoAfterFile=after_install.txt
SetupLogging=yes

[Messages]
WelcomeLabel1=Welcome to ATELIER by R4N
WelcomeLabel2=This will install ATELIER {#AppVersion}, a VST3 audio engineering and DSP development environment.%n%nUnderstand sound. Shape character.
SelectDirLabel3=The ATELIER.vst3 bundle will be installed into the folder below. The standard VST3 folder is recommended, because every DAW scans it automatically.

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{app}\ATELIER.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "after_install.txt"; DestDir: "{commonpf64}\R4N\ATELIER"; DestName: "ATELIER - Setup and Troubleshooting.txt"; Flags: ignoreversion
Source: "..\docs\DAW_ROUTING.md"; DestDir: "{commonpf64}\R4N\ATELIER"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\ATELIER.vst3"

[Code]
function BinaryPath(): String;
begin
  Result := ExpandConstant('{app}\ATELIER.vst3\Contents\x86_64-win\ATELIER.vst3');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not FileExists(BinaryPath()) then
      MsgBox('Installation check FAILED: ' + BinaryPath() + ' was not found.' + #13#10 +
             'Please re-run the installer as administrator, or choose a folder you can write to.', mbError, MB_OK)
    else
      Log('Installation check OK: ' + BinaryPath());
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and (CompareText(WizardDirValue(), ExpandConstant('{commoncf64}\VST3')) <> 0) then
    Result := MsgBox('You chose a custom folder:' + #13#10 + WizardDirValue() + #13#10#13#10 +
                     'Your DAW must be told to scan this folder, or ATELIER will not appear. Continue?',
                     mbConfirmation, MB_YESNO) = IDYES;
end;
