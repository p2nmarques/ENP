/*
 * E3.3.7_p4_E4D_test_enp_production_runtime_hardware_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7_p4_E4D_test_enp_production_runtime_hardware_main.c
 *
 * E3.3.7 Phase 4 / P4-E4D
 * Production runtime hardware validation.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Topology:
 *     Node 1 (Gateway) <---- real ESP-NOW ----> Node 2 (Sensor)
 *
 * This test validates the already-frozen P4-E4C production composition on
 * real ESP32 hardware. It deliberately does not introduce a new ENP API.
 *
 * The test exercises:
 *     real Wi-Fi/ESP-NOW initialization
 *     production ENP context
 *     production dispatcher
 *     Discovery service and neighbor table
 *     production route/data path
 *     production receive path and transport callback
 *     DATA local delivery through the data plane
 *     ACK local delivery through the data plane
 *
 * Hardware validation is PASS only when the relevant observations are made
 * on real target hardware.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/enp_config.h"

#include "core/enp_context.h"
#include "core/enp_maintenance.h"
#include "core/enp_receive_path.h"
#include "core/enp_transport.h"

#include "core/dispatcher/enp_dispatcher.h"

#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_ack.h"
#include "core/protocol/payloads/enp_data.h"

#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"

#include "core/service/discovery/enp_service_discovery.h"
#include "core/service/enp_service.h"

#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

static const char *TAG = "E3_3_7_P4_E4D";

#define TEST_NETWORK_ID ((enp_network_id_t)1U)
#define TEST_GATEWAY_NODE_ID ((enp_node_id_t)1U)
#define TEST_SENSOR_NODE_ID ((enp_node_id_t)2U)

#define TEST_DATA_SEQUENCE ((enp_sequence_t)0xE4D1U)
#define TEST_APP_SEQUENCE ((uint32_t)0x0000E4D1U)
#define TEST_ACK_SEQUENCE ((enp_sequence_t)0xE4D2U)

#define TEST_DATA_PAYLOAD_LEN 4U
#define TEST_DISCOVERY_WAIT_MS 8000U
#define TEST_RESULT_WAIT_MS 5000U

static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_routing_data_path_t s_routing_path;
static enp_receive_path_t s_receive_path;

static volatile unsigned s_data_received = 0U;
static volatile unsigned s_ack_received = 0U;
static volatile bool s_data_valid = false;
static volatile bool s_ack_valid = false;
static volatile bool s_test_done = false;

static void nvs_init(void) {
	esp_err_t err = nvs_flash_init();

	if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
		(err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}

	ESP_ERROR_CHECK(err);
}

static enp_config_t make_config(void) {
#if CONFIG_DEVICE_ROLE_GATEWAY
	return (enp_config_t){.network_id = TEST_NETWORK_ID,
						  .node_id = TEST_GATEWAY_NODE_ID,
						  .role = ENP_ROLE_GATEWAY};
#else
	return (enp_config_t){.network_id = TEST_NETWORK_ID,
						  .node_id = TEST_SENSOR_NODE_ID,
						  .role = ENP_ROLE_SENSOR};
#endif
}

static enp_address_t make_address(enp_node_id_t node) {
	return (enp_address_t){.network = TEST_NETWORK_ID, .node = node};
}

static bool resolve_transport(void *context, enp_route_destination_t next_hop,
							  enp_transport_address_t *transport_address) {
	enp_context_t *ctx = (enp_context_t *)context;

	if ((ctx == NULL) || (transport_address == NULL)) {
		return false;
	}

	const enp_address_t logical = {.network = next_hop.network_id,
								   .node = next_hop.node_id};

	return enp_neighbor_get_transport_address(&ctx->neighbors, &logical,
											  transport_address) == ESP_OK;
}

static esp_err_t
application_service_process(enp_context_t *context, const enp_packet_t *packet,
							const enp_transport_address_t *source) {
	(void)context;

	if ((packet == NULL) || (source == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(packet);
	const enp_data_header_t *data_header =
		(const enp_data_header_t *)enp_packet_payload_const(packet);

	if ((header == NULL) || (data_header == NULL)) {
		return ESP_ERR_INVALID_STATE;
	}

	const enp_address_t expected_source = make_address(TEST_GATEWAY_NODE_ID);
	const enp_address_t expected_destination =
		make_address(TEST_SENSOR_NODE_ID);

	if ((header->type != (uint8_t)ENP_PACKET_APPLICATION) ||
		(header->sequence != TEST_DATA_SEQUENCE) ||
		!enp_address_equal(&header->source, &expected_source) ||
		!enp_address_equal(&header->destination, &expected_destination) ||
		((header->flags & ENP_FLAG_ACK_REQUIRED) == 0U) ||
		(data_header->application_sequence != TEST_APP_SEQUENCE)) {
		ESP_LOGE(TAG, "FAIL: local DATA identity/content mismatch");
		return ESP_ERR_INVALID_ARG;
	}

	const uint8_t expected[TEST_DATA_PAYLOAD_LEN] = {0xE4U, 0xD0U, 0x00U,
													 0x01U};

	if (!enp_data_header_valid(data_header) ||
		!enp_data_payload_length_valid(data_header, TEST_DATA_PAYLOAD_LEN) ||
		memcmp(((const uint8_t *)data_header) + ENP_DATA_HEADER_SIZE, expected,
			   sizeof(expected)) != 0) {
		ESP_LOGE(TAG, "FAIL: local DATA payload validation");
		return ESP_ERR_INVALID_ARG;
	}

	++s_data_received;
	s_data_valid = true;

	ESP_LOGI(TAG, "PASS: real ESP-NOW DATA reached local application service");

	/*
	 * Send the correlated ACK back through the real transport. The ACK is
	 * subsequently received by the sender and traverses its production
	 * receive path and ACK data-plane domain.
	 */
	enp_packet_t ack;
	const enp_address_t ack_source = make_address(TEST_SENSOR_NODE_ID);

	enp_packet_init(&ack, ENP_PACKET_ACK, &ack_source);

	enp_header_t *ack_header = enp_packet_header(&ack);
	enp_ack_payload_t *ack_payload =
		(enp_ack_payload_t *)enp_packet_payload(&ack);

	if ((ack_header == NULL) || (ack_payload == NULL)) {
		return ESP_ERR_INVALID_STATE;
	}

	ack_header->destination = make_address(TEST_GATEWAY_NODE_ID);
	ack_header->flags = ENP_FLAG_NONE;
	ack_header->ttl = 8U;
	ack_header->sequence = TEST_ACK_SEQUENCE;

	enp_ack_payload_init(ack_payload, TEST_DATA_SEQUENCE, TEST_APP_SEQUENCE);

	if (enp_packet_seal(&ack, ENP_ACK_WIRE_SIZE) != ESP_OK) {
		return ESP_FAIL;
	}

	const esp_err_t send_err = enp_transport_send(s_context.transport, source,
												  enp_packet_data_const(&ack),
												  enp_packet_length(&ack));

	if (send_err == ESP_OK) {
		ESP_LOGI(TAG, "PASS: correlated ACK transmitted through real ESP-NOW");
	}

	return send_err;
}

