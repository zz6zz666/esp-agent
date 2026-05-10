/*
 * tray_icon_linux.c — Linux system tray icon using GTK3 + libayatana-appindicator
 *
 * Provides the same API as tray_icon.c (Windows) on Linux:
 *   - System tray icon with right-click popup menu
 *   - Left-click toggles SDL2 display window show/hide
 *   - XDG Autostart toggle (user login)
 *   - "Always Hide Windows" persistent setting  (→ config.json)
 *   - "Check for Updates on Startup" persistent setting (→ config.json)
 *   - GitHub release auto-update check via libcurl
 *
 * Dependencies: libgtk-3-dev, libayatana-appindicator3-dev, libcurl, cJSON
 *
 * Design note: GTK and SDL each open their own independent X11 connections.
 * Our private X11 connection (s_x11_dpy) is used ONLY for property/state
 * changes on the SDL window (XSendEvent, XChangeProperty, XMapWindow, etc.)
 * — we never call XSelectInput on it, so no events are stolen from SDL.
 */

#include "tray_icon.h"

#if !defined(_WIN32)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---- When GTK/AppIndicator are not available, provide empty stubs ---- */

#ifndef HAS_TRAY_LINUX_GTK

bool tray_icon_init(void) { return false; }
void tray_icon_set_sdl_window(void *xid) { (void)xid; }
void tray_icon_show_window(void) {}
void tray_icon_hide_window(void) {}
void tray_icon_show_and_flash(void) {}
bool tray_icon_is_window_visible(void) { return true; }
bool tray_icon_quit_requested(void) { return false; }
void tray_icon_pump(void) {}
void tray_icon_cleanup(void) {}
bool tray_always_hide_is_enabled(void) { return false; }
void tray_always_hide_toggle(void) {}
bool tray_auto_update_is_enabled(void) { return true; }
void tray_auto_update_set_enabled(bool enable) { (void)enable; }
void tray_icon_perform_update_check(const char *ver) { (void)ver; }

#else /* HAS_TRAY_LINUX_GTK — full implementation below */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

#include <curl/curl.h>

#include "cJSON.h"

/* ---- Internal state ---- */
static AppIndicator  *s_indicator        = NULL;
static GtkWidget     *s_menu             = NULL;
static GtkWidget     *s_item_show        = NULL;
static GtkWidget     *s_item_hide        = NULL;
static GtkWidget     *s_item_always_hide = NULL;
static GtkWidget     *s_item_autostart   = NULL;
static GtkWidget     *s_item_auto_update = NULL;
static Window         s_x11_window       = 0;
static Display       *s_x11_dpy          = NULL;
static bool           s_window_visible   = true;
static bool           s_quit_requested   = false;
static bool           s_initialized      = false;
static bool           s_gtk_ok           = false;

#define GITHUB_API_HOST  "api.github.com"
#define GITHUB_API_PATH  "/repos/zz6zz666/crush-claw/releases/latest"

extern bool display_hal_is_lua_mode(void);

/* ---- Config JSON helpers (shared with config.json) ---- */

static char g_config_path[512];

static const char *get_config_path(void)
{
    if (!g_config_path[0]) {
        const char *home = getenv("HOME");
        if (!home) home = ".";
        snprintf(g_config_path, sizeof(g_config_path),
                 "%s/.crush-claw/config.json", home);
    }
    return g_config_path;
}

