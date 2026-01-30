#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/spi_types.h"
#include "nvs_flash.h"
#include <stdint.h>

#include "esp_event.h"
#include "esp_wifi.h"
#include "portmacro.h"

#include "client.h"
#include "webserver.h"
#include "env.h"

#define GPIO_CS 15
#define GPIO_SCLK 14
#define GPIO_MISO 12
#define GPIO_MOSI 13

// TODO: use env variables to replace before
// pushing to git

#define MAXIMUM_RETRY 5

/* FreeRTOS event group to signal when we are connected */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each even, but we only care about
 * two events:
 * - we are connected to the AP (access point) with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi_station";
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < MAXIMUM_RETRY) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "retry to connect to the AP");
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "connect to the AP fail");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASSWORD,
              /* Setting a password implies station will connect to all security
               * modes including WEP/WPA. However these modes are deprecated and
               * not advisable to be used. Incase your Access point doesn't
               * support WPA2, these mode can be enabled by commenting below
               * line */
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  /* xEventGroupWaitBits() returns the bits before the call returned, hence we
   * can test which event actually happened. */
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", WIFI_SSID,
             WIFI_PASSWORD);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", WIFI_SSID,
             WIFI_PASSWORD);
  } else {
    ESP_LOGI(TAG, "unexpected event");
  }
}
void app_main(void) {
  esp_err_t ret;

  httpd_handle_t server = NULL;

  ESP_ERROR_CHECK(nvs_flash_init());

  ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
  wifi_init_sta();

  server = start_webserver();

  http_rest_with_url();

  while(server) {
    printf("Accepting requests...\n");
    sleep(5);
  }

  // Configuration for the SPI bus
  spi_bus_config_t buscfg = {.mosi_io_num = GPIO_MOSI,
                             .miso_io_num = GPIO_MISO,
                             .sclk_io_num = GPIO_SCLK};

  // Configuration for the SPI device on the other side of the bus
  spi_device_interface_config_t devcfg = {
      .command_bits = 0,
      .address_bits = 0,
      .dummy_bits = 0,
      .clock_speed_hz = 1000000, // 1 MHz
      .duty_cycle_pos = 128,     // 50% duty cycle
      .mode = 0,                 // SPI mode 0
      .spics_io_num = GPIO_CS,   // CS pin
      .cs_ena_posttrans = 3,     // Keep the CS low 3 cycles after transaction
      .queue_size = 1};

  // Initialize the SPI bus
  ret = spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
  printf("spi_bus_initialize %d\n", ret);

  spi_device_handle_t spi_handle;
  ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi_handle);
  printf("spi_bus_add_device %d\n", ret);

  uint8_t tx_data = 'C';
  uint8_t rx_data = 'D';
  printf("tx_data: %c\n", tx_data);
  printf("rx_data: %c\n", rx_data);

  spi_transaction_t trans = {.length = 8 * sizeof(tx_data), // bits to transmit
                             .rxlength = 8 * sizeof(rx_data), // bits to receive
                             .tx_buffer = &tx_data,
                             .rx_buffer = &rx_data};

  printf("Master received:\n");
  while (1) {
    ret = spi_device_transmit(spi_handle, &trans);
    printf("Transmitted: %c\n", *((char *)trans.tx_buffer));
    printf("received: %c\n", *((char *)trans.rx_buffer));
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
