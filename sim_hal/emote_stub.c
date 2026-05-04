/*
 * emote_stub.c — Simulator emote engine (replaces esp-claw's emote.c).
 *
 * Differences from the real emote.c:
 *   - Uses EMOTE_SOURCE_PATH + filesystem dir instead of flash partition
 *   - panel_handle and io_handle are fake pointers (esp_lcd_panel_draw_bitmap
 *     bridges to display_hal regardless of handle value)
 *
 * Real esp-claw code compiled unchanged:
 *   - esp_emote_expression (emote engine + gfx + LVGL fonts)
 *   - display_arbiter (ownership management)
 */
#include "emote.h"

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "expression_emote.h"
#include "freertos/FreeRTOS.h"
#include "gfx.h"
#include "display_arbiter.h"
#include "display_hal.h"
#include "esp_lcd_touch.h"
#include "core/gfx_touch.h"
#include "emote_defs.h"

/* display_sdl2.c extension — signals main loop that a frame is ready */
extern void display_hal_mark_frame_ready(void);

static const char *TAG = "app_emote";

/* Forward declarations */
static emote_handle_t s_emote_handle;
static bool s_touch_enabled = true;

static void sim_touch_event_cb(gfx_touch_t *touch,
                                const gfx_touch_event_t *event,
                                void *user_data)
{
    (void)touch;
    (void)user_data;
    if (!s_emote_handle || !s_touch_enabled) return;

    if (event->type == GFX_TOUCH_EVENT_PRESS) {
        /* Play "offline" animation once (76 frames @ 20 fps = 3.8 s),
         * then auto-restore "swim" idle. */
        emote_insert_anim_dialog(s_emote_handle, "offline", 3800);
        ESP_LOGI(TAG, "touch tap: playing 'offline' dialog (3.8s)");
    }
}

/* Try installed path first, fall back to dev-relative path */
#define EMOTE_ASSETS_DIR_DEV      "sim_hal/assets/284_240"
#define EMOTE_ASSETS_DIR_INSTALLED "/usr/share/esp-agent/assets/284_240"

#include <sys/stat.h>

static const char *emote_get_assets_dir(void)
{
    struct stat st;
    if (stat(EMOTE_ASSETS_DIR_INSTALLED, &st) == 0 && S_ISDIR(st.st_mode)) {
        return EMOTE_ASSETS_DIR_INSTALLED;
    }
    return EMOTE_ASSETS_DIR_DEV;
}

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static int s_lcd_width;
static int s_lcd_height;
static emote_handle_t s_emote_handle;

static void emote_on_owner_changed(display_arbiter_owner_t owner, void *user_ctx)
{
    (void)user_ctx;
    if (owner != DISPLAY_ARBITER_OWNER_EMOTE || !s_emote_handle) {
        return;
    }
    esp_err_t err = emote_notify_all_refresh(s_emote_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh after owner switch failed: %s", esp_err_to_name(err));
    }
}

static void emote_flush_callback(int x_start, int y_start, int x_end, int y_end,
                                 const void *data, emote_handle_t handle)
{
    if (!s_panel_handle || !display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        if (handle) {
            emote_notify_flush_finished(handle);
        }
        return;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel_handle,
                                              x_start, y_start, x_end, y_end, data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
    }

    /* display_hal_present() is called from the main loop — SDL2 requires
     * rendering operations (RenderClear/RenderCopy/RenderPresent) to happen
     * on the window-creating thread.  Pixel writes (draw_bitmap) are safe
     * from any thread since they only touch the software surface buffer. */
    display_hal_mark_frame_ready();

    if (handle) {
        emote_notify_flush_finished(handle);
    }
}

static void emote_update_callback(gfx_disp_event_t event, const void *obj,
                                  emote_handle_t handle)
{
    if (!handle) return;
    gfx_obj_t *wait_obj = emote_get_obj_by_name(handle, EMT_DEF_ELEM_EMERG_DLG);
    if (wait_obj == obj && event == GFX_DISP_EVENT_ALL_FRAME_DONE) {
        ESP_LOGI(TAG, "Emergency dialog finished");
    }
}

static esp_err_t emote_load_board_display(void)
{
#if !CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    void *lcd_handle = NULL;
    void *lcd_config = NULL;
    esp_err_t err = esp_board_manager_get_device_handle(
        ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_handle);
    if (err != ESP_OK) return err;

    err = esp_board_manager_get_device_config(
        ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_config);
    if (err != ESP_OK) return err;

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)lcd_handle;
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)lcd_config;

    ESP_RETURN_ON_FALSE(lcd_handles && lcd_cfg && lcd_handles->panel_handle,
                        ESP_ERR_INVALID_STATE, TAG, "display_lcd handle/config is NULL");

    s_panel_handle = lcd_handles->panel_handle;
    s_io_handle = lcd_handles->io_handle;
    /* Emote engine always renders at the hardware LCD size (320x240),
       regardless of the virtual display size used by Lua scripts.
       The board_manager stub may report a different virtual LCD size. */
    s_lcd_width = 320;
    s_lcd_height = 240;
    return ESP_OK;
