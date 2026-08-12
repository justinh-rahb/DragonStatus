#include "ds_portal.h"

#include "cJSON.h"
#include "dc_bambu.h"
#include "dc_moonraker.h"
#include "dc_portal.h"
#include "dc_source.h"
#include "dc_wifi.h"
#include "ds_board.h"
#include "ds_lighting.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static cJSON *recv_json(httpd_req_t *req)
{
    if (!req->content_len || req->content_len > 1024) return NULL;
    char body[1025]; int got = httpd_req_recv(req, body, req->content_len);
    if (got != req->content_len) return NULL;
    body[got] = '\0'; return cJSON_Parse(body);
}

static cJSON *lighting_json(void)
{
    ds_lighting_config_t config; ds_lighting_get_config(&config);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddNumberToObject(root, "brightness", config.brightness);
    cJSON_AddNumberToObject(root, "speed", config.speed);
    cJSON_AddNumberToObject(root, "effect", config.effect);
    cJSON_AddStringToObject(root, "profile", "factory_h2d");
    return root;
}

static esp_err_t lighting_get(httpd_req_t *req) { return send_json(req, lighting_json()); }

static esp_err_t lighting_post(httpd_req_t *req)
{
    cJSON *body = recv_json(req); if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    ds_lighting_config_t config; ds_lighting_get_config(&config); cJSON *value;
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "enabled")) && cJSON_IsBool(value)) config.enabled = cJSON_IsTrue(value);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "brightness")) && cJSON_IsNumber(value)) config.brightness = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > 255 ? 255 : value->valueint);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "speed")) && cJSON_IsNumber(value)) config.speed = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > 255 ? 255 : value->valueint);
    if ((value = cJSON_GetObjectItemCaseSensitive(body, "effect")) && cJSON_IsNumber(value)) config.effect = (uint8_t)(value->valueint < 0 ? 0 : value->valueint > DC_LIGHTING_PROGRESS ? DC_LIGHTING_PROGRESS : value->valueint);
    cJSON_Delete(body); if (ds_lighting_set_config(&config) != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save lighting");
    return send_json(req, lighting_json());
}

static const char *wifi_state(dc_wifi_state_t state)
{
    switch (state) {
    case DC_WIFI_STATE_STA_CONNECTING: return "connecting";
    case DC_WIFI_STATE_STA_CONNECTED:  return "station";
    case DC_WIFI_STATE_AP_PORTAL:      return "setup_ap";
    default: return "starting";
    }
}

static const char *printer_state(void)
{
    if (dc_source_get() == DC_SRC_KLIPPER) {
        dc_moonraker_status_t status = {0};
        dc_moonraker_get_status(&status);
        return dc_printer_state_str(status.printer);
    }
    if (dc_source_get() == DC_SRC_BAMBU) {
        dc_bambu_status_t status = {0};
        dc_bambu_get_status(&status);
        return status.error ? "error" : status.printing ? "printing" : status.connected ? "idle" : "unknown";
    }
    return "standalone";
}

static esp_err_t info_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddStringToObject(root, "project", "dragonstatus");
    cJSON_AddStringToObject(root, "firmware", esp_app_get_description()->version);
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("source_status"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("lighting"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("polling"));
    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "schema", 1);
    cJSON_AddStringToObject(ui, "product", "dragonstatus");
    cJSON_AddStringToObject(ui, "display_name", "DragonStatus");
    return send_json(req, root);
}

static esp_err_t state_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "api_version", 2);
    cJSON_AddStringToObject(root, "project", "dragonstatus");
    cJSON *printer = cJSON_AddObjectToObject(root, "printer");
    cJSON_AddStringToObject(printer, "source", dc_source_str(dc_source_get()));
    cJSON_AddStringToObject(printer, "state", printer_state());
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "state", wifi_state(dc_wifi_state()));
    cJSON *lighting = cJSON_AddObjectToObject(root, "lighting");
    dc_lighting_output_t outputs[DC_LIGHTING_MAX_OUTPUTS];
    uint8_t output_count = 0;
    cJSON_AddBoolToObject(lighting, "hardware_ready", ds_board_lighting_outputs(outputs, &output_count));
    cJSON_AddNumberToObject(lighting, "outputs", output_count);
    return send_json(req, root);
}

static const httpd_uri_t s_routes[] = {
    { .uri = "/api/v2/info", .method = HTTP_GET, .handler = info_get },
    { .uri = "/api/v2/state", .method = HTTP_GET, .handler = state_get },
    { .uri = "/api/v2/lighting", .method = HTTP_GET, .handler = lighting_get },
    { .uri = "/api/v2/lighting", .method = HTTP_POST, .handler = lighting_post },
};

esp_err_t ds_portal_start(void)
{
    return dc_portal_start(&(dc_portal_config_t){
        .product = "dragonstatus",
        .display_name = "DragonStatus",
        .product_routes = s_routes,
        .product_route_count = sizeof(s_routes) / sizeof(s_routes[0]),
    });
}
