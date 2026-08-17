/**
 * @file main.c
 *
 * @brief ENP v0.2 application entry point.
 *
 * Target:
 *     ESP-IDF 6.0.2
 *
 * Current ENP services:
 *     Discovery
 *
 * Current transport:
 *     ESP-NOW
 */

#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/enp_config.h"
#include "config/enp_defaults.h"

#include "core/enp_context.h"
#include "core/enp_maintenance.h"
#include "core/enp_transport.h"

#include "core/dispatcher/enp_dispatcher.h"

#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_data.h"

#include "core/service/discovery/enp_service_discovery.h"

#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

/*----------------------------------------------------------
 * Logging
 *---------------------------------------------------------*/

static const char *TAG = "MAIN";

/*----------------------------------------------------------
 * ENP Runtime
 *---------------------------------------------------------*/

static enp_context_t s_context;

/*----------------------------------------------------------
 * Local Configuration
 *---------------------------------------------------------*/

/*
 * ENP v0.2 currently has no Kconfig entries for the
 * logical network and node identifiers.
 *
 * Keep these values explicit until the configuration layer
 * is extended.
 */
#define ENP_LOCAL_NETWORK_ID ((enp_network_id_t)1U)

#define ENP_GATEWAY_NODE_ID ((enp_node_id_t)1U)
#define ENP_SENSOR_NODE_ID ((enp_node_id_t)2U)

/*----------------------------------------------------------
 * Transport Receive Callback
 *---------------------------------------------------------*/

/**
 * @brief ENP transport receive callback.
 *
 * The transport delivers raw bytes and the transport source.
 *
 * The callback copies the received frame into an ENP packet
 * object before passing it to the dispatcher.
 *
 * The ESP-NOW transport already moves the data out of the
 * ESP-NOW callback context and into its static worker task.
 */
static void enp_receive_callback(const enp_transport_address_t *source,
								 const void *data, size_t length) {
	if ((source == NULL) || (data == NULL) || (length == 0U)) {
		return;
	}

	if (length > sizeof(enp_packet_t)) {
		ESP_LOGW(TAG, "Received frame too large: %u", (unsigned)length);

		return;
	}

	/*
	 * The packet API represents a complete ENP frame in a
	 * fixed-size buffer.
	 */
	enp_packet_t packet;

	memset(&packet, 0, sizeof(packet));

	memcpy(enp_packet_data(&packet), data, length);

	/*
	 * The dispatcher performs the complete ENP validation
	 * before passing the packet to a service.
	 */
	const esp_err_t err = enp_dispatcher_dispatch(&packet, source);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Packet dispatch failed: %s", esp_err_to_name(err));
	}
}

/*----------------------------------------------------------
 * NVS
 *---------------------------------------------------------*/

static void nvs_init(void) {
	esp_err_t err = nvs_flash_init();

	if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
		(err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		ESP_ERROR_CHECK(nvs_flash_erase());

		err = nvs_flash_init();
	}

	ESP_ERROR_CHECK(err);
}

/*----------------------------------------------------------
 * ENP Configuration
 *---------------------------------------------------------*/

static enp_config_t enp_create_config(void) {
	enp_config_t config;

	memset(&config, 0, sizeof(config));

	config.network_id = ENP_LOCAL_NETWORK_ID;

#if CONFIG_DEVICE_ROLE_GATEWAY

	config.node_id = ENP_GATEWAY_NODE_ID;

	config.role = ENP_ROLE_GATEWAY;

#else

	config.node_id = ENP_SENSOR_NODE_ID;

	config.role = ENP_ROLE_SENSOR;

#endif

	return config;
}

/*----------------------------------------------------------
 * E3.3.1 DATA wire self-test
 *---------------------------------------------------------*/

static bool e3_3_1_data_self_test(void) {
	unsigned pass_count = 0U;
	unsigned fail_count = 0U;
	enp_data_header_t header;

#define E3_DATA_PASS(msg)                                                      \
	do {                                                                       \
		ESP_LOGI(TAG, "PASS: %s", (msg));                                      \
		++pass_count;                                                          \
	} while (0)

#define E3_DATA_FAIL(msg)                                                      \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", (msg));                                      \
		++fail_count;                                                          \
	} while (0)

