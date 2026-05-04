#define MyAppName "esp-agent"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "esp-agent"
#define MyAppURL "https://github.com/zz6zz666/esp-agent"
#define MyAppExeName "esp-agent.exe"
#define MyAgentExeName "esp-claw-desktop.exe"

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
OutputBaseFilename=esp-agent-setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=assets\lobster.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce
Name: "path"; Description: "Add to &PATH environment variable (run esp-agent from any terminal)"; GroupDescription: "Environment:"; Flags: checkedonce
Name: "autostart"; Description: "Start with Wi&ndows (auto-run on login)"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
; Main binaries
Source: "..\build\{#MyAgentExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; SDL2 + SDL2_ttf
Source: "C:\msys64\mingw64\bin\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\SDL2_ttf.dll"; DestDir: "{app}"; Flags: ignoreversion

; Lua
Source: "C:\msys64\mingw64\bin\lua54.dll"; DestDir: "{app}"; Flags: ignoreversion

; libpng
Source: "C:\msys64\mingw64\bin\libpng16-16.dll"; DestDir: "{app}"; Flags: ignoreversion

; libcurl + transitive deps
Source: "C:\msys64\mingw64\bin\libcurl-4.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libidn2-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libssh2-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libnghttp2-14.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libzstd.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libbrotlidec.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libbrotlicommon.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libunistring-5.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libpsl-5.dll"; DestDir: "{app}"; Flags: ignoreversion

; OpenSSL
Source: "C:\msys64\mingw64\bin\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libssl-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion

; SDL2_ttf transitive deps: freetype, harfbuzz
Source: "C:\msys64\mingw64\bin\libfreetype-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libharfbuzz-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libgraphite2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libglib-2.0-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libpcre2-8-0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libiconv-2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libintl-8.dll"; DestDir: "{app}"; Flags: ignoreversion

; Winpthreads (GCC runtime)
Source: "C:\msys64\mingw64\bin\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; GCC runtime
Source: "C:\msys64\mingw64\bin\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; zlib (transitive dep of many libraries)
Source: "C:\msys64\mingw64\bin\zlib1.dll"; DestDir: "{app}"; Flags: ignoreversion

; bzip2 (transitive dep of freetype)
Source: "C:\msys64\mingw64\bin\libbz2-1.dll"; DestDir: "{app}"; Flags: ignoreversion

; GCC C++ runtime (needed by some C libraries)
Source: "C:\msys64\mingw64\bin\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion

; libcurl HTTP/3 & QUIC transitive deps
Source: "C:\msys64\mingw64\bin\libnghttp3-9.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libngtcp2-16.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\msys64\mingw64\bin\libngtcp2_crypto_ossl-0.dll"; DestDir: "{app}"; Flags: ignoreversion

; Emote engine assets (boot animation)
Source: "..\sim_hal\assets\284_240\*"; DestDir: "{app}\assets\284_240"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName} Agent"; Filename: "{app}\{#MyAgentExeName}"; Parameters: "--foreground"; Comment: "Start esp-agent desktop simulator"
Name: "{group}\{#MyAppName} CLI"; Filename: "{cmd}"; Parameters: "/k ""{app}\{#MyAppExeName}"" help"; Comment: "Open a terminal with esp-agent commands"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAgentExeName}"; Parameters: "--foreground"; Tasks: desktopicon

[Registry]
; Add to PATH (per-machine) — only if user selected the task
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; \
    Tasks: path; Check: NeedsAddPath(ExpandConstant('{app}'))

; Auto-start with Windows (per-user Run key)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "esp-agent"; \
    ValueData: """{app}\{#MyAgentExeName}"" --daemon"; \
    Tasks: autostart

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
  if FileExists(ExpandConstant('{app}\{#MyAppExeName}')) then
  begin
    Exec(ExpandConstant('{app}\{#MyAppExeName}'), 'stop',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
