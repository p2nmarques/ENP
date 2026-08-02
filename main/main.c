#include "freertos/idf_additions.h"
#include "wifi.h"
#include "espnow.h"
#include "gateway.h"

void app_main(void)
{
    nvs_init();

    wifi_init();

    while (!wifi_is_connected())
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_ERROR_CHECK(espnow_init());

    gateway_init();

    ESP_LOGI(TAG, "Gateway Ready");

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
