#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_tls_crypto.h"
#include "http_parser.h"
#include "protocol_examples_utils.c"
#include <string.h>
#include <unistd.h>

#define EXAMPLE_HTTP_QUERY_KEY_MAX_KEN (64)

static const char *WEBSERVER_TAG = "webserver";
/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req) {
  char *buf;
  size_t buf_len;

  /* Get header value string length and allocate memory for length + 1,
   * extra byte for null termination */
  buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
  if (buf_len > 1) {
    buf = malloc(buf_len);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, WEBSERVER_TAG,
                        "buffer alloc failed");
    /* Copy null terminated value string into buffer */
    if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
      ESP_LOGI(WEBSERVER_TAG, "Found header => Host: %s", buf);
    }
    free(buf);
  }

  // ... more header tests

  /* Read URL query string length and allocate memory for length + 1;
   * extra byte for null termination */
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = malloc(buf_len);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, WEBSERVER_TAG,
                        "buffer alloc failed");
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      ESP_LOGI(WEBSERVER_TAG, "Found URL query => %s", buf);
      char param[EXAMPLE_HTTP_QUERY_KEY_MAX_KEN],
          dec_param[EXAMPLE_HTTP_QUERY_KEY_MAX_KEN] = {0};
      /* Get value of expected key from query string */
      if (httpd_query_key_value(buf, "query1", param, sizeof(param)) ==
          ESP_OK) {
        ESP_LOGI(WEBSERVER_TAG, "Found URL query parameter => query1=%s",
                 param);
        example_uri_decode(dec_param, param,
                           strnlen(param, EXAMPLE_HTTP_QUERY_KEY_MAX_KEN));
        ESP_LOGI(WEBSERVER_TAG, "Decode query parameter => %s", dec_param);
      }
    }
    free(buf);
  }

  /* Set some custom headers */
  httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");

  /* Send response with custom headers and body set as the
   * string passed in user context */
  const char *resp_str = (const char *)req->user_ctx;
  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static const httpd_uri_t hello = {.uri = "/hello",
                           .method = HTTP_GET,
                           .handler = hello_get_handler,
                           /* Let's pass response string in user
                            * context to demonstrate it's usage */
                           .user_ctx = "Hello world"};

/* Function for starting the webserver */
static httpd_handle_t start_webserver(void) {
  // Generate default configuration
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  // Empty handle to http_server
  httpd_handle_t server = NULL;

  // Start the httpd server
  if (httpd_start(&server, &config) == ESP_OK) {
    // Register URI handlers
    ESP_LOGI(WEBSERVER_TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &hello);
  }

  return server;
}
