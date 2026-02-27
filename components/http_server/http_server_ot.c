#include "http_server_ot.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdlib.h>

// Decode percent-encoded URL string in-place. Returns dst (same as src).
static char *url_decode_inplace(char *s)
{
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return s;
}

static const char *TAG = "HTTP_Server";

static httpd_handle_t server = NULL;

#define MAX_HANDLERS 48
#define MAX_URI_LEN 64

typedef struct {
    char uri[MAX_URI_LEN];
    rest_handler_t handler;
    bool is_registered;
} rest_handler_entry_t;

static rest_handler_entry_t rest_handlers[MAX_HANDLERS];
static int handler_count = 0;

// Generic REST handler wrapper for ESP-IDF HTTP server
static esp_err_t rest_handler_wrapper(httpd_req_t *req)
{
    char *response = NULL;

    // Find the registered handler for this URI (ignoring query string)
    rest_handler_t handler = NULL;

    // Extract base URI (without query string)
    const char *query_start = strchr(req->uri, '?');
    size_t uri_len = query_start ? (size_t)(query_start - req->uri) : strlen(req->uri);

    for (int i = 0; i < handler_count; i++) {
        if (rest_handlers[i].is_registered) {
            size_t handler_uri_len = strlen(rest_handlers[i].uri);
            // Match if lengths equal and base URI matches
            if (uri_len == handler_uri_len &&
                strncmp(rest_handlers[i].uri, req->uri, uri_len) == 0) {
                handler = rest_handlers[i].handler;
                break;
            }
        }
    }

    if (!handler) {
        ESP_LOGW(TAG, "No handler found for URI: %s", req->uri);
        ESP_LOGW(TAG, "Registered handlers:");
        for (int i = 0; i < handler_count; i++) {
            ESP_LOGW(TAG, "  [%d] %s", i, rest_handlers[i].uri);
        }
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Parse query parameters
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    char *params_buf = NULL;
    char *param_names[16];
    char *param_values[16];
    int num_params = 0;

    if (buf_len > 1) {
        params_buf = malloc(buf_len);
        if (params_buf) {
            if (httpd_req_get_url_query_str(req, params_buf, buf_len) == ESP_OK) {
                // Parse query string into param/value pairs
                char *token = strtok(params_buf, "&");
                while (token && num_params < 16) {
                    char *eq = strchr(token, '=');
                    if (eq) {
                        *eq = '\0';
                        param_names[num_params] = token;
                        param_values[num_params] = eq + 1;
                        url_decode_inplace(param_values[num_params]);
                        num_params++;
                    }
                    token = strtok(NULL, "&");
                }
            }
        }
    }

    // Handle POST data (if any)
    if (req->method == HTTP_POST) {
        char *content = malloc(req->content_len + 1);
        if (content) {
            int ret = httpd_req_recv(req, content, req->content_len);
            if (ret > 0) {
                content[ret] = '\0';
                // TODO: Parse POST data into params/values
                // For now, just log it
                ESP_LOGI(TAG, "POST data: %s", content);
            }
            free(content);
        }
    }

    // Call the registered handler
    response = handler(num_params, param_names, param_values);

    if (params_buf) {
        free(params_buf);
    }

    // Send response
    if (response) {
        // Check if response already contains HTTP headers (starts with "HTTP/1.")
        // This allows dynamic handlers to return full HTML pages
        if (strncmp(response, "HTTP/1.", 7) == 0) {
            // Response has full HTTP headers - send raw
            httpd_resp_send(req, response, strlen(response));
        } else {
            // Response is JSON - add headers
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, response, strlen(response));
        }
        // Note: Original code used static strings, so we don't free response
        return ESP_OK;
    } else {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
}

// Static page handler wrapper
static esp_err_t page_handler_wrapper(httpd_req_t *req)
{
    const char *html_data = (const char *)req->user_ctx;

    if (html_data) {
        // HTML data already includes HTTP headers from html2header.py
        httpd_resp_send(req, html_data, strlen(html_data));
        return ESP_OK;
    } else {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}

esp_err_t http_server_init(void)
{
    if (server) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = MAX_HANDLERS;
    config.stack_size = 8192;  // Increase stack size for handler tasks
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
        memset(rest_handlers, 0, sizeof(rest_handlers));
        handler_count = 0;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ret;
    }
}

esp_err_t http_server_stop(void)
{
    if (server) {
        esp_err_t ret = httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
        return ret;
    }
    return ESP_OK;
}

esp_err_t http_server_register_rest_handler(const char *uri, rest_handler_t handler)
{
    if (!server) {
        ESP_LOGE(TAG, "HTTP server not initialized");
        return ESP_FAIL;
    }

    if (handler_count >= MAX_HANDLERS) {
        ESP_LOGE(TAG, "Maximum handlers reached");
        return ESP_FAIL;
    }

    // Store handler info
    strncpy(rest_handlers[handler_count].uri, uri, MAX_URI_LEN - 1);
    rest_handlers[handler_count].handler = handler;
    rest_handlers[handler_count].is_registered = true;
    handler_count++;

    // Register GET handler
    httpd_uri_t uri_get = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = rest_handler_wrapper,
        .user_ctx = NULL
    };
    esp_err_t ret = httpd_register_uri_handler(server, &uri_get);

    // Register POST handler
    httpd_uri_t uri_post = {
        .uri = uri,
        .method = HTTP_POST,
        .handler = rest_handler_wrapper,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_post);

    ESP_LOGI(TAG, "Registered REST handler: %s", uri);
    return ret;
}

esp_err_t http_server_register_page_handler(const char *uri, const char *html_data)
{
    if (!server) {
        ESP_LOGE(TAG, "HTTP server not initialized");
        return ESP_FAIL;
    }

    httpd_uri_t uri_get = {
        .uri = uri,
        .method = HTTP_GET,
        .handler = page_handler_wrapper,
        .user_ctx = (void *)html_data  // Pass HTML data as user context
    };

    esp_err_t ret = httpd_register_uri_handler(server, &uri_get);
    ESP_LOGI(TAG, "Registered page handler: %s", uri);
    return ret;
}
