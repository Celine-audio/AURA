; The Windows installer, built by Inno Setup in .github/workflows/windows.yml:
;
;     iscc packaging\installer.iss
;
; after the build, with PROJECT_NAME, PRODUCT_NAME and COMPANY_NAME in the environment (CI
; reads them from the .env CMake writes). Paths here are relative to this file, so the build
; is expected in ..\Builds, as CI does it. Céline's, carried over with AURA's formats.
;
; Every format is a component, all ticked, so the default is "install everything" and a
; custom install is one untick away. The plugins go where hosts look for them system-wide.
; There is no standalone app to install, and the AAX is left out: it needs Avid's PACE
; signature to load.
;
; The artwork in windows\ is drawn by make-artwork.swift; the license page is the AGPL
; itself, which is the licence these binaries are under.

#define Version Trim(FileRead(FileOpen("..\VERSION")))
#define ProjectName GetEnv('PROJECT_NAME')
#define ProductName GetEnv('PRODUCT_NAME')
#define Publisher GetEnv('COMPANY_NAME')
#define Year GetDateTimeString("yyyy", "", "")
#define Artefacts "..\Builds\" + ProjectName + "_artefacts\Release"

[Setup]
; Never change the AppId: it is how Windows knows a new version replaces an old one rather
; than installing beside it. AURA's own -- sharing Céline's would make each plugin's
; installer think it was upgrading the other.
AppId={{7FC6AA82-3169-4D92-A4CA-E63BDEAB358A}
AppName={#ProductName}
AppVersion={#Version}
AppVerName={#ProductName} {#Version}
AppPublisher={#Publisher}
AppPublisherURL=https://github.com/Celine-audio/Aura
AppSupportURL=https://github.com/Celine-audio/Aura/issues
AppUpdatesURL=https://github.com/Celine-audio/Aura/releases
AppCopyright=Copyright (C) {#Year} {#Publisher}

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The plugin folders are under Common Files, which every user's host reads.
PrivilegesRequired=admin

; No app to put here: this is where the uninstaller lives.
DefaultDirName={commonpf64}\{#Publisher}\{#ProductName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName={#ProductName}

LicenseFile=..\COPYING

WizardStyle=modern
WizardImageFile=windows\wizard-100.bmp,windows\wizard-150.bmp,windows\wizard-200.bmp
WizardSmallImageFile=windows\wizard-small-100.bmp,windows\wizard-small-150.bmp,windows\wizard-small-200.bmp
WizardImageStretch=no
SetupIconFile=windows\icon.ico
UninstallDisplayIcon={uninstallexe}
UninstallDisplayName={#ProductName}

OutputDir=..\Builds
OutputBaseFilename={#ProductName}-{#Version}-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom
Name: "lv2"; Description: "LV2 plugin"; Types: full custom

[Files]
; The linker leaves symbol and incremental-link files beside the plugins; none is any use
; to someone installing them.
Source: "{#Artefacts}\VST3\{#ProductName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#ProductName}.vst3"; Excludes: "*.ilk,*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#Artefacts}\CLAP\{#ProductName}.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#Artefacts}\LV2\{#ProductName}.lv2\*"; DestDir: "{commoncf64}\LV2\{#ProductName}.lv2"; Excludes: "*.ilk,*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: lv2

[Messages]
FinishedLabel=Setup has installed [name] on your computer.%n%nRestart your DAW and rescan its plugins if [name] does not show up.
