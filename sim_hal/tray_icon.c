/*
 * tray_icon.c — Windows system tray icon for Crush Claw desktop simulator
 *
 * Creates a hidden Win32 window to receive tray notification messages.
 * Provides a right-click popup menu (Show/Hide, Exit).
 * Left-click toggles the SDL2 display window.
 */
#include "tray_icon.h"

#if defined(_WIN32)

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

/* ---- Internal state ---- */
static HWND   s_tray_hwnd = NULL;      /* hidden message window */
static HWND   s_sdl_hwnd = NULL;       /* SDL2 display window handle */
static NOTIFYICONDATAA s_nid = {0};
static bool   s_window_visible = true;
static bool   s_quit_requested = false;
static bool   s_initialized = false;
static HMENU  s_menu = NULL;

#define WM_TRAYICON  (WM_APP + 1)
#define TRAY_UID     1001
#define IDM_SHOW     2001
#define IDM_HIDE     2002
#define IDM_EXIT     2003
#define IDM_AUTOSTART   2004
#define IDM_ALWAYS_HIDE 2005
#define IDM_AUTO_UPDATE 2006
#define IDI_CLAW        1

#define REG_AUTOSTART_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define REG_AUTOSTART_VAL "Crush Claw"
#define REG_APP_KEY       "Software\\crush-claw"
#define REG_ALWAYS_HIDE   "AlwaysHide"
#define REG_AUTO_UPDATE   "AutoUpdate"

#define GITHUB_API_HOST  L"api.github.com"
#define GITHUB_API_PATH  L"/repos/zz6zz666/crush-claw/releases/latest"

/* ---- Forward decls ---- */
static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static bool tray_autostart_is_enabled(void);
static void tray_autostart_set_enabled(bool enable);

extern bool display_hal_is_lua_mode(void);

/* ---- Public API ---- */

bool tray_icon_init(void)
{
    if (s_initialized) return true;

    /* Register a window class for the hidden tray message window */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = tray_wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "CrushClawTrayClass";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    /* use a small system icon as fallback; the NOTIFYICONDATA also sets an icon */
    wc.hIcon         = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_CLAW));

    if (!RegisterClassA(&wc)) return false;

    s_tray_hwnd = CreateWindowExA(
        0, "CrushClawTrayClass", "crush-claw Tray",
        WS_OVERLAPPED, 0, 0, 1, 1,
        NULL, NULL, wc.hInstance, NULL);

    if (!s_tray_hwnd) return false;

    /* Create tray icon */
    memset(&s_nid, 0, sizeof(s_nid));
    s_nid.cbSize           = sizeof(s_nid);
    s_nid.hWnd             = s_tray_hwnd;
    s_nid.uID              = TRAY_UID;
    s_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_TRAYICON;
    s_nid.hIcon            = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDI_CLAW));
    strncpy(s_nid.szTip, "Crush Claw Desktop Simulator", sizeof(s_nid.szTip) - 1);

    Shell_NotifyIconA(NIM_ADD, &s_nid);

    /* Create popup menu */
    s_menu = CreatePopupMenu();
    AppendMenuA(s_menu, MF_STRING, IDM_SHOW, "Show Window");
    AppendMenuA(s_menu, MF_STRING, IDM_HIDE, "Hide to Tray");
    AppendMenuA(s_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(s_menu, MF_STRING | (tray_always_hide_is_enabled() ? MF_CHECKED : 0),
                IDM_ALWAYS_HIDE, "Always Hide Windows");
    AppendMenuA(s_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(s_menu, MF_STRING | (tray_autostart_is_enabled() ? MF_CHECKED : 0),
                IDM_AUTOSTART, "Auto-start on Login");
    AppendMenuA(s_menu, MF_STRING | (tray_auto_update_is_enabled() ? MF_CHECKED : 0),
                IDM_AUTO_UPDATE, "Check for Updates on Startup");
    AppendMenuA(s_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(s_menu, MF_STRING, IDM_EXIT, "Exit");

    s_initialized = true;
    return true;
}

/* ---- Autostart registry helpers ---- */

static bool tray_autostart_is_enabled(void)
{
    HKEY hKey;
    char val[1024];
    DWORD size = sizeof(val);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_AUTOSTART_KEY,
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    LONG rc = RegQueryValueExA(hKey, REG_AUTOSTART_VAL, NULL, NULL,
                                (LPBYTE)val, &size);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

static void tray_autostart_set_enabled(bool enable)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_AUTOSTART_KEY,
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return;
    if (enable) {
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_path, sizeof(exe_path))) {
            char *slash = strrchr(exe_path, '\\');
            if (slash) {
                snprintf(slash + 1, sizeof(exe_path) - (size_t)(slash + 1 - exe_path),
                         "%s", "esp-claw-desktop.exe");
            }
            char val[MAX_PATH + 16];
            snprintf(val, sizeof(val), "\"%s\" --daemon", exe_path);
            RegSetValueExA(hKey, REG_AUTOSTART_VAL, 0, REG_SZ,
                           (const BYTE *)val, (DWORD)(strlen(val) + 1));
        }
    } else {
        RegDeleteValueA(hKey, REG_AUTOSTART_VAL);
    }
    RegCloseKey(hKey);
}

