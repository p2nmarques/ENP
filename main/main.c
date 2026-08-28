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
#include "core/routing/enp_routing_runtime.h"
#include "core/enp_maintenance.h"
#include "core/enp_transport.h"

#include "core/dispatcher/enp_dispatcher.h"

#include "core/protocol/enp_packet.h"

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
 * Routing Runtime Integration
 *---------------------------------------------------------*/

static bool routing_resolve_transport(
    void *context, enp_route_destination_t next_hop,
    enp_transport_address_t *transport_address) {
    enp_context_t *const enp_context = context;

    if ((enp_context == NULL) || (transport_address == NULL)) {
        return false;
    }

    if (next_hop.network_id != enp_context->network.id) {
        return false;
    }

    const enp_address_t address = {
        .network = (enp_network_id_t)next_hop.network_id,
        .node = (enp_node_id_t)next_hop.node_id,
    };

    return enp_neighbor_get_transport_address(
               &enp_context->neighbors, &address, transport_address) == ESP_OK;
}

static bool routing_next_hop_is_admitted(
    const enp_context_t *context, enp_route_destination_t next_hop) {
    if (context == NULL) {
        return false;
    }

    if (next_hop.network_id != context->network.id) {
        return false;
    }

    if (next_hop.node_id == context->network.local.id) {
        return false;
    }

    const enp_address_t address = {
        .network = (enp_network_id_t)next_hop.network_id,
        .node = (enp_node_id_t)next_hop.node_id,
    };

    const enp_neighbor_t *const neighbor =
        enp_neighbor_find_const(&context->neighbors, &address);

    if ((neighbor == NULL) ||
        (neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE)) {
        return false;
    }

    enp_transport_address_t transport_address;

    return enp_neighbor_get_transport_address(
               (enp_neighbor_table_t *)&context->neighbors, &address,
               &transport_address) == ESP_OK;
}

static bool routing_select_next_hop(
    void *context, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop,
    enp_route_destination_t *next_hop) {
    const enp_context_t *const enp_context = context;

    if ((enp_context == NULL) || (next_hop == NULL)) {
        return false;
    }

    if ((destination.network_id == failed_next_hop.network_id) &&
        (destination.node_id == failed_next_hop.node_id)) {
        return false;
    }

    if (!routing_next_hop_is_admitted(enp_context, destination)) {
        return false;
    }

    *next_hop = destination;

    return true;
}

static uint32_t routing_now_ms(void *context) {
    const enp_context_t *const enp_context = context;

    if (enp_context == NULL) {
        return 0U;
    }

    return enp_context_time_ms(enp_context);
}

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
#define ENP_RELAY_NODE_ID   ((enp_node_id_t)2U)
#define ENP_SENSOR_NODE_ID  ((enp_node_id_t)3U)

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

#elif CONFIG_DEVICE_ROLE_RELAY

	config.node_id = ENP_RELAY_NODE_ID;
	config.role = ENP_ROLE_RELAY;

#elif CONFIG_DEVICE_ROLE_SENSOR

	config.node_id = ENP_SENSOR_NODE_ID;
	config.role = ENP_ROLE_SENSOR;

#else

	/* Fail closed: an invalid build-time role must not silently become a sensor. */
	config.node_id = (enp_node_id_t)0U;
	config.role = ENP_ROLE_UNKNOWN;

#endif

	return config;
}

/*----------------------------------------------------------
 * app_main
 *---------------------------------------------------------*/

void app_main(void) {
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "ENP - ESP Network Protocol");

	ESP_LOGI(TAG, "ENP version: 0.2.0");

	ESP_LOGI(TAG, "ESP-IDF: 6.0.2");

	ESP_LOGI(TAG, "Transport: ESP-NOW");

	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY

	ESP_LOGI(TAG, "Device role: GATEWAY");

#elif CONFIG_DEVICE_ROLE_RELAY

	ESP_LOGI(TAG, "Device role: RELAY");

#elif CONFIG_DEVICE_ROLE_SENSOR

	ESP_LOGI(TAG, "Device role: SENSOR");

#else

	ESP_LOGE(TAG, "Device role: UNKNOWN (invalid build-time role configuration)");
	return;

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
	 * Routing runtime
	 *-----------------------------------------------------*/

	const enp_address_t routing_local_address = {
		.network = s_context.network.id,
		.node = s_context.network.local.id,
	};

	const enp_routing_runtime_config_t routing_config = {
		.transport = s_context.transport,
		.local_address = routing_local_address,
		.select_next_hop = routing_select_next_hop,
		.select_next_hop_context = &s_context,
		.resolve_transport = routing_resolve_transport,
		.resolve_transport_context = &s_context,
		.now_ms = routing_now_ms,
		.now_ms_context = &s_context,
	};

	if (!enp_routing_runtime_init(&routing_config)) {
		ESP_LOGE(TAG, "Routing runtime initialization failed");

		return;
	}

	ESP_LOGI(TAG, "Routing runtime initialized");

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