#define E3_DATA_CHECK(condition, msg)                                          \
	do {                                                                       \
		if (condition) {                                                       \
			E3_DATA_PASS(msg);                                                 \
		} else {                                                               \
			E3_DATA_FAIL(msg);                                                 \
		}                                                                      \
	} while (0)

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.1 ENP DATA WIRE SELF-TEST");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	E3_DATA_CHECK(ENP_DATA_HEADER_SIZE == 12U, "DATA header size is 12 bytes");

	enp_data_header_init(&header, ENP_DATA_SUBTYPE_APPLICATION,
						 ENP_DATA_FLAG_NONE, 1U, 7U);

	E3_DATA_CHECK(header.payload_version == ENP_DATA_PAYLOAD_VERSION,
				  "DATA payload version initialized");

	E3_DATA_CHECK(header.subtype == ENP_DATA_SUBTYPE_APPLICATION,
				  "DATA subtype initialized");

	E3_DATA_CHECK(header.flags == ENP_DATA_FLAG_NONE, "DATA flags initialized");

	E3_DATA_CHECK(header.application_sequence == 1U,
				  "application sequence initialized");

	E3_DATA_CHECK(header.payload_length == 7U, "payload length initialized");

	E3_DATA_CHECK(header.reserved == 0U, "reserved field initialized to zero");

	E3_DATA_CHECK(enp_data_header_valid(&header), "valid DATA header accepted");

	header.payload_version = 0xFFU;
	E3_DATA_CHECK(!enp_data_header_valid(&header),
				  "invalid DATA version rejected");

	header.payload_version = ENP_DATA_PAYLOAD_VERSION;
	header.subtype = 0xFFU;
	E3_DATA_CHECK(!enp_data_header_valid(&header),
				  "invalid DATA subtype rejected");

	header.subtype = ENP_DATA_SUBTYPE_APPLICATION;
	header.flags = 0x8000U;
	E3_DATA_CHECK(!enp_data_header_valid(&header),
				  "unknown DATA flags rejected");

	header.flags = ENP_DATA_FLAG_NONE;
	header.reserved = 1U;
	E3_DATA_CHECK(!enp_data_header_valid(&header),
				  "non-zero reserved field rejected");

	header.reserved = 0U;
	header.application_sequence = 0U;
	E3_DATA_CHECK(!enp_data_header_valid(&header),
				  "zero application sequence rejected");

	enp_data_header_init(&header, ENP_DATA_SUBTYPE_APPLICATION,
						 ENP_DATA_FLAG_NONE, 1U, 32U);

	E3_DATA_CHECK(enp_data_payload_length_valid(&header, 32U),
				  "matching payload length accepted");

	E3_DATA_CHECK(!enp_data_payload_length_valid(&header, 31U),
				  "short payload rejected");

	E3_DATA_CHECK(!enp_data_payload_length_valid(&header, 33U),
				  "long payload rejected");

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "E3.3.1 DATA self-test PASS: %u", pass_count);
	ESP_LOGI(TAG, "E3.3.1 DATA self-test FAIL: %u", fail_count);

	if (fail_count == 0U) {
		ESP_LOGI(TAG, "ALL E3.3.1 DATA WIRE TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "E3.3.1 DATA WIRE TEST FAILED");
	}

	ESP_LOGI(TAG, "======================================");

#undef E3_DATA_CHECK
#undef E3_DATA_FAIL
#undef E3_DATA_PASS

	return fail_count == 0U;
}

/*----------------------------------------------------------
 * app_main
 *---------------------------------------------------------*/

void app_main(void) {
	if (!e3_3_1_data_self_test()) {
		ESP_LOGE(TAG,
				 "E3.3.1 DATA self-test failed; application startup aborted");
		return;
	}

	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "ENP - ESP Network Protocol");

	ESP_LOGI(TAG, "ENP version: 0.2.0");

	ESP_LOGI(TAG, "ESP-IDF: 6.0.2");

	ESP_LOGI(TAG, "Transport: ESP-NOW");

	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY

	ESP_LOGI(TAG, "Device role: GATEWAY");

#else

	ESP_LOGI(TAG, "Device role: SENSOR");

#endif

	/*------------------------------------------------------
	 * Platform initialization
	 *-----------------------------------------------------*/

	nvs_init();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	/*------------------------------------------------------
	 * Wi-Fi initialization
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_wifi_init());

	ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

	while (!enp_wifi_is_connected()) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	ESP_LOGI(TAG, "Wi-Fi connected");

	ESP_LOGI(TAG, "Wi-Fi channel: %u", (unsigned)enp_wifi_get_channel());

	/*------------------------------------------------------
	 * ENP configuration
	 *-----------------------------------------------------*/

	const enp_config_t config = enp_create_config();

	ESP_LOGI(TAG, "ENP network ID: %u", (unsigned)config.network_id);

	ESP_LOGI(TAG, "ENP node ID: %lu", (unsigned long)config.node_id);

	/*------------------------------------------------------
	 * Transport
	 *-----------------------------------------------------*/

	enp_transport_t *transport = enp_transport_espnow_get();

	if (transport == NULL) {
		ESP_LOGE(TAG, "Failed to obtain ESP-NOW transport");

		return;
	}

	/*
	 * enp_context_init() initializes the supplied transport.
	 */
	ESP_ERROR_CHECK(enp_context_init(&s_context, transport, &config));

	ESP_LOGI(TAG, "ENP context initialized");

	/*------------------------------------------------------
	 * Dispatcher
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_dispatcher_init(&s_context));

	ESP_LOGI(TAG, "ENP dispatcher initialized");

	/*------------------------------------------------------
	 * Services
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_dispatcher_register(enp_service_discovery_get()));

	ESP_LOGI(TAG, "Discovery service registered");

	/*------------------------------------------------------
	 * Transport receive path
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_transport_set_receive_callback(s_context.transport,
													   enp_receive_callback));

	ESP_LOGI(TAG, "ENP receive path configured");

	/*------------------------------------------------------
	 * Initial Discovery
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_service_discovery_send(&s_context));

	ESP_LOGI(TAG, "Initial discovery announcement sent");

	/*------------------------------------------------------
	 * Periodic maintenance
	 *-----------------------------------------------------*/

	ESP_ERROR_CHECK(enp_maintenance_init(&s_context));

	/*------------------------------------------------------
	 * Startup complete
	 *-----------------------------------------------------*/

	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "ENP started successfully");

	ESP_LOGI(TAG, "======================================");

	/*
	 * app_main() is not required to remain alive.
	 *
	 * ENP's ESP-NOW transport owns its own static worker
	 * task, so the main task can terminate.
	 */
	vTaskDelete(NULL);
}