/* ---- Always-hide registry helpers ---- */

bool tray_always_hide_is_enabled(void)
{
    HKEY hKey;
    DWORD val = 0, size = sizeof(val);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_APP_KEY,
                        0, NULL, 0, KEY_READ, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return false;
    LONG rc = RegQueryValueExA(hKey, REG_ALWAYS_HIDE, NULL, NULL,
                                (LPBYTE)&val, &size);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS && val != 0;
}

void tray_always_hide_toggle(void)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_APP_KEY,
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return;
    bool cur = tray_always_hide_is_enabled();
    DWORD val = cur ? 0 : 1;
    RegSetValueExA(hKey, REG_ALWAYS_HIDE, 0, REG_DWORD,
                   (const BYTE *)&val, sizeof(val));
    RegCloseKey(hKey);
}

/* ---- Auto-update registry helpers ---- */

bool tray_auto_update_is_enabled(void)
{
    HKEY hKey;
    DWORD val = 0, size = sizeof(val);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_APP_KEY,
                        0, NULL, 0, KEY_READ, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return true;
    LONG rc = RegQueryValueExA(hKey, REG_AUTO_UPDATE, NULL, NULL,
                                (LPBYTE)&val, &size);
    RegCloseKey(hKey);
    return rc != ERROR_SUCCESS || val != 0;
}

void tray_auto_update_set_enabled(bool enable)
{
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_APP_KEY,
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL)
        != ERROR_SUCCESS)
        return;
    DWORD val = enable ? 1 : 0;
    RegSetValueExA(hKey, REG_AUTO_UPDATE, 0, REG_DWORD,
                   (const BYTE *)&val, sizeof(val));
    RegCloseKey(hKey);
}

/* ---- Version comparison ---- */

static int compare_versions(const char *v1, const char *v2)
{
    if (*v1 == 'v') v1++;
    if (*v2 == 'v') v2++;
    while (*v1 && *v2) {
        int n1 = 0, n2 = 0;
        while (*v1 && *v1 != '.') { n1 = n1 * 10 + (*v1 - '0'); v1++; }
        while (*v2 && *v2 != '.') { n2 = n2 * 10 + (*v2 - '0'); v2++; }
        if (n1 != n2) return n1 < n2 ? -1 : 1;
        if (*v1) v1++;
        if (*v2) v2++;
    }
    return (*v1 || *v2) ? (*v2 ? -1 : 1) : 0;
}

/* ---- GitHub release check ---- */

