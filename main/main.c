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
#include "core/routing/enp_routing_runtime.h"
#include "core/routing/enp_routing_data_path.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_route_metric.h"
#include "core/network/enp_neighbor.h"

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
 * IG-F.7.8 Application Sink
 *---------------------------------------------------------*/

static esp_err_t
ig_f_7_8_application_init(enp_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "IG-F.7.8 application service initialized");
    return ESP_OK;
}

static esp_err_t
ig_f_7_8_application_process(
    enp_context_t *context,
    const enp_packet_t *packet,
    const enp_transport_address_t *source)
{
    (void)source;

    if ((context == NULL) || (packet == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const enp_header_t *const header =
        enp_packet_header_const(packet);

    if ((header == NULL) ||
        (header->type != ENP_PACKET_APPLICATION)) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * The ENP packet API stores the payload length in the validated header.
     * There is no enp_packet_payload_length() accessor in this codebase.
     */
    const size_t payload_length =
        (size_t)header->payload_length;

    ESP_LOGI(TAG,
             "IG-F.7.8 application delivered: "
             "src=%u/%u dst=%u/%u sequence=%lu payload_length=%u",
             (unsigned)header->source.network,
             (unsigned)header->source.node,
             (unsigned)header->destination.network,
             (unsigned)header->destination.node,
             (unsigned long)header->sequence,
             (unsigned)payload_length);

    return ESP_OK;
}

static const enp_service_t s_ig_f_7_8_application_service = {
    .name = "ig-f.7.8-application",
    .packet_type = ENP_PACKET_APPLICATION,
    .init = ig_f_7_8_application_init,
    .process = ig_f_7_8_application_process,
};

/*----------------------------------------------------------
 * Routing Runtime Integration
 *---------------------------------------------------------*/

 static bool routing_resolve_transport(
     void *context,
     enp_route_destination_t next_hop,
     enp_transport_address_t *transport_address)
 {
     enp_context_t *const enp_context = context;

     if ((enp_context == NULL) || (transport_address == NULL)) {
         ESP_LOGD(TAG,
                  "Transport resolution rejected: invalid context or output");
         return false;
     }

     /*
      * Production routing only resolves transport addresses for
      * neighbours in the local ENP network.
      */
     if (next_hop.network_id != enp_context->network.id) {
         ESP_LOGD(TAG,
                  "Transport resolution rejected: network=%" PRIu16
                  " node=%" PRIu16 " (local network=%" PRIu16 ")",
                  next_hop.network_id,
                  next_hop.node_id,
                  enp_context->network.id);

         return false;
     }

     /*
      * A node must never resolve itself as a transport next hop.
      */
     if (next_hop.node_id == enp_context->network.local.id) {
         ESP_LOGD(TAG,
                  "Transport resolution rejected: node=%" PRIu16
                  " is local node",
                  next_hop.node_id);

         return false;
     }

     const enp_address_t address = {
         .network = (enp_network_id_t)next_hop.network_id,
         .node = (enp_node_id_t)next_hop.node_id,
     };

     /*
      * Transport resolution is permitted only for an existing ACTIVE
      * neighbour. The underlying address lookup API may resolve STALE
      * entries, so lifecycle admission is enforced here.
      */
     const enp_neighbor_t *const neighbor =
         enp_neighbor_find_const(&enp_context->neighbors, &address);

     if (neighbor == NULL) {
         ESP_LOGD(TAG,
                  "Transport resolution rejected: neighbor missing: "
                  "network=%" PRIu16 " node=%" PRIu16,
                  address.network,
                  address.node);

         return false;
     }

     if (neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE) {
         ESP_LOGD(TAG,
                  "Transport resolution rejected: neighbor not ACTIVE: "
                  "network=%" PRIu16 " node=%" PRIu16
                  " state=%d",
                  address.network,
                  address.node,
                  (int)neighbor->state);

         return false;
     }

     const esp_err_t err = enp_neighbor_get_transport_address(
         &enp_context->neighbors,
         &address,
         transport_address);

     if (err != ESP_OK) {
         ESP_LOGD(TAG,
                  "Transport resolution failed: "
                  "network=%" PRIu16 " node=%" PRIu16
                  " err=%s",
                  address.network,
                  address.node,
                  esp_err_to_name(err));

         return false;
     }

     ESP_LOGD(TAG,
              "Transport resolution accepted: "
              "network=%" PRIu16 " node=%" PRIu16
              " state=ACTIVE",
              address.network,
              address.node);

     return true;
 }

static bool routing_next_hop_is_admitted(const enp_context_t *context,
										 enp_route_destination_t next_hop) {
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

	if ((neighbor == NULL) || (neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE)) {
		return false;
	}

	enp_transport_address_t transport_address;

	return enp_neighbor_get_transport_address(
			   (enp_neighbor_table_t *)&context->neighbors, &address,
			   &transport_address) == ESP_OK;
}

static bool routing_select_next_hop(void *context,
									enp_route_destination_t destination,
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
 * The ENP node identifier is owned by Kconfig:
 *
 *     CONFIG_ENP_E3_NODE_ID
 *
 * The build-time device role selects the appropriate
 * node identifier through Kconfig.projbuild.
 *
 * ENP_LOCAL_NETWORK_ID remains a local production constant
 * until network identity is also moved into Kconfig.
 */

#define ENP_LOCAL_NETWORK_ID ((enp_network_id_t)1U)

/*----------------------------------------------------------
 * IG-F.7.8 Gateway Validation
 *---------------------------------------------------------*/

/*
 * Hardware-validation topology:
 *
 *   Gateway node 1  ->  Relay node 2  ->  Sensor node 3
 *
 * This task is compiled only into the Gateway configuration. It waits
 * until Relay node 2 is an ACTIVE neighbour, installs the temporary
 * validation route to node 3 through node 2 in the production runtime
 * route table, submits one application packet through the production
 * routing data path, and then terminates.
 *
 * No production routing, neighbour, maintenance, transport, or node-role
 * behaviour is changed by this validation task.
 */
#if CONFIG_DEVICE_ROLE_GATEWAY

#define IG_F_7_8_NETWORK_ID          ((enp_network_id_t)1U)
#define IG_F_7_8_RELAY_NODE_ID       ((enp_node_id_t)2U)
#define IG_F_7_8_DESTINATION_NODE_ID ((enp_node_id_t)3U)
#define IG_F_7_8_ROUTE_SEQUENCE      (1U)
#define IG_F_7_8_WAIT_TIMEOUT_MS     (30000U)
#define IG_F_7_8_TASK_STACK_SIZE     (4096U)
#define IG_F_7_8_TASK_PRIORITY       (5U)

static StackType_t s_ig_f_7_8_task_stack[IG_F_7_8_TASK_STACK_SIZE];
static StaticTask_t s_ig_f_7_8_task_control;

static bool ig_f_7_8_relay_is_active(void)
{
    const enp_address_t relay_address = {
        .network = IG_F_7_8_NETWORK_ID,
        .node = IG_F_7_8_RELAY_NODE_ID,
    };

    const enp_neighbor_t *const relay =
        enp_neighbor_find_const(&s_context.neighbors, &relay_address);

    return (relay != NULL) &&
           (relay->state == ENP_NEIGHBOR_STATE_ACTIVE);
}

static bool ig_f_7_8_install_validation_route(void)
{
    enp_route_table_t *const routes =
        enp_routing_runtime_route_table();

    if (routes == NULL) {
        return false;
    }

    enp_route_entry_t entry = {0};

    entry.destination = (enp_route_destination_t) {
        .network_id = IG_F_7_8_NETWORK_ID,
        .node_id = IG_F_7_8_DESTINATION_NODE_ID,
    };

    entry.next_hop = (enp_route_destination_t) {
        .network_id = IG_F_7_8_NETWORK_ID,
        .node_id = IG_F_7_8_RELAY_NODE_ID,
    };

    entry.route_sequence = IG_F_7_8_ROUTE_SEQUENCE;
    entry.expires_at_ms =
        enp_context_time_ms(&s_context) + 60000U;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    if (!enp_route_metric_init(
            &entry.metric,
            ENP_ROUTE_METRIC_HOP_COUNT)) {
        return false;
    }

    entry.metric.value = 1U;
    entry.metric.valid = true;

    const enp_route_entry_t *const existing =
        enp_route_table_lookup_const(
            routes,
            entry.destination);

    if (existing != NULL) {
        return enp_route_table_update(routes, &entry);
    }

    return enp_route_table_insert(routes, &entry);
}

static bool ig_f_7_8_make_validation_packet(
        enp_packet_t *packet)
{
    if (packet == NULL) {
        return false;
    }

    const enp_address_t source = {
        .network = s_context.network.id,
        .node = s_context.network.local.id,
    };

    enp_packet_init(
        packet,
        ENP_PACKET_APPLICATION,
        &source);

    enp_header_t *const header =
        enp_packet_header(packet);

    if (header == NULL) {
        return false;
    }

    header->destination.network = IG_F_7_8_NETWORK_ID;
    header->destination.node = IG_F_7_8_DESTINATION_NODE_ID;
    header->sequence = 0xF7080001U;

    static const uint8_t payload[] =
        "IG-F.7.8-GATEWAY-TO-SENSOR";

    memcpy(
        enp_packet_payload(packet),
        payload,
        sizeof(payload) - 1U);

    return enp_packet_seal(
               packet,
               sizeof(payload) - 1U) == ESP_OK;
}

static void ig_f_7_8_validation_task(void *argument)
{
    (void)argument;

    ESP_LOGI(TAG,
             "IG-F.7.8: waiting for relay network=%u node=%u",
             (unsigned)IG_F_7_8_NETWORK_ID,
             (unsigned)IG_F_7_8_RELAY_NODE_ID);

    const TickType_t start_tick =
        xTaskGetTickCount();

    while (!ig_f_7_8_relay_is_active()) {
        if ((xTaskGetTickCount() - start_tick) >=
            pdMS_TO_TICKS(IG_F_7_8_WAIT_TIMEOUT_MS)) {
            ESP_LOGE(TAG,
                     "IG-F.7.8: relay neighbour did not become ACTIVE");
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    ESP_LOGI(TAG,
             "IG-F.7.8: relay neighbour ACTIVE");

    if (!ig_f_7_8_install_validation_route()) {
        ESP_LOGE(TAG,
                 "IG-F.7.8: failed to install validation route");
        vTaskDelete(NULL);
        return;
    }

    const enp_route_destination_t destination = {
        .network_id = IG_F_7_8_NETWORK_ID,
        .node_id = IG_F_7_8_DESTINATION_NODE_ID,
    };

    enp_route_table_t *const routes =
        enp_routing_runtime_route_table();

    const enp_route_entry_t *const route =
        (routes != NULL)
            ? enp_route_table_lookup_const(routes, destination)
            : NULL;

    if ((route == NULL) ||
        (route->state != ENP_ROUTE_STATE_ACTIVE)) {
        ESP_LOGE(TAG,
                 "IG-F.7.8: validation route not ACTIVE");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG,
             "IG-F.7.8: validation route ACTIVE destination=%u next-hop=%u",
             (unsigned)route->destination.node_id,
             (unsigned)route->next_hop.node_id);

    enp_packet_t packet = {0};

    if (!ig_f_7_8_make_validation_packet(&packet)) {
        ESP_LOGE(TAG,
                 "IG-F.7.8: failed to create validation packet");
        vTaskDelete(NULL);
        return;
    }

    enp_routing_data_path_t *const routing_path =
        enp_routing_runtime_data_path();

    if (routing_path == NULL) {
        ESP_LOGE(TAG,
                 "IG-F.7.8: routing data path unavailable");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG,
             "IG-F.7.8 ACTIVE: submitting destination=%u",
             (unsigned)IG_F_7_8_DESTINATION_NODE_ID);

    const esp_err_t submit_err =
        enp_routing_data_path_submit(
            routing_path,
            &packet);

    if (submit_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "IG-F.7.8 ACTIVE: submit failed: %s",
                 esp_err_to_name(submit_err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG,
             "IG-F.7.8 ACTIVE: submit accepted");

    vTaskDelete(NULL);
}

static void ig_f_7_8_start_validation_task(void)
{
    TaskHandle_t const task =
        xTaskCreateStatic(
            ig_f_7_8_validation_task,
            "ig_f_7_8",
            IG_F_7_8_TASK_STACK_SIZE,
            NULL,
            IG_F_7_8_TASK_PRIORITY,
            s_ig_f_7_8_task_stack,
            &s_ig_f_7_8_task_control);

    if (task == NULL) {
        ESP_LOGE(TAG,
                 "IG-F.7.8: failed to create Gateway validation task");
    } else {
        ESP_LOGI(TAG,
                 "IG-F.7.8: Gateway validation task started");
    }
}

#endif /* CONFIG_DEVICE_ROLE_GATEWAY */

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
 
 static void enp_receive_callback(
        const enp_transport_address_t *source,
        const void *data,
        size_t length)
{
    if ((source == NULL) || (data == NULL) || (length == 0U)) {
        return;
    }

    if (length > sizeof(enp_packet_t)) {
        ESP_LOGW(TAG, "Received frame too large: %u", (unsigned)length);
        return;
    }

    enp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    memcpy(enp_packet_data(&packet), data, length);

    const enp_header_t *const header = enp_packet_header_const(&packet);
    if (header == NULL) {
        ESP_LOGW(TAG, "Received ENP packet without a header");
        return;
    }

    ESP_LOGI(TAG,
             "RX header: type=%u src=%u/%u dst=%u/%u local=%u/%u ttl=%u",
             (unsigned)header->type,
             (unsigned)header->source.network,
             (unsigned)header->source.node,
             (unsigned)header->destination.network,
             (unsigned)header->destination.node,
             (unsigned)s_context.network.id,
             (unsigned)s_context.network.local.id,
             (unsigned)header->ttl);

    const bool is_application =
        (header->type == ENP_PACKET_APPLICATION);
    const bool is_local_network =
        (header->destination.network == s_context.network.id);
    const bool is_non_local =
        (header->destination.node != s_context.network.local.id);

    ESP_LOGI(TAG,
             "RX forwarding decision: application=%d local_network=%d non_local=%d",
             (int)is_application,
             (int)is_local_network,
             (int)is_non_local);

    if (is_application && is_local_network && is_non_local) {
        enp_routing_data_path_t *const routing_path =
            enp_routing_runtime_data_path();

        if (routing_path == NULL) {
            ESP_LOGW(TAG,
                     "Routing path unavailable: destination=%u/%u",
                     (unsigned)header->destination.network,
                     (unsigned)header->destination.node);
            return;
        }

        ESP_LOGI(TAG,
                 "Forwarding application packet: source=%u/%u destination=%u/%u ttl=%u",
                 (unsigned)header->source.network,
                 (unsigned)header->source.node,
                 (unsigned)header->destination.network,
                 (unsigned)header->destination.node,
                 (unsigned)header->ttl);

        /*
         * IG-F.7.8 forwarding diagnostics.
         *
         * Read-only inspection immediately before the authoritative
         * forwarding call. This distinguishes:
         *
         *   (A) no ACTIVE route for the logical destination
         *   (B) an ACTIVE route whose next hop cannot be resolved to an
         *       admissible transport address
         *
         * No route, neighbour, packet, or transport state is modified.
         */
        enp_route_table_t *const route_table =
            enp_routing_runtime_route_table();

        const enp_route_destination_t destination = {
            .network_id = header->destination.network,
            .node_id = header->destination.node,
        };

        const enp_route_entry_t *const diagnostic_route =
            (route_table != NULL)
                ? enp_route_table_lookup_const(route_table, destination)
                : NULL;

        if (diagnostic_route == NULL) {
            ESP_LOGW(TAG,
                     "Forward diagnostic: route missing destination=%u/%u",
                     (unsigned)destination.network_id,
                     (unsigned)destination.node_id);
        } else {
            ESP_LOGI(TAG,
                     "Forward diagnostic: route destination=%u/%u "
                     "next-hop=%u/%u state=%d",
                     (unsigned)diagnostic_route->destination.network_id,
                     (unsigned)diagnostic_route->destination.node_id,
                     (unsigned)diagnostic_route->next_hop.network_id,
                     (unsigned)diagnostic_route->next_hop.node_id,
                     (int)diagnostic_route->state);

            if (diagnostic_route->state == ENP_ROUTE_STATE_ACTIVE) {
                const enp_address_t next_hop_address = {
                    .network =
                        (enp_network_id_t)diagnostic_route->next_hop.network_id,
                    .node =
                        (enp_node_id_t)diagnostic_route->next_hop.node_id,
                };

                const enp_neighbor_t *const next_hop_neighbor =
                    enp_neighbor_find_const(
                        &s_context.neighbors,
                        &next_hop_address);

                if (next_hop_neighbor == NULL) {
                    ESP_LOGW(TAG,
                             "Forward diagnostic: next-hop neighbor missing "
                             "next-hop=%u/%u",
                             (unsigned)diagnostic_route->next_hop.network_id,
                             (unsigned)diagnostic_route->next_hop.node_id);
                } else if (next_hop_neighbor->state !=
                           ENP_NEIGHBOR_STATE_ACTIVE) {
                    ESP_LOGW(TAG,
                             "Forward diagnostic: next-hop neighbor not ACTIVE "
                             "next-hop=%u/%u state=%d",
                             (unsigned)diagnostic_route->next_hop.network_id,
                             (unsigned)diagnostic_route->next_hop.node_id,
                             (int)next_hop_neighbor->state);
                } else {
                    enp_transport_address_t diagnostic_transport_address;

                    const bool transport_resolved =
                        routing_resolve_transport(
                            &s_context,
                            diagnostic_route->next_hop,
                            &diagnostic_transport_address);

                    ESP_LOGI(TAG,
                             "Forward diagnostic: next-hop transport resolution "
                             "next-hop=%u/%u resolved=%d",
                             (unsigned)diagnostic_route->next_hop.network_id,
                             (unsigned)diagnostic_route->next_hop.node_id,
                             (int)transport_resolved);
                }
            } else {
                ESP_LOGW(TAG,
                         "Forward diagnostic: route is not ACTIVE "
                         "destination=%u/%u state=%d",
                         (unsigned)diagnostic_route->destination.network_id,
                         (unsigned)diagnostic_route->destination.node_id,
                         (int)diagnostic_route->state);
            }
        }

        const esp_err_t forward_err =
            enp_routing_data_path_forward(routing_path, &packet);

        if (forward_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Packet forwarding failed: %s",
                     esp_err_to_name(forward_err));
        } else {
            ESP_LOGI(TAG,
                     "Packet forwarded: destination=%u/%u",
                     (unsigned)header->destination.network,
                     (unsigned)header->destination.node);
        }
        return;
    }

    const esp_err_t err = enp_dispatcher_dispatch(&packet, source);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Packet dispatch failed: %s",
                 esp_err_to_name(err));
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

	/*
	 * Single authoritative source of the local ENP node ID.
	 *
	 * Kconfig.projbuild derives CONFIG_ENP_E3_NODE_ID from
	 * the selected device role.
	 */
	config.node_id = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID;

#if CONFIG_DEVICE_ROLE_GATEWAY

	config.role = ENP_ROLE_GATEWAY;

#elif CONFIG_DEVICE_ROLE_RELAY

	config.role = ENP_ROLE_RELAY;

#elif CONFIG_DEVICE_ROLE_SENSOR

	config.role = ENP_ROLE_SENSOR;

#else

	/*
	 * Fail closed: an invalid build-time role must not
	 * silently become a valid ENP role.
	 */
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

	ESP_LOGE(TAG,
			 "Device role: UNKNOWN (invalid build-time role configuration)");
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

	ESP_ERROR_CHECK(enp_dispatcher_register(&s_ig_f_7_8_application_service));

	ESP_LOGI(TAG, "IG-F.7.8 application service registered");

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

#if CONFIG_DEVICE_ROLE_GATEWAY
	ig_f_7_8_start_validation_task();
#endif

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