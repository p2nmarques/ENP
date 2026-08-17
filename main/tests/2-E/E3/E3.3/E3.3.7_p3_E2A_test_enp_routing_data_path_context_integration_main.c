/*
 * E3.3.7_test_enp_routing_data_path_context_integration_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 3 / E2-A
 * real ENP context + neighbor integration.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "core/enp_context.h"
#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"

static const char *TAG = "E3_3_7_E2A";

#define TEST_NETWORK_ID 1U
#define TEST_LOCAL_NODE_ID 1U
#define TEST_NEXT_HOP_NODE_ID 2U
#define TEST_DEST_NODE_ID 3U
#define TEST_SEQUENCE 0x7401U
#define TEST_TTL 8U
#define TEST_PAYLOAD_LENGTH 3U

static uint8_t s_tx_buffer[ENP_MAX_FRAME_SIZE];
static size_t s_tx_length;
static enp_transport_address_t s_tx_destination;
static unsigned s_init_calls;
static unsigned s_deinit_calls;
static unsigned s_send_calls;
static unsigned s_callback_calls;
static enp_transport_receive_callback_t s_receive_callback;

/* Keep large ENP runtime objects out of the ESP-IDF main-task stack. */
static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_routing_data_path_t s_routing_data_path;
static enp_packet_t s_packet;

static void mock_reset(void) {
	memset(s_tx_buffer, 0, sizeof(s_tx_buffer));
	memset(&s_tx_destination, 0, sizeof(s_tx_destination));
	s_tx_length = 0U;
	s_init_calls = 0U;
	s_deinit_calls = 0U;
	s_send_calls = 0U;
	s_callback_calls = 0U;
	s_receive_callback = NULL;
}

static esp_err_t mock_init(const enp_config_t *config) {
	++s_init_calls;
	return (config != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t mock_deinit(void) {
	++s_deinit_calls;
	return ESP_OK;
}

static esp_err_t mock_send(const enp_transport_address_t *destination,
						   const void *data, size_t length) {
	++s_send_calls;
	if ((destination == NULL) || (data == NULL) || (length == 0U) ||
		(length > sizeof(s_tx_buffer))) {
		return ESP_ERR_INVALID_ARG;
	}
	s_tx_destination = *destination;
	memcpy(s_tx_buffer, data, length);
	s_tx_length = length;
	return ESP_OK;
}

static esp_err_t
mock_set_receive_callback(enp_transport_receive_callback_t callback) {
	++s_callback_calls;
	if (callback == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	s_receive_callback = callback;
	return ESP_OK;
}

static enp_transport_t s_mock_transport = {
	.init = mock_init,
	.deinit = mock_deinit,
	.send = mock_send,
	.set_receive_callback = mock_set_receive_callback,
};

static void test_receive_callback(const enp_transport_address_t *source,
								  const void *data, size_t length) {
	(void)source;
	(void)data;
	(void)length;
}

static enp_config_t make_config(void) {
	return (enp_config_t){
		.network_id = TEST_NETWORK_ID,
		.node_id = TEST_LOCAL_NODE_ID,
		.role = ENP_ROLE_GATEWAY,
	};
}

static enp_transport_address_t make_transport_address(uint8_t seed) {
	enp_transport_address_t address = {.length = 6U};
	for (size_t index = 0U; index < address.length; ++index) {
		address.value[index] = (uint8_t)(seed + index);
	}
	return address;
}

static enp_address_t make_logical_address(uint32_t node_id) {
	return (enp_address_t){
		.network = TEST_NETWORK_ID,
		.node = node_id,
	};
}

static bool install_route(enp_route_table_t *routes) {
	const enp_route_entry_t entry = {
		.destination =
			{
				.network_id = TEST_NETWORK_ID,
				.node_id = TEST_DEST_NODE_ID,
			},
		.next_hop =
			{
				.network_id = TEST_NETWORK_ID,
				.node_id = TEST_NEXT_HOP_NODE_ID,
			},
		.metric =
			{
				.valid = true,
				.type = ENP_ROUTE_METRIC_HOP_COUNT,
				.value = 2U,
			},
		.route_sequence = 1U,
		.expires_at_ms = UINT32_MAX,
		.state = ENP_ROUTE_STATE_ACTIVE,
	};
	return enp_route_table_insert(routes, &entry);
}

static bool make_packet(enp_packet_t *packet) {
	const enp_address_t source = make_logical_address(TEST_LOCAL_NODE_ID);
	enp_packet_init(packet, ENP_PACKET_APPLICATION, &source);
	enp_header_t *header = enp_packet_header(packet);
	if (header == NULL) {
		return false;
	}
	header->destination = make_logical_address(TEST_DEST_NODE_ID);
	header->flags = ENP_FLAG_ACK_REQUIRED;
	header->ttl = TEST_TTL;
	header->sequence = TEST_SEQUENCE;
	const uint8_t payload[TEST_PAYLOAD_LENGTH] = {0xE3U, 0x37U, 0x02U};
	memcpy(enp_packet_payload(packet), payload, sizeof(payload));
	return enp_packet_seal(packet, sizeof(payload)) == ESP_OK;
}

static bool
resolve_transport_from_context(void *context, enp_route_destination_t next_hop,
							   enp_transport_address_t *transport_address) {
	if ((context == NULL) || (transport_address == NULL)) {
		return false;
	}
	enp_context_t *enp_context = (enp_context_t *)context;
	const enp_address_t logical_address = {
		.network = next_hop.network_id,
		.node = next_hop.node_id,
	};
	return enp_neighbor_get_transport_address(&enp_context->neighbors,
											  &logical_address,
											  transport_address) == ESP_OK;
}

static bool transport_address_equal(const enp_transport_address_t *left,
									const enp_transport_address_t *right) {
	if ((left == NULL) || (right == NULL) || (left->length != right->length)) {
		return false;
	}
	return memcmp(left->value, right->value, left->length) == 0;
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 3 / E2-A CONTEXT + NEIGHBOR INTEGRATION");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	bool pass = true;
	mock_reset();

	const enp_config_t config = make_config();

	if (enp_context_init(&s_context, &s_mock_transport, &config) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ENP context initialization");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: ENP context initialized");
	}

	if (s_init_calls != 1U) {
		ESP_LOGE(TAG, "FAIL: transport init callback expected once, got %u",
				 s_init_calls);
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: context initialized supplied transport once");
	}

	if ((s_context.network.id != TEST_NETWORK_ID) ||
		(s_context.network.local.id != TEST_LOCAL_NODE_ID) ||
		(s_context.network.local.role != ENP_ROLE_GATEWAY)) {
		ESP_LOGE(TAG, "FAIL: context logical identity incorrect");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: context logical identity initialized");
	}

	if (enp_transport_set_receive_callback(&s_mock_transport,
										   test_receive_callback) != ESP_OK) {
		ESP_LOGE(TAG,
				 "FAIL: controlled transport receive callback registration");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: controlled transport receive callback registered");
	}

	if (s_callback_calls != 1U || s_receive_callback == NULL) {
		ESP_LOGE(TAG, "FAIL: transport callback registration state");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: transport callback registration state valid");
	}

	const enp_address_t next_hop_address =
		make_logical_address(TEST_NEXT_HOP_NODE_ID);
	const enp_transport_address_t next_hop_transport =
		make_transport_address(0x20U);

	if (enp_neighbor_update(&s_context.neighbors, &next_hop_address,
							&next_hop_transport, ENP_ROLE_RELAY, 0U, 1U, -42,
							enp_context_time_ms(&s_context)) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: real context neighbor update");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: real context neighbor table updated");
	}

	enp_transport_address_t resolved_transport = {0};
	if (enp_neighbor_get_transport_address(&s_context.neighbors,
										   &next_hop_address,
										   &resolved_transport) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: logical neighbor lookup");
		pass = false;
	} else if (!transport_address_equal(&resolved_transport,
										&next_hop_transport)) {
		ESP_LOGE(TAG,
				 "FAIL: logical neighbor resolved to wrong transport address");
		pass = false;
	} else {
		ESP_LOGI(TAG,
				 "PASS: logical neighbor resolves to stored transport address");
	}

	if (!enp_route_table_init(&s_routes)) {
		ESP_LOGE(TAG, "FAIL: route table initialization");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: route table initialized");
	}

	if (!install_route(&s_routes)) {
		ESP_LOGE(TAG, "FAIL: route destination C -> next hop B installation");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: active route C -> next hop B installed");
	}

	if (!enp_routing_data_path_init(
			&s_routing_data_path, &s_routes, &s_mock_transport,
			resolve_transport_from_context, &s_context)) {
		ESP_LOGE(TAG, "FAIL: routing data path initialization");
		pass = false;
	} else {
		ESP_LOGI(
			TAG,
			"PASS: routing data path initialized with real context resolver");
	}

	memset(&s_packet, 0, sizeof(s_packet));
	if (!make_packet(&s_packet)) {
		ESP_LOGE(TAG, "FAIL: DATA packet construction");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: DATA packet constructed");
	}

	memset(s_tx_buffer, 0, sizeof(s_tx_buffer));
	memset(&s_tx_destination, 0, sizeof(s_tx_destination));
	s_tx_length = 0U;
	s_send_calls = 0U;

	if (enp_routing_data_path_submit(&s_routing_data_path, &s_packet) !=
		ESP_OK) {
		ESP_LOGE(TAG, "FAIL: routing data path origin submission");
		pass = false;
	} else {
		ESP_LOGI(
			TAG,
			"PASS: routing data path submitted DATA through context resolver");
	}

	if (s_send_calls != 1U) {
		ESP_LOGE(TAG,
				 "FAIL: controlled transport send count expected 1, got %u",
				 s_send_calls);
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: controlled transport send invoked once");
	}

	if (!transport_address_equal(&s_tx_destination, &next_hop_transport)) {
		ESP_LOGE(TAG, "FAIL: routing path selected wrong transport address");
		pass = false;
	} else {
		ESP_LOGI(TAG,
				 "PASS: routing path selected real neighbor transport address");
	}

	if ((s_tx_length != enp_packet_length(&s_packet)) ||
		memcmp(s_tx_buffer, enp_packet_data_const(&s_packet), s_tx_length) !=
			0) {
		ESP_LOGE(TAG,
				 "FAIL: submitted DATA changed during context integration");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: context integration preserved DATA unchanged");
	}

	const enp_header_t *header = enp_packet_header_const(&s_packet);
	if ((header == NULL) || (header->ttl != TEST_TTL) ||
		(header->sequence != TEST_SEQUENCE) ||
		(header->flags != ENP_FLAG_ACK_REQUIRED)) {
		ESP_LOGE(TAG,
				 "FAIL: DATA identity/TTL changed during origin submission");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: origin DATA identity and TTL preserved");
	}

	if (enp_context_deinit(&s_context) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ENP context deinitialization");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: ENP context deinitialized");
	}

	if (s_deinit_calls != 1U) {
		ESP_LOGE(TAG, "FAIL: transport deinit callback expected once, got %u",
				 s_deinit_calls);
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: context deinitialized supplied transport once");
	}

	ESP_LOGI(TAG, "--------------------------------------");
	if (pass) {
		ESP_LOGI(TAG, "E3.3.7 Phase 3 / E2-A self-test PASS");
		ESP_LOGI(TAG, "======================================");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 3 / E2-A self-test FAIL");
		ESP_LOGE(TAG, "======================================");
	}
}
