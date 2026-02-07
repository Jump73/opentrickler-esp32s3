#ifndef HTTP_SERVER_OT_H_
#define HTTP_SERVER_OT_H_

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// REST handler function type - similar to original OpenTrickler
// Returns: response data string (must be static or dynamically allocated)
// Parameters: query parameters from URL
typedef char* (*rest_handler_t)(int num_params, char *params[], char *values[]);

// Initialize HTTP server
esp_err_t http_server_init(void);

// Stop HTTP server
esp_err_t http_server_stop(void);

// Register a REST endpoint handler (GET and POST)
// uri: endpoint path (e.g., "/rest/scale_config")
// handler: function to handle the request
esp_err_t http_server_register_rest_handler(const char *uri, rest_handler_t handler);

// Register a static page handler (GET only)
// uri: page path (e.g., "/")
// html_data: pointer to HTML string (should be persistent/static)
esp_err_t http_server_register_page_handler(const char *uri, const char *html_data);

#ifdef __cplusplus
}
#endif

#endif  // HTTP_SERVER_OT_H_
