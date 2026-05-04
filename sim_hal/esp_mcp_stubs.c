/*
 * esp_mcp_stubs.c — No-op MCP SDK stubs for desktop simulator.
 *
 * All functions return ESP_OK (success) so cap_mcp_server's init/start flow
 * succeeds and the capability group registers. No real MCP server runs on
 * desktop — this is purely to pass compilation and capability registration.
 */
#include "esp_err.h"
#include "esp_mcp_data.h"
#include "esp_mcp_property.h"
#include "esp_mcp_tool.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_mgr.h"

#include <stdlib.h>
#include <string.h>

/* ---- esp_mcp_value_t (by-value returns, must match SDK layout) ---- */

esp_mcp_value_t esp_mcp_value_create_bool(bool value)
{
    esp_mcp_value_t v = { .type = ESP_MCP_VALUE_TYPE_BOOLEAN };
    v.data.bool_value = value;
    return v;
}

esp_mcp_value_t esp_mcp_value_create_string(const char *value)
{
    esp_mcp_value_t v = { .type = ESP_MCP_VALUE_TYPE_INVALID };
    if (!value) return v;
    v.data.string_value = strdup(value);
    if (v.data.string_value) v.type = ESP_MCP_VALUE_TYPE_STRING;
    return v;
}

/* ---- esp_mcp_property_t stubs ---- */

esp_mcp_property_t *esp_mcp_property_create_with_string(const char *name,
                                                         const char *default_value)
{
    (void)name; (void)default_value;
    /* Return a non-NULL dummy pointer so tool registration succeeds */
    return (esp_mcp_property_t *)1;
}

const char *esp_mcp_property_list_get_property_string(
    const esp_mcp_property_list_t *list, const char *name)
{
    (void)list; (void)name;
    return NULL;
}

/* ---- esp_mcp_tool_t stubs ---- */

esp_mcp_tool_t *esp_mcp_tool_create(const char *name, const char *description,
                                     esp_mcp_tool_callback_t callback)
{
    (void)name; (void)description; (void)callback;
    return (esp_mcp_tool_t *)1; /* non-NULL dummy */
}

esp_err_t esp_mcp_tool_add_property(esp_mcp_tool_t *tool,
                                     esp_mcp_property_t *property)
{
    (void)tool; (void)property;
    return ESP_OK;
}

/* ---- esp_mcp_t stubs ---- */

esp_err_t esp_mcp_create(esp_mcp_t **mcp)
{
    if (!mcp) return ESP_ERR_INVALID_ARG;
    *mcp = (esp_mcp_t *)1; /* non-NULL dummy handle */
    return ESP_OK;
}

esp_err_t esp_mcp_add_tool(esp_mcp_t *mcp, esp_mcp_tool_t *tool)
{
    (void)mcp; (void)tool;
    return ESP_OK;
}

/* ---- esp_mcp_mgr stubs ---- */

/* Minimal transport vtable — all no-ops, never actually invoked */
static const esp_mcp_transport_t s_stub_transport = {0};

const esp_mcp_transport_t esp_mcp_transport_http_server = {0};

esp_err_t esp_mcp_mgr_init(esp_mcp_mgr_config_t config,
                            esp_mcp_mgr_handle_t *handle)
{
    (void)config;
    if (!handle) return ESP_ERR_INVALID_ARG;
    /* A real init would use config.transport, config.config, config.instance */
    *handle = 1; /* non-zero handle */
    return ESP_OK;
}

esp_err_t esp_mcp_mgr_start(esp_mcp_mgr_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t esp_mcp_mgr_stop(esp_mcp_mgr_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t esp_mcp_mgr_deinit(esp_mcp_mgr_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

esp_err_t esp_mcp_mgr_register_endpoint(esp_mcp_mgr_handle_t handle,
                                         const char *ep_name,
                                         void *priv_data)
{
    (void)handle; (void)ep_name; (void)priv_data;
    return ESP_OK;
}