void tray_icon_perform_update_check(const char *current_version)
{
    if (!tray_auto_update_is_enabled()) return;

    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char latest_ver[64] = {0};

    hSession = WinHttpOpen(L"crush-claw-updater/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           NULL, NULL, 0);
    if (!hSession) goto cleanup;

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    hConnect = WinHttpConnect(hSession, GITHUB_API_HOST,
                              INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) goto cleanup;

    hRequest = WinHttpOpenRequest(hConnect, L"GET", GITHUB_API_PATH,
                                  NULL, NULL, NULL,
                                  WINHTTP_FLAG_SECURE);
    if (!hRequest) goto cleanup;

    LPCWSTR accept = L"Accept: application/vnd.github.v3+json\r\n";
    WinHttpAddRequestHeaders(hRequest, accept, (ULONG)wcslen(accept),
                             WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0))
        goto cleanup;
    if (!WinHttpReceiveResponse(hRequest, NULL))
        goto cleanup;

    {
        char buf[4096];
        DWORD size = 0, downloaded = 0;
        while (WinHttpReadData(hRequest, buf + downloaded,
                               (DWORD)(sizeof(buf) - downloaded - 1),
                               &size) && size > 0)
        {
            downloaded += size;
            if (downloaded >= sizeof(buf) - 1) break;
        }
        buf[downloaded] = '\0';

        const char *tag = strstr(buf, "\"tag_name\"");
        if (tag) {
            tag = strchr(tag, ':');
            if (tag) {
                tag++;
                while (*tag && (*tag == ' ' || *tag == '"')) tag++;
                const char *end = strchr(tag, '"');
                if (end) {
                    size_t len = (size_t)(end - tag);
                    if (len < sizeof(latest_ver) - 1) {
                        memcpy(latest_ver, tag, len);
                        latest_ver[len] = '\0';
                    }
                }
            }
        }
    }

    if (latest_ver[0] && compare_versions(latest_ver, current_version) > 0) {
        char msg[1024];
        snprintf(msg, sizeof(msg),
                 "A new version of Crush Claw is available!\n\n"
                 "Current version: %s\n"
                 "Latest version:  %s\n\n"
                 "Would you like to visit the releases page to download the update?",
                 current_version, latest_ver);
        int ret = MessageBoxA(NULL, msg, "Update Available",
                              MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
        if (ret == IDYES) {
            ShellExecuteA(NULL, "open",
                "https://github.com/zz6zz666/crush-claw/releases/latest",
                NULL, NULL, SW_SHOWNORMAL);
        }
    }

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
}

void tray_icon_set_sdl_window(void *hwnd)
{
    if ((HWND)hwnd == s_sdl_hwnd) return;
    s_sdl_hwnd = (HWND)hwnd;

    LONG_PTR ex_style = GetWindowLongPtrA(s_sdl_hwnd, GWL_EXSTYLE);

    if (display_hal_is_lua_mode()) {
        ex_style |= WS_EX_APPWINDOW;
        ex_style &= ~WS_EX_TOOLWINDOW;
    } else {
        ex_style |= WS_EX_TOOLWINDOW;
        ex_style &= ~WS_EX_APPWINDOW;
    }
    SetWindowLongPtrA(s_sdl_hwnd, GWL_EXSTYLE, ex_style);

    ShowWindow(s_sdl_hwnd, SW_HIDE);
    ShowWindow(s_sdl_hwnd, SW_SHOWNOACTIVATE);
}

void tray_icon_flash_start(void)
{
    if (!s_sdl_hwnd || !s_nid.hWnd) return;
    FlashWindow(s_sdl_hwnd, TRUE);
}

void tray_icon_flash_stop(void)
{
    if (!s_sdl_hwnd) return;
    FLASHWINFO fi = {sizeof(FLASHWINFO), s_sdl_hwnd, FLASHW_STOP, 0, 0};
    FlashWindowEx(&fi);
}

void tray_icon_show_and_flash(void)
{
    if (!s_sdl_hwnd) return;
    FLASHWINFO fi = {sizeof(FLASHWINFO), s_sdl_hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 500};
    FlashWindowEx(&fi);
}

void tray_icon_show_window(void)
{
    if (s_sdl_hwnd) {
        if (display_hal_is_lua_mode()) {
            ShowWindow(s_sdl_hwnd, SW_RESTORE);
            SetForegroundWindow(s_sdl_hwnd);
            BringWindowToTop(s_sdl_hwnd);
        } else {
            ShowWindow(s_sdl_hwnd, SW_SHOWNOACTIVATE);
            LONG exstyle = GetWindowLong(s_sdl_hwnd, GWL_EXSTYLE);
            SetWindowLong(s_sdl_hwnd, GWL_EXSTYLE, exstyle | WS_EX_TOPMOST);
        }
    }
    s_window_visible = true;
}

void tray_icon_hide_window(void)
{
    if (s_sdl_hwnd) {
        if (display_hal_is_lua_mode()) {
            ShowWindow(s_sdl_hwnd, SW_MINIMIZE);
        } else {
            LONG exstyle = GetWindowLong(s_sdl_hwnd, GWL_EXSTYLE);
            if (exstyle & WS_EX_TOPMOST)
                SetWindowLong(s_sdl_hwnd, GWL_EXSTYLE, exstyle & ~WS_EX_TOPMOST);
            ShowWindow(s_sdl_hwnd, SW_HIDE);
        }
    }
    s_window_visible = false;
}

bool tray_icon_is_window_visible(void)
{
    return s_window_visible;
}

bool tray_icon_quit_requested(void)
{
    return s_quit_requested;
}

void tray_icon_pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, s_tray_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    /* Also pump messages for the tray-sent callbacks on the SDL window */
    while (PeekMessageA(&msg, s_sdl_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void tray_icon_cleanup(void)
{
    if (s_initialized) {
        Shell_NotifyIconA(NIM_DELETE, &s_nid);
        if (s_menu) DestroyMenu(s_menu);
        if (s_tray_hwnd) DestroyWindow(s_tray_hwnd);
        s_tray_hwnd = NULL;
        s_initialized = false;
    }
}

/* ---- Window procedure ---- */
static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            /* Update checkmarks before showing menu */
            CheckMenuItem(s_menu, IDM_AUTOSTART,
                tray_autostart_is_enabled() ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(s_menu, IDM_ALWAYS_HIDE,
                tray_always_hide_is_enabled() ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(s_menu, IDM_AUTO_UPDATE,
                tray_auto_update_is_enabled() ? MF_CHECKED : MF_UNCHECKED);
            /* Right-click: show popup menu */
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(s_menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                          pt.x, pt.y, 0, hwnd, NULL);
            PostMessageA(hwnd, WM_NULL, 0, 0);
        } else if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            /* Left-click: toggle window visibility */
            if (tray_icon_is_window_visible())
                tray_icon_hide_window();
            else
                tray_icon_show_window();
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SHOW:
            tray_icon_show_window();
            break;
        case IDM_HIDE:
            tray_icon_hide_window();
            break;
        case IDM_ALWAYS_HIDE:
            tray_always_hide_toggle();
            break;
        case IDM_AUTOSTART: {
            bool cur = tray_autostart_is_enabled();
            tray_autostart_set_enabled(!cur);
            break;
        }
        case IDM_AUTO_UPDATE: {
            bool cur = tray_auto_update_is_enabled();
            tray_auto_update_set_enabled(!cur);
            break;
        }
        case IDM_EXIT:
            s_quit_requested = true;
            break;
        }
        break;

    case WM_CREATE:
        /* Re-create tray icon if explorer restarted */
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &s_nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
    return 0;
}

#endif /* _WIN32 */
