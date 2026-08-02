/******************************************************************************
 * main.c
 *
 * ESP-NOW Gateway Demo
 *
 * ESP-IDF 6.0.2
 ******************************************************************************/

#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "espnow.h"
#include "gateway.h"

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
    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "ESP-NOW Gateway Demo");
    ESP_LOGI(TAG, "ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "==================================");

    /*----------------------------------------------------------
     * NVS
     *---------------------------------------------------------*/

    nvs_init();
	
	/*----------------------------------------------------------
     * TCP
     *---------------------------------------------------------*/
	
	ESP_ERROR_CHECK(esp_netif_init());

    /*----------------------------------------------------------
     * WiFi
     *---------------------------------------------------------*/

	 ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_ERROR_CHECK(wifi_init());

    ESP_LOGI(TAG, "Waiting for WiFi...");

    while (!wifi_is_connected())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG,
             "WiFi connected (Channel %u)",
             wifi_get_channel());

    /*----------------------------------------------------------
     * ESP-NOW
     *---------------------------------------------------------*/

    ESP_ERROR_CHECK(
        espnow_init());

    /*----------------------------------------------------------
     * Gateway
     *---------------------------------------------------------*/

    ESP_ERROR_CHECK(
        gateway_init());

    ESP_LOGI(TAG,
             "Gateway Ready");

    /*----------------------------------------------------------
     * Idle
     *---------------------------------------------------------*/

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
