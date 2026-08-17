/*
 * wifi.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

#include "../link/enp_transport_wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "WIFI";

static bool s_connected = false;
static uint8_t s_channel = 0;

/*------------------------------------------------------------------
 * Event Handler
 *-----------------------------------------------------------------*/

static void enp_wifi_event_handler(void *arg, esp_event_base_t event_base,
								   int32_t event_id, void *event_data) {
	(void)arg;

	if (event_base == WIFI_EVENT) {
		switch (event_id) {
		case WIFI_EVENT_STA_START:

			ESP_LOGI(TAG, "Connecting...");

			ESP_ERROR_CHECK(esp_wifi_connect());

			break;

		case WIFI_EVENT_STA_DISCONNECTED:

			s_connected = false;

			ESP_LOGW(TAG, "Disconnected");

			ESP_ERROR_CHECK(esp_wifi_connect());

			break;

		default:
			break;
		}
	}

	if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

		wifi_second_chan_t second;

		ESP_ERROR_CHECK(esp_wifi_get_channel(&s_channel, &second));

		s_connected = true;

		ESP_LOGI(TAG, "Connected");

		ESP_LOGI(TAG, "IP Address : " IPSTR, IP2STR(&event->ip_info.ip));

		ESP_LOGI(TAG, "Channel    : %u", s_channel);
	}
}

/*------------------------------------------------------------------
 * Public
 *-----------------------------------------------------------------*/

esp_err_t enp_wifi_init(void) {

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_netif_create_default_wifi_sta();

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
											   enp_wifi_event_handler, NULL));

	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
											   enp_wifi_event_handler, NULL));

	wifi_config_t wifi_cfg = {0};

	strncpy((char *)wifi_cfg.sta.ssid, CONFIG_ESP_WIFI_SSID,
			sizeof(wifi_cfg.sta.ssid) - 1);

	strncpy((char *)wifi_cfg.sta.password, CONFIG_ESP_WIFI_PASSWORD,
			sizeof(wifi_cfg.sta.password) - 1);

	wifi_cfg.sta.scan_method = WIFI_FAST_SCAN;

	wifi_cfg.sta.failure_retry_cnt = 5;

	wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	wifi_cfg.sta.pmf_cfg.capable = true;
	wifi_cfg.sta.pmf_cfg.required = false;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

	ESP_ERROR_CHECK(esp_wifi_start());

	return ESP_OK;
}

bool enp_wifi_is_connected(void) { return s_connected; }

uint8_t enp_wifi_get_channel(void) { return s_channel; }