static esp_err_t ack_service_process(enp_context_t *context,
									 const enp_packet_t *packet,
									 const enp_transport_address_t *source) {
	(void)context;
	(void)source;

	if (packet == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(packet);
	const enp_ack_payload_t *ack =
		(const enp_ack_payload_t *)enp_packet_payload_const(packet);

	if ((header == NULL) || (ack == NULL) ||
		(header->type != (uint8_t)ENP_PACKET_ACK) ||
		(header->sequence != TEST_ACK_SEQUENCE) ||
		(ack->data_packet_sequence != TEST_DATA_SEQUENCE) ||
		(ack->application_sequence != TEST_APP_SEQUENCE) ||
		!enp_ack_payload_valid(ack)) {
		ESP_LOGE(TAG, "FAIL: local ACK identity/correlation mismatch");
		return ESP_ERR_INVALID_ARG;
	}

	++s_ack_received;
	s_ack_valid = true;
	s_test_done = true;

	ESP_LOGI(TAG, "PASS: real ESP-NOW ACK reached local application service");

	return ESP_OK;
}

static const enp_service_t s_application_service = {
	.name = "p4_e4d_application",
	.packet_type = ENP_PACKET_APPLICATION,
	.init = NULL,
	.process = application_service_process};

static const enp_service_t s_ack_service = {.name = "p4_e4d_ack",
											.packet_type = ENP_PACKET_ACK,
											.init = NULL,
											.process = ack_service_process};

static bool init_platform(void) {
	nvs_init();

	if (esp_netif_init() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: esp_netif_init");
		return false;
	}

	if (esp_event_loop_create_default() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: esp_event_loop_create_default");
		return false;
	}

	if (enp_wifi_init() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: enp_wifi_init");
		return false;
	}

	while (!enp_wifi_is_connected()) {
		vTaskDelay(pdMS_TO_TICKS(100U));
	}

	ESP_LOGI(TAG, "PASS: Wi-Fi connected, channel=%u",
			 (unsigned)enp_wifi_get_channel());

	return true;
}

static bool install_gateway_route(void) {
#if CONFIG_DEVICE_ROLE_GATEWAY
	const enp_route_entry_t entry = {
		.destination = {.network_id = TEST_NETWORK_ID,
						.node_id = TEST_SENSOR_NODE_ID},
		.next_hop = {.network_id = TEST_NETWORK_ID,
					 .node_id = TEST_SENSOR_NODE_ID},
		.metric = {.valid = true,
				   .type = ENP_ROUTE_METRIC_HOP_COUNT,
				   .value = 1U},
		.route_sequence = 1U,
		.expires_at_ms = UINT32_MAX,
		.state = ENP_ROUTE_STATE_ACTIVE};

	return enp_route_table_insert(&s_routes, &entry);
#else
	return true;
#endif
}

static bool make_data_packet(enp_packet_t *packet) {
	if (packet == NULL) {
		return false;
	}

	const enp_address_t data_source = make_address(TEST_GATEWAY_NODE_ID);

	enp_packet_init(packet, ENP_PACKET_APPLICATION, &data_source);

	enp_header_t *header = enp_packet_header(packet);
	enp_data_header_t *data_header =
		(enp_data_header_t *)enp_packet_payload(packet);

	if ((header == NULL) || (data_header == NULL)) {
		return false;
	}

	header->destination = make_address(TEST_SENSOR_NODE_ID);
	header->flags = ENP_FLAG_ACK_REQUIRED;
	header->ttl = 8U;
	header->sequence = TEST_DATA_SEQUENCE;

	static const uint8_t payload[TEST_DATA_PAYLOAD_LEN] = {0xE4U, 0xD0U, 0x00U,
														   0x01U};

	enp_data_header_init(data_header, ENP_DATA_SUBTYPE_APPLICATION,
						 ENP_DATA_FLAG_NONE, TEST_APP_SEQUENCE,
						 TEST_DATA_PAYLOAD_LEN);

	memcpy(((uint8_t *)data_header) + ENP_DATA_HEADER_SIZE, payload,
		   sizeof(payload));

	return enp_packet_seal(packet, (uint16_t)(ENP_DATA_HEADER_SIZE +
											  sizeof(payload))) == ESP_OK;
}

static bool wait_for_sensor_neighbor(void) {
#if CONFIG_DEVICE_ROLE_GATEWAY
	const enp_address_t sensor = make_address(TEST_SENSOR_NODE_ID);
	enp_transport_address_t address = {0};

	const uint32_t start = enp_context_time_ms(&s_context);

	while ((enp_context_time_ms(&s_context) - start) < TEST_DISCOVERY_WAIT_MS) {
		if (enp_neighbor_get_transport_address(&s_context.neighbors, &sensor,
											   &address) == ESP_OK) {
			ESP_LOGI(TAG, "PASS: Discovery populated Sensor transport address");
			return true;
		}

		vTaskDelay(pdMS_TO_TICKS(100U));
	}

	ESP_LOGE(TAG, "FAIL: Sensor neighbor not discovered");
	return false;
#else
	return true;
#endif
}

static bool run_gateway_test(void) {
#if CONFIG_DEVICE_ROLE_GATEWAY
	if (!wait_for_sensor_neighbor()) {
		return false;
	}

	if (!install_gateway_route()) {
		ESP_LOGE(TAG, "FAIL: production route installation");
		return false;
	}

	ESP_LOGI(TAG, "PASS: production route to Sensor installed");

	enp_packet_t data_packet;

	if (!make_data_packet(&data_packet)) {
		ESP_LOGE(TAG, "FAIL: DATA packet construction");
		return false;
	}

	if (enp_routing_data_path_submit(&s_routing_path, &data_packet) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: DATA submitted through production routing path");
		return false;
	}

	ESP_LOGI(TAG,
			 "PASS: DATA submitted through production routing path to ESP-NOW");

	const uint32_t start = enp_context_time_ms(&s_context);

	while (!s_test_done &&
		   ((enp_context_time_ms(&s_context) - start) < TEST_RESULT_WAIT_MS)) {
		vTaskDelay(pdMS_TO_TICKS(50U));
	}

	if (s_ack_valid && (s_ack_received == 1U)) {
		ESP_LOGI(TAG, "PASS: production receive path delivered correlated ACK");
		return true;
	}

	ESP_LOGE(TAG, "FAIL: correlated ACK was not observed");
	return false;
#else
	return true;
#endif
}

void app_main(void) {
	bool pass = true;

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E4D");
	ESP_LOGI(TAG, "PRODUCTION RUNTIME HARDWARE VALIDATION");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY
	ESP_LOGI(TAG, "Hardware role: GATEWAY / NODE 1");
#else
	ESP_LOGI(TAG, "Hardware role: SENSOR / NODE 2");
#endif

	pass = init_platform();

	if (pass) {
		const enp_config_t config = make_config();

		enp_transport_t *transport = enp_transport_espnow_get();

		pass = (transport != NULL);

		if (pass) {
			pass = (enp_context_init(&s_context, transport, &config) == ESP_OK);
		}

		if (pass) {
			ESP_LOGI(TAG,
					 "PASS: production ENP context initialized with ESP-NOW");
		} else {
			ESP_LOGE(TAG, "FAIL: production ENP context initialization");
		}
	}

	if (pass) {
		pass = enp_dispatcher_init(&s_context) == ESP_OK;
		if (pass) {
			ESP_LOGI(TAG, "PASS: production dispatcher initialized");
		}
	}

	if (pass) {
		pass = enp_dispatcher_register(enp_service_discovery_get()) == ESP_OK;

		if (pass) {
			pass = enp_dispatcher_register(&s_application_service) == ESP_OK;
		}

		if (pass) {
			pass = enp_dispatcher_register(&s_ack_service) == ESP_OK;
		}

		if (pass) {
			ESP_LOGI(TAG, "PASS: Discovery, DATA and ACK services registered");
		}
	}

	if (pass) {
		pass = enp_route_table_init(&s_routes);

		if (pass) {
			pass = enp_routing_data_path_init(&s_routing_path, &s_routes,
											  s_context.transport,
											  resolve_transport, &s_context);

			if (pass) {
				ESP_LOGI(TAG, "PASS: production routing data path initialized");
			} else {
				ESP_LOGE(TAG,
						 "FAIL: production routing data path initialization");
			}
		}
	}

	if (pass) {
		pass = enp_receive_path_init(&s_receive_path, &s_context,
									 &s_routing_path) == ESP_OK;

		if (pass) {
			pass = enp_receive_path_bind(&s_receive_path) == ESP_OK;
		}

		if (pass) {
			pass = enp_transport_set_receive_callback(
					   s_context.transport,
					   enp_receive_path_transport_callback) == ESP_OK;
		}

		if (pass) {
			ESP_LOGI(TAG, "PASS: production receive path bound to real ESP-NOW "
						  "transport");
		}
	}

	if (pass) {
		pass = enp_maintenance_init(&s_context) == ESP_OK;

		if (pass) {
			ESP_LOGI(TAG, "PASS: production maintenance started");
		}
	}

	if (pass) {
		pass = enp_service_discovery_send(&s_context) == ESP_OK;

		if (pass) {
			ESP_LOGI(
				TAG,
				"PASS: initial Discovery sent through production transport");
		}
	}

#if !CONFIG_DEVICE_ROLE_GATEWAY
	/*
	 * The sensor remains active while the gateway sends the validation DATA.
	 * No special receive loop is required: ESP-NOW and maintenance are
	 * task-driven.
	 */
	const uint32_t sensor_start = enp_context_time_ms(&s_context);
	while ((enp_context_time_ms(&s_context) - sensor_start) <
		   (TEST_DISCOVERY_WAIT_MS + TEST_RESULT_WAIT_MS + 2000U)) {
		vTaskDelay(pdMS_TO_TICKS(100U));
	}

	if (s_data_valid && (s_data_received == 1U)) {
		ESP_LOGI(TAG,
				 "PASS: real DATA reached the Sensor application exactly once");
	} else {
		ESP_LOGE(TAG,
				 "FAIL: Sensor did not observe the expected DATA exactly once");
		pass = false;
	}
#endif

#if CONFIG_DEVICE_ROLE_GATEWAY
	if (pass) {
		pass = run_gateway_test();
	}
#endif

	if (s_data_valid && (s_data_received == 1U)) {
		ESP_LOGI(TAG, "PASS: DATA hardware observation recorded");
	}

	if (s_ack_valid && (s_ack_received == 1U)) {
		ESP_LOGI(TAG, "PASS: ACK hardware observation recorded");
	}

	(void)enp_maintenance_deinit();
	(void)enp_receive_path_deinit(&s_receive_path);
	(void)enp_dispatcher_deinit();
	(void)enp_context_deinit(&s_context);

	ESP_LOGI(TAG, "--------------------------------------");

	if (pass) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E4D hardware validation PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E4D hardware validation FAIL");
	}

	ESP_LOGI(TAG, "======================================");

	vTaskDelete(NULL);
}