static cJSON *read_config(void)
{
    FILE *fp = fopen(get_config_path(), "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, sz, fp);
    fclose(fp);
    if (n == 0) { free(buf); return NULL; }
    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static bool write_config_json(cJSON *root)
{
    char *js = cJSON_Print(root);
    if (!js) return false;
    /* Ensure parent dir exists */
    char path[512];
    snprintf(path, sizeof(path), "%s/.crush-claw", getenv("HOME") ? getenv("HOME") : ".");
    mkdir(path, 0755);
    FILE *fp = fopen(get_config_path(), "wb");
    if (fp) { fputs(js, fp); fclose(fp); }
    free(js);
    return fp != NULL;
}

static bool config_get_bool(const char *key, bool def)
{
    cJSON *root = read_config();
    if (!root) return def;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    bool ret = cJSON_IsTrue(item) ? true : (cJSON_IsFalse(item) ? false : def);
    cJSON_Delete(root);
    return ret;
}

static void config_set_bool(const char *key, bool val)
{
    cJSON *root = read_config();
    if (!root) root = cJSON_CreateObject();
    cJSON *existing = cJSON_GetObjectItemCaseSensitive(root, key);
    if (existing)
        cJSON_ReplaceItemInObjectCaseSensitive(root, key,
            cJSON_CreateBool(val ? 1 : 0));
    else
        cJSON_AddBoolToObject(root, key, val ? 1 : 0);
    write_config_json(root);
    cJSON_Delete(root);
}

/* ---- XDG Autostart (cross-desktop: GNOME, KDE, XFCE, etc.) ---- */

static char *get_autostart_path(void)
{
    const char *home = getenv("HOME");
    if (!home) return NULL;
    static char path[512];
    snprintf(path, sizeof(path), "%s/.config/autostart/crush-claw.desktop", home);
    return path;
}

static bool autostart_is_enabled(void)
{
    const char *p = get_autostart_path();
    if (!p) return false;
    return access(p, F_OK) == 0;
}

static void autostart_set_enabled(bool enable)
{
    const char *home = getenv("HOME");
    if (!home) return;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config/autostart", home);
    mkdir(dir, 0755);

    const char *path = get_autostart_path();

    if (!enable) {
        unlink(path);
        return;
    }

    /* Find the binary path */
    char exe_path[512] = "/usr/bin/esp-claw-desktop";
    if (access(exe_path, X_OK) != 0) {
        ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (n > 0) exe_path[n] = '\0';
    }

    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp,
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Crush Claw\n"
        "Comment=Desktop AI Agent Simulator\n"
        "Exec=%s --daemon\n"
        "Icon=lobster\n"
        "Terminal=false\n"
        "Categories=Utility;\n"
        "StartupNotify=false\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-KDE-autostart-after=panel\n",
        exe_path);
    fclose(fp);
    chmod(path, 0755);
}

/* ---- Always-hide (persistent) ---- */

bool tray_always_hide_is_enabled(void)
{
    return config_get_bool("always_hide_windows", false);
}

void tray_always_hide_toggle(void)
{
    bool cur = tray_always_hide_is_enabled();
    config_set_bool("always_hide_windows", !cur);
}

/* ---- Auto-update check (persistent) ---- */

bool tray_auto_update_is_enabled(void)
{
    return config_get_bool("auto_update_check", true);
}

void tray_auto_update_set_enabled(bool enable)
{
    config_set_bool("auto_update_check", enable);
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

/* ---- libcurl helpers ---- */

struct curl_buf { char *data; size_t len; size_t cap; };

static size_t curl_write_cb(void *ptr, size_t sz, size_t nmemb, void *user)
{
    struct curl_buf *b = (struct curl_buf *)user;
    size_t total = sz * nmemb;
    size_t need = b->len + total + 1;
    if (need > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        if (ncap < need) ncap = need;
        char *nd = realloc(b->data, ncap);
        if (!nd) return 0;
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

/* ---- GitHub update check ---- */

static void show_update_dialog(const char *current, const char *latest)
{
    GtkWidget *dlg = gtk_message_dialog_new(NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_YES_NO,
        "A new version of Crush Claw is available!\n\n"
        "Current version: %s\n"
        "Latest version:  %s\n\n"
        "Would you like to visit the releases page?",
        current, latest);
    gtk_window_set_title(GTK_WINDOW(dlg), "Update Available");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_YES) {
        const char *argv[3] = { "xdg-open",
            "https://github.com/zz6zz666/crush-claw/releases/latest", NULL };
        g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, NULL, NULL);
    }
    gtk_widget_destroy(dlg);
}

void tray_icon_perform_update_check(const char *current_version)
{
    if (!tray_auto_update_is_enabled()) return;
    if (!s_gtk_ok) return;

    CURL *curl = curl_easy_init();
    if (!curl) return;

    struct curl_buf buf = {0};
    char url[256];
    snprintf(url, sizeof(url), "https://%s%s", GITHUB_API_HOST, GITHUB_API_PATH);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "crush-claw-updater/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) { free(buf.data); return; }

    const char *tag = strstr(buf.data, "\"tag_name\"");
    char latest_ver[64] = {0};
    if (tag) {
        tag = strchr(tag, ':');
        if (tag) {
            tag++;
            while (*tag && (*tag == ' ' || *tag == '"')) tag++;
            const char *end = strchr(tag, '"');
            if (end && (size_t)(end - tag) < sizeof(latest_ver)) {
                memcpy(latest_ver, tag, end - tag);
            }
        }
    }
    free(buf.data);

    if (latest_ver[0] && compare_versions(latest_ver, current_version) > 0) {
        show_update_dialog(current_version, latest_ver);
    }
}

/* ---- X11 window operations (separate connection; no event stealing) ---- */

static void x11_show_window(void)
{
    Window win = s_x11_window;
    if (!win || !s_x11_dpy) return;

    bool lua_mode = display_hal_is_lua_mode();

    if (lua_mode) {
        XMapWindow(s_x11_dpy, win);
        XRaiseWindow(s_x11_dpy, win);

        Atom net_active = XInternAtom(s_x11_dpy, "_NET_ACTIVE_WINDOW", False);
        XEvent xev = {0};
        xev.type = ClientMessage;
        xev.xclient.window = win;
        xev.xclient.message_type = net_active;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 2;
        xev.xclient.data.l[1] = CurrentTime;
        XSendEvent(s_x11_dpy, DefaultRootWindow(s_x11_dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    } else {
        XMapWindow(s_x11_dpy, win);

        /* Re-add ABOVE for emote always-on-top */
        Atom net_state = XInternAtom(s_x11_dpy, "_NET_WM_STATE", False);
        Atom net_above = XInternAtom(s_x11_dpy, "_NET_WM_STATE_ABOVE", False);
        XEvent xev = {0};
        xev.type = ClientMessage;
        xev.xclient.window = win;
        xev.xclient.message_type = net_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
        xev.xclient.data.l[1] = (long)net_above;
        XSendEvent(s_x11_dpy, DefaultRootWindow(s_x11_dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    }
    XFlush(s_x11_dpy);
}

static void x11_hide_window(void)
{
    Window win = s_x11_window;
    if (!win || !s_x11_dpy) return;

    bool lua_mode = display_hal_is_lua_mode();

    if (lua_mode) {
        XIconifyWindow(s_x11_dpy, win, DefaultScreen(s_x11_dpy));
    } else {
        /* Strip ABOVE then withdraw */
        Atom net_state = XInternAtom(s_x11_dpy, "_NET_WM_STATE", False);
        Atom net_above = XInternAtom(s_x11_dpy, "_NET_WM_STATE_ABOVE", False);
        XEvent xev = {0};
        xev.type = ClientMessage;
        xev.xclient.window = win;
        xev.xclient.message_type = net_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 0; /* _NET_WM_STATE_REMOVE */
        xev.xclient.data.l[1] = (long)net_above;
        XSendEvent(s_x11_dpy, DefaultRootWindow(s_x11_dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);

        XWithdrawWindow(s_x11_dpy, win, DefaultScreen(s_x11_dpy));
    }
    XFlush(s_x11_dpy);
}

static void x11_set_urgency(bool urgent)
{
    Window win = s_x11_window;
    if (!win || !s_x11_dpy) return;

    XWMHints *hints = XGetWMHints(s_x11_dpy, win);
    if (!hints) {
        hints = XAllocWMHints();
        if (!hints) return;
        memset(hints, 0, sizeof(XWMHints));
        hints->flags = 0;
    }
    if (urgent)
        hints->flags |= XUrgencyHint;
    else
        hints->flags &= ~XUrgencyHint;
    XSetWMHints(s_x11_dpy, win, hints);
    XFree(hints);
    XFlush(s_x11_dpy);
}

/* ---- GTK menu callbacks ---- */

static void on_show(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (!s_window_visible) tray_icon_show_window();
}

static void on_hide(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    if (s_window_visible) tray_icon_hide_window();
}

static void on_always_hide(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    config_set_bool("always_hide_windows",
        gtk_check_menu_item_get_active(item));
}

static void on_autostart(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    autostart_set_enabled(gtk_check_menu_item_get_active(item));
}

static void on_auto_update(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    config_set_bool("auto_update_check",
        gtk_check_menu_item_get_active(item));
}

static void on_quit(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    s_quit_requested = true;
}

static void update_checkmarks(void)
{
    if (!s_menu) return;
    gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(s_item_always_hide),
        tray_always_hide_is_enabled());
    gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(s_item_autostart),
        autostart_is_enabled());
    gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(s_item_auto_update),
        tray_auto_update_is_enabled());
}

/* ---- Public API ---- */

bool tray_icon_init(void)
{
    if (s_initialized) return true;
    if (!getenv("DISPLAY")) return false;

    if (!gtk_init_check(NULL, NULL)) return false;
    s_gtk_ok = true;

    s_x11_dpy = XOpenDisplay(NULL);
    if (!s_x11_dpy) return false;

    s_indicator = app_indicator_new(
        "crush-claw",
        "crush-claw-tray",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    app_indicator_set_status(s_indicator, APP_INDICATOR_STATUS_ACTIVE);

    /* Use our lobster icon (installed to /usr/share/pixmaps/) */
    app_indicator_set_icon_full(s_indicator,
        "/usr/share/pixmaps/lobster.png", "Crush Claw Desktop");

    /* Build tray menu (matches Windows tray menu layout) */
    s_menu = gtk_menu_new();

    GtkWidget *item;

    item = gtk_menu_item_new_with_label("Show Window");
    g_signal_connect(item, "activate", G_CALLBACK(on_show), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);
    s_item_show = item;

    item = gtk_menu_item_new_with_label("Hide to Tray");
    g_signal_connect(item, "activate", G_CALLBACK(on_hide), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);
    s_item_hide = item;

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);

    item = gtk_check_menu_item_new_with_label("Always Hide Windows");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                    tray_always_hide_is_enabled());
    g_signal_connect(item, "toggled", G_CALLBACK(on_always_hide), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);
    s_item_always_hide = item;

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);

    item = gtk_check_menu_item_new_with_label("Auto-start on Login");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                    autostart_is_enabled());
    g_signal_connect(item, "toggled", G_CALLBACK(on_autostart), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);
    s_item_autostart = item;

    item = gtk_check_menu_item_new_with_label("Check for Updates on Startup");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                    tray_auto_update_is_enabled());
    g_signal_connect(item, "toggled", G_CALLBACK(on_auto_update), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);
    s_item_auto_update = item;

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);

    item = gtk_menu_item_new_with_label("Exit");
    g_signal_connect(item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(s_menu), item);

    gtk_widget_show_all(s_menu);

    /* Update checkmarks every time the menu is shown */
    g_signal_connect(s_menu, "show", G_CALLBACK(update_checkmarks), NULL);

    app_indicator_set_menu(s_indicator, GTK_MENU(s_menu));

    s_window_visible = true;
    s_initialized = true;
    return true;
}

