/******************************************************************************
 * main.c
 *
 * ESP-NOW Gateway Demo
 *
 * ESP-IDF 6.0.2
 ******************************************************************************/

#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "application/gateway.h"
#include "network/enp_transport_espnow.h"
#include "network/enp_transport_wifi.h"

static const char *TAG = "MAIN";

/*------------------------------------------------------------------
 * NVS
 *-----------------------------------------------------------------*/

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();

    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());

        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);
}

/*------------------------------------------------------------------
 * app_main
 *-----------------------------------------------------------------*/

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "ENP");
	 ESP_LOGI(TAG, "ESP Network Protocol");
     ESP_LOGI(TAG, "Version 0.2.0");
     ESP_LOGI(TAG, "======================================");

 #if CONFIG_DEVICE_ROLE_GATEWAY
     ESP_LOGI(TAG, "Device Role : Gateway");
 #else
     ESP_LOGI(TAG, "Device Role : Sensor");
 #endif

     nvs_init();

     ESP_ERROR_CHECK(esp_netif_init());

     ESP_ERROR_CHECK(esp_event_loop_create_default());

     ESP_ERROR_CHECK(enp_wifi_init());

     ESP_LOGI(TAG, "Waiting for WiFi...");

     while (!enp_wifi_is_connected())
     {
         vTaskDelay(pdMS_TO_TICKS(100));
     }

     ESP_LOGI(TAG,
              "Connected on channel %u",
              enp_wifi_get_channel());

     ESP_ERROR_CHECK(
             enp_transport_init());

 #if CONFIG_DEVICE_ROLE_GATEWAY

     ESP_ERROR_CHECK(
             enp_gateway_init());

 #else

     ESP_ERROR_CHECK(
             esp_sensor_init());

 #endif

     ESP_LOGI(TAG,
              "Application started");

     vTaskDelete(NULL);
 }
