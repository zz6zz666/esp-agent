#define MyAppName "Crush Claw"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "Crush Claw"
#define MyAppURL "https://github.com/zz6zz666/crush-claw"
#define MyAppExeName "crush-claw.exe"
#define MyAgentExeName "esp-claw-desktop.exe"

; Architecture-specific defines — override via ISCC /DARCH=amd64|x86|arm64
#ifndef ARCH
# define ARCH "amd64"
#endif
#if ARCH == "amd64"
# define MARCH "amd64"
# define SETUP_ARCH "x64compatible"
# define BUILD_DIR "..\build"
# define MINGW_PREFIX "C:\msys64\mingw64"
#elif ARCH == "x86"
# define MARCH "x86"
# define SETUP_ARCH "x86"
# define BUILD_DIR "..\build-x86"
# define MINGW_PREFIX "C:\msys64\mingw32"
#elif ARCH == "arm64"
# define MARCH "arm64"
# define SETUP_ARCH "arm64"
# define BUILD_DIR "..\build-arm64"
# define MINGW_PREFIX "C:\msys64\clangarm64"
#else
# error Unsupported ARCH (use amd64, x86, or arm64)
#endif

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=.
OutputBaseFilename=crush-claw-setup-{#MyAppVersion}-{#MARCH}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed={#SETUP_ARCH}
ArchitecturesInstallIn64BitMode={#SETUP_ARCH}
PrivilegesRequired=admin
CloseApplications=no
UsePreviousTasks=no
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile={#SourcePath}\assets\lobster.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
Name: "path"; Description: "Add to &PATH environment variable (run crush-claw from any terminal)"; GroupDescription: "Environment:"; Flags: checkedonce
Name: "autostart"; Description: "Auto-start on &login"; GroupDescription: "Startup:"; Flags: checkedonce
Name: "autoupdate"; Description: "Check for &updates on startup"; GroupDescription: "Updates:"; Flags: checkedonce

[Files]
; Main binaries
Source: "{#BUILD_DIR}\{#MyAgentExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BUILD_DIR}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; SDL2 + SDL2_ttf
Source: "{#MINGW_PREFIX}\bin\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\SDL2_ttf.dll"; DestDir: "{app}"; Flags: ignoreversion

; Fonts — bundled NotoColorEmoji.ttf replaces Windows Segoe UI Emoji
Source: "..\fonts\NotoColorEmoji.ttf"; DestDir: "{app}\fonts"; Flags: ignoreversion
Source: "..\fonts\DejaVuSans.ttf"; DestDir: "{app}\fonts"; Flags: ignoreversion
Source: "..\fonts\wqy-zenhei.ttc"; DestDir: "{app}\fonts"; Flags: ignoreversion

; Lua
Source: "{#MINGW_PREFIX}\bin\lua54.dll"; DestDir: "{app}"; Flags: ignoreversion

; libpng
Source: "{#MINGW_PREFIX}\bin\libpng16-16.dll"; DestDir: "{app}"; Flags: ignoreversion

; libcurl + transitive deps
Source: "{#MINGW_PREFIX}\bin\libcurl-4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libidn2-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libssh2-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libnghttp2-14.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libzstd.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libbrotlidec.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libbrotlicommon.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libunistring-5.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libpsl-5.dll"; DestDir: "{app}"; Flags: ignoreversion

; OpenSSL
#if ARCH == "amd64"
Source: "{#MINGW_PREFIX}\bin\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libssl-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
#elif ARCH == "arm64"
Source: "{#MINGW_PREFIX}\bin\libcrypto-3-arm64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libssl-3-arm64.dll"; DestDir: "{app}"; Flags: ignoreversion
#else
Source: "{#MINGW_PREFIX}\bin\libcrypto-3.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libssl-3.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif

; SDL2_ttf transitive deps: freetype, harfbuzz
Source: "{#MINGW_PREFIX}\bin\libfreetype-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libharfbuzz-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libgraphite2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libglib-2.0-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libpcre2-8-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libiconv-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libintl-8.dll"; DestDir: "{app}"; Flags: ignoreversion

; Winpthreads (GCC runtime)
Source: "{#MINGW_PREFIX}\bin\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; GCC runtime
#if ARCH == "amd64"
Source: "{#MINGW_PREFIX}\bin\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
#elif ARCH == "x86"
Source: "{#MINGW_PREFIX}\bin\libgcc_s_dw2-1.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif

; GCC/libc++ runtime
#if ARCH != "arm64"
Source: "{#MINGW_PREFIX}\bin\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif

; zlib (transitive dep of many libraries)
Source: "{#MINGW_PREFIX}\bin\zlib1.dll"; DestDir: "{app}"; Flags: ignoreversion

; bzip2 (transitive dep of freetype)
Source: "{#MINGW_PREFIX}\bin\libbz2-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; libcurl HTTP/3 & QUIC transitive deps
Source: "{#MINGW_PREFIX}\bin\libnghttp3-9.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libngtcp2-16.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MINGW_PREFIX}\bin\libngtcp2_crypto_ossl-0.dll"; DestDir: "{app}"; Flags: ignoreversion

; Emote engine assets (boot animation)
Source: "..\sim_hal\assets\284_240\*"; DestDir: "{app}\assets\284_240"; Flags: ignoreversion recursesubdirs createallsubdirs

; Defaults (skills, docs, scripts) — seeded on first run to ~/.crush-claw
Source: "..\defaults\*"; DestDir: "{app}\defaults"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAgentExeName}"; Parameters: "--daemon"; Comment: "Start Crush Claw desktop simulator"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAgentExeName}"; Parameters: "--daemon"; Tasks: desktopicon

[Registry]
; Add to PATH (per-machine) — only if user selected the task
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Tasks: path; Check: NeedsAddPath(ExpandConstant('{app}'))

; Auto-start with Windows (per-user Run key)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "Crush Claw"; \
    ValueData: """{app}\{#MyAgentExeName}"" --daemon"; \
    Tasks: autostart

; Auto-check for updates (per-user)
Root: HKCU; Subkey: "Software\crush-claw"; \
    ValueType: dword; ValueName: "AutoUpdate"; ValueData: "1"; \
    Tasks: autoupdate
Root: HKCU; Subkey: "Software\crush-claw"; \
    ValueType: dword; ValueName: "AutoUpdate"; ValueData: "0"; \
    Tasks: not autoupdate

[Run]
; Launch agent as background daemon (no console, no log tail)
Filename: "{app}\{#MyAgentExeName}"; Parameters: "--daemon"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent runhidden

[UninstallRun]
; Terminate running agent before uninstall
Filename: "{app}\{#MyAppExeName}"; Parameters: "stop"; Flags: runhidden; RunOnceId: "stopAgent"

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';') = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  NeedsRestart := False;
  Result := '';

  { Graceful stop via CLI }
  if FileExists(ExpandConstant('{app}\{#MyAppExeName}')) then
  begin
    Exec(ExpandConstant('{app}\{#MyAppExeName}'), 'stop',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;

  { Graceful stop of the daemon directly (taskkill /im without /f sends WM_CLOSE) }
  Exec('taskkill', '/im esp-claw-desktop.exe /t',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  { Wait for processes to release file handles }
  Sleep(500);

  { Force kill if still hanging (should not normally happen) }
  Exec('taskkill', '/f /im esp-claw-desktop.exe /t',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill', '/f /im crush-claw.exe /t',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;