#endif
}

static emote_config_t emote_get_default_config(void)
{
    /* On real hardware the LCD interface is big-endian so the gfx
     * byte-swaps RGB565 output (swap=true).  Our SDL2 surface is
     * little-endian RGB565 — no swap needed. */
    bool swap = false;

    emote_config_t config = {
        .flags = {
            .swap = swap,
            .double_buffer = true,
            .buff_dma = true,
        },
        .gfx_emote = {
            .h_res = s_lcd_width,
            .v_res = s_lcd_height,
            .fps = 10,
        },
        .buffers = {
            .buf_pixels = (size_t)s_lcd_width * 16,
        },
        .task = {
            .task_priority = 3,
            .task_stack = 12 * 1024,
            .task_affinity = -1,
            .task_stack_in_ext = false,
        },
        .flush_cb = emote_flush_callback,
        .update_cb = emote_update_callback,
    };
    return config;
}

static esp_err_t emote_apply(const char *idle, const char *msg)
{
    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "emote handle is NULL");

    ESP_RETURN_ON_ERROR(emote_set_event_msg(s_emote_handle, EMOTE_MGR_EVT_SYS, msg),
                        TAG, "set emote message failed");
    ESP_RETURN_ON_ERROR(emote_set_anim_emoji(s_emote_handle, idle),
                        TAG, "set emote idle animation failed");

    if (display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        ESP_RETURN_ON_ERROR(emote_notify_all_refresh(s_emote_handle),
                            TAG, "refresh emote display failed");
    }
    return ESP_OK;
}

esp_err_t emote_set_network_status(bool sta_connected, const char *ap_ssid)
{
    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "emote handle is NULL");

    const bool ap_present = (ap_ssid != NULL && ap_ssid[0] != '\0');
    const char *idle = sta_connected ? "swim" : "offline";

    char msg[96];
    if (sta_connected && ap_present) {
        snprintf(msg, sizeof(msg), "Online * AP: %s", ap_ssid);
    } else if (sta_connected) {
        snprintf(msg, sizeof(msg), "Wi-Fi connected");
    } else if (ap_present) {
        snprintf(msg, sizeof(msg), "Setup WiFi: %s", ap_ssid);
    } else {
        snprintf(msg, sizeof(msg), "Wi-Fi offline");
    }

    return emote_apply(idle, msg);
}

static void emote_cleanup(void)
{
    if (s_emote_handle) {
        emote_deinit(s_emote_handle);
        s_emote_handle = NULL;
    }
    display_arbiter_set_owner_changed_callback(NULL, NULL);
}

static esp_err_t emote_init_internal(void)
{
    emote_data_t data = {
        .type = EMOTE_SOURCE_PATH,
        .source = {
            .path = emote_get_assets_dir(),
        },
        .flags = {
            .mmap_enable = false,
        },
    };

    if (s_emote_handle) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(emote_load_board_display(), TAG,
                        "Failed to get board display handles");

    emote_config_t config = emote_get_default_config();
    ESP_RETURN_ON_ERROR(display_arbiter_set_owner_changed_callback(
                            emote_on_owner_changed, NULL), TAG,
                        "register display owner callback failed");
    s_emote_handle = emote_init(&config);
    if (!s_emote_handle || !emote_is_initialized(s_emote_handle)) {
        emote_cleanup();
        return ESP_FAIL;
    }

    esp_err_t err = emote_mount_and_load_assets(s_emote_handle, &data);
    if (err != ESP_OK) {
        emote_cleanup();
        return err;
    }

    /* Acquire EMOTE ownership — this is the default owner (display_arbiter starts
       with EMOTE as default, but we explicitly acquire to be safe). */
    display_arbiter_acquire(DISPLAY_ARBITER_OWNER_EMOTE);

    /* Wire SDL mouse → emote touch: register the SDL touch bridge with
     * the gfx engine so that mouse clicks trigger animation reactions. */
    {
        esp_lcd_touch_handle_t touch_h = esp_lcd_touch_init_sdl();
        if (touch_h) {
            gfx_touch_config_t tcfg = {
                .handle = touch_h,
                .event_cb = sim_touch_event_cb,
                .poll_ms = 30,
                .disp = s_emote_handle->gfx_disp,
                .user_data = NULL,
            };
            gfx_touch_t *t = gfx_touch_add(s_emote_handle->gfx_handle, &tcfg);
            if (t) {
                ESP_LOGI(TAG, "SDL touch bridge wired: poll=%dms", 30);
            } else {
                ESP_LOGW(TAG, "gfx_touch_add failed — emote won't react to clicks");
            }
        }
    }

    return emote_set_network_status(true, NULL);
}

esp_err_t emote_start(void)
{
    esp_err_t err = emote_init_internal();
    if (err != ESP_OK) {
        emote_cleanup();
    }
    return err;
}
