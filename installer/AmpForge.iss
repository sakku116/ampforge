; Amp Forge — Inno Setup installer script
; Requires: Inno Setup 6  https://jrsoftware.org/isinfo.php
;
; Build:
;   ISCC.exe installer\AmpForge.iss
;
; Optional: place vc_redist.x64.exe next to this file to bundle it.
;   Download: https://aka.ms/vs/17/release/vc_redist.x64.exe

#define MyAppName      "Amp Forge"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#define MyAppPublisher "Amp Forge"
#define MyAppExeName   "Amp Forge.exe"
#define BuildDir       "..\build\AmpForge_artefacts\Release"
#define OutputDir      "..\dist"

[Setup]
AppId={{F2B4D8A1-7C3E-4F5A-9B2D-8E6F1A4C5D7B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Amp Forge
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir={#OutputDir}
OutputBaseFilename=AmpForge-Setup-{#MyAppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main binaries
Source: "{#BuildDir}\Amp Forge.exe";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\AmpForgeScanWorker.exe"; DestDir: "{app}"; Flags: ignoreversion

; VC++ 2015-2022 x64 redistributable (optional).
; Place vc_redist.x64.exe next to this script and it will be bundled and
; silently installed only when the runtime is missing on the target machine.
Source: "vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist; Check: NeedsVCRedist

[Icons]
Name: "{group}\{#MyAppName}";                       Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";                 Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/passive /norestart"; StatusMsg: "Installing Visual C++ Redistributable..."; Check: NeedsVCRedistFile; Flags: waitprogress

[Code]

function VCRuntimeInstalled: Boolean;
var
  Installed: Cardinal;
begin
  Result :=
    RegQueryDWordValue(HKLM,
      'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
      'Installed', Installed) and (Installed = 1);
  if not Result then
    Result := FileExists(ExpandConstant('{sys}\vcruntime140.dll'));
end;

function NeedsVCRedist: Boolean;
begin
  Result := not VCRuntimeInstalled;
end;

function NeedsVCRedistFile: Boolean;
begin
  Result := NeedsVCRedist and
            FileExists(ExpandConstant('{tmp}\vc_redist.x64.exe'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssInstall) and NeedsVCRedist and
     not FileExists(ExpandConstant('{tmp}\vc_redist.x64.exe')) then
  begin
    MsgBox(
      'The Visual C++ 2015-2022 Redistributable (x64) was not found on this machine.' + #13#10 +
      'Amp Forge requires it to run.' + #13#10#13#10 +
      'Please download and install it from Microsoft, then re-run Amp Forge:' + #13#10 +
      'https://aka.ms/vs/17/release/vc_redist.x64.exe',
      mbInformation, MB_OK);
  end;
end;
