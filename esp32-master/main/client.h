#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_log_level.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"
#include "inttypes.h"
#include "stdlib.h"
#include "string.h"
#include "sys/param.h"

#include "esp_http_client.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048
#define MAX_HTTP_RECV_BUFFER 512

static const char *CLIENT_TAG = "HTTP_CLIENT";

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
  char *buffer = (char *)evt->user_data; // Buffer to store response of http
                                         // request from event handler
  static int output_len = 0;             // stores number of bytes read
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
             evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGI(CLIENT_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    if (evt->user_data) {
      memcpy(evt->user_data + output_len, evt->data, evt->data_len);
      output_len += evt->data_len;
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_ON_FINISH");
    buffer[output_len] = '\0';
    output_len = 0;
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGI(CLIENT_TAG, "HTTP_EVENT_DISCONNECTED");
    int mbedtls_err = 0;
    esp_err_t err = esp_tls_get_and_clear_last_error(
        (esp_tls_error_handle_t)evt->user_data, &mbedtls_err, NULL);
    if (err != 0) {
      ESP_LOGI(CLIENT_TAG, "Last esp error code: 0x%x", err);
      ESP_LOGI(CLIENT_TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
    }
    buffer[output_len] = '\0';
    output_len = 0;
    break;
  case HTTP_EVENT_REDIRECT:
    ESP_LOGD(CLIENT_TAG, "HTTP_EVENT_REDIRECT");
    esp_http_client_set_header(evt->client, "From", "user@example.com");
    esp_http_client_set_header(evt->client, "Accept", "text/html");
    esp_http_client_set_redirection(evt->client);
    break;
  default:
    break;
  }
  return ESP_OK;
}

void http_rest_with_url(void) {
  // Declare local_response_buffer with size (MAX_HTTP_OUTPUT_BUFFER + 1) to
  // prevent out of bound access when it is used by functions like strlen().
  // The buffer should only be used upto size MAX_HTTP_OUTPUT_BUFFER
  static char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};

  /**
   * NOTE: All the configuration parameters for http_client must be specified
   * either in URL or as host and path parameters. If host and path parameters
   * are not set, query parameter will be ignored. In such cases, query
   * parameter should be specified in URL.
   *
   * If URL as well as host and path parameters are specified, values of host
   * and path will be considered.
   */

  esp_http_client_config_t config = {
      .url = "https://api.open-meteo.com/v1/"
             "forecast?latitude=36.2127&longitude=-121.126&current=temperature_"
             "2m&timezone=America%2FLos_Angeles&temperature_unit=fahrenheit",
      .event_handler = _http_event_handler,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .user_data = local_response_buffer,
      .disable_auto_redirect = true};
  ESP_LOGI(CLIENT_TAG, "HTTP request with url => %s", config.url);
  esp_http_client_handle_t client = esp_http_client_init(&config);

  // GET
  esp_err_t err = esp_http_client_perform(client);
  ESP_LOGI("HTTP_CLIENT", "Response:\n%s", local_response_buffer);

  if (err == ESP_OK) {
    ESP_LOGI(CLIENT_TAG, "HTTP GET Status = %d, content_length = %" PRId64,
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(CLIENT_TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
}