void tray_icon_set_sdl_window(void *xid)
{
    Window prev = s_x11_window;
    s_x11_window = (Window)(uintptr_t)xid;
    if (!s_x11_window || !s_x11_dpy) return;

    Atom win_type  = XInternAtom(s_x11_dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom type_util = XInternAtom(s_x11_dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    Atom type_norm = XInternAtom(s_x11_dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    Atom net_state = XInternAtom(s_x11_dpy, "_NET_WM_STATE", False);
    Atom skip_task = XInternAtom(s_x11_dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);

    if (display_hal_is_lua_mode()) {
        /* Lua window → show in taskbar/dock */
        XChangeProperty(s_x11_dpy, s_x11_window, win_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&type_norm, 1);
        XEvent xev = {0};
        xev.type = ClientMessage;
        xev.xclient.window = s_x11_window;
        xev.xclient.message_type = net_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 0; /* remove */
        xev.xclient.data.l[1] = (long)skip_task;
        XSendEvent(s_x11_dpy, DefaultRootWindow(s_x11_dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    } else {
        /* Emote window → hide from taskbar/dock */
        XChangeProperty(s_x11_dpy, s_x11_window, win_type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&type_util, 1);
        XEvent xev = {0};
        xev.type = ClientMessage;
        xev.xclient.window = s_x11_window;
        xev.xclient.message_type = net_state;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 1; /* add */
        xev.xclient.data.l[1] = (long)skip_task;
        XSendEvent(s_x11_dpy, DefaultRootWindow(s_x11_dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
    }
    XFlush(s_x11_dpy);

    /* Cycle visibility to apply property changes (same pattern as Windows) */
    if (prev != s_x11_window && prev != 0) {
        XUnmapWindow(s_x11_dpy, s_x11_window);
        XMapWindow(s_x11_dpy, s_x11_window);
    }
}

void tray_icon_show_window(void)
{
    x11_show_window();
    s_window_visible = true;
}

void tray_icon_hide_window(void)
{
    x11_hide_window();
    s_window_visible = false;
}

void tray_icon_show_and_flash(void)
{
    x11_set_urgency(true);
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
    if (!s_gtk_ok) return;
    /* Process pending GTK events (non-blocking).
       GTK uses its own X11 connection — does not interfere with SDL. */
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
}

void tray_icon_cleanup(void)
{
    if (s_indicator) {
        g_object_unref(s_indicator);
        s_indicator = NULL;
    }
    if (s_x11_dpy) {
        XCloseDisplay(s_x11_dpy);
        s_x11_dpy = NULL;
    }
    s_menu = NULL;
    s_x11_window = 0;
    s_initialized = false;
    s_gtk_ok = false;
}

#endif /* HAS_TRAY_LINUX_GTK */

#endif /* !_WIN32 */
