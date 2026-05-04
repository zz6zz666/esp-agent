/*
 * tray_icon.c — Windows system tray icon for esp-agent desktop simulator
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

/* ---- Forward decl ---- */
static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

/* ---- Public API ---- */

bool tray_icon_init(void)
{
    if (s_initialized) return true;

    /* Register a window class for the hidden tray message window */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = tray_wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "EspAgentTrayClass";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    /* use a small system icon as fallback; the NOTIFYICONDATA also sets an icon */
    wc.hIcon         = LoadIcon(NULL, IDI_INFORMATION);

    if (!RegisterClassA(&wc)) return false;

    s_tray_hwnd = CreateWindowExA(
        0, "EspAgentTrayClass", "esp-agent Tray",
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
    s_nid.hIcon            = LoadIcon(NULL, IDI_INFORMATION);
    strncpy(s_nid.szTip, "esp-agent Desktop Simulator", sizeof(s_nid.szTip) - 1);

    Shell_NotifyIconA(NIM_ADD, &s_nid);

    /* Create popup menu */
    s_menu = CreatePopupMenu();
    AppendMenuA(s_menu, MF_STRING, IDM_SHOW, "Show Window");
    AppendMenuA(s_menu, MF_STRING, IDM_HIDE, "Hide to Tray");
    AppendMenuA(s_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(s_menu, MF_STRING, IDM_EXIT, "Exit");

    s_initialized = true;
    return true;
}

void tray_icon_set_sdl_window(void *hwnd)
{
    s_sdl_hwnd = (HWND)hwnd;
    /* Remove from taskbar — tray icon is the primary UI surface */
    LONG_PTR ex_style = GetWindowLongPtrA(s_sdl_hwnd, GWL_EXSTYLE);
    SetWindowLongPtrA(s_sdl_hwnd, GWL_EXSTYLE, ex_style | WS_EX_TOOLWINDOW);
    /* Re-show to apply the style change */
    ShowWindow(s_sdl_hwnd, SW_HIDE);
    ShowWindow(s_sdl_hwnd, SW_SHOW);
    SetForegroundWindow(s_sdl_hwnd);
}

void tray_icon_show_window(void)
{
    if (s_sdl_hwnd) {
        ShowWindow(s_sdl_hwnd, SW_SHOW);
        SetForegroundWindow(s_sdl_hwnd);
    }
    s_window_visible = true;
}

void tray_icon_hide_window(void)
{
    if (s_sdl_hwnd) {
        ShowWindow(s_sdl_hwnd, SW_HIDE);
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
        case IDM_EXIT:
            s_quit_requested = true;
            /* Post WM_QUIT so SDL's main loop sees it */
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
