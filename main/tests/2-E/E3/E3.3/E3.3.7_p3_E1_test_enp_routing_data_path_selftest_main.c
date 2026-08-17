/*
 * E3.3.7_p3_E1_test_enp_routing_data_path_selftest_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 3 / E1 — hardware-independent routing data-path self-test.
 * ESP-IDF 6.0.2 compatible.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"

static const char *TAG = "E3_3_7";

static uint8_t s_tx_buffer[ENP_MAX_FRAME_SIZE];
static size_t s_tx_length;
static uint16_t s_tx_destination;

static esp_err_t mock_send(const enp_transport_address_t *destination,
						   const void *data, size_t length) {
	if (destination == NULL || data == NULL || length > sizeof(s_tx_buffer)) {
		return ESP_ERR_INVALID_ARG;
	}
	memcpy(s_tx_buffer, data, length);
	s_tx_length = length;
	s_tx_destination = destination->value[0];
	return ESP_OK;
}

static bool resolve_transport(void *context, enp_route_destination_t next_hop,
							  enp_transport_address_t *transport_address) {
	(void)context;
	if (transport_address == NULL) {
		return false;
	}
	memset(transport_address, 0, sizeof(*transport_address));
	transport_address->length = 1U;
	transport_address->value[0] = (uint8_t)next_hop.node_id;
	return true;
}

static bool install_route(enp_route_table_t *routes, uint16_t destination,
						  uint16_t next_hop) {
	enp_route_entry_t entry = {0};
	entry.destination.network_id = 1U;
	entry.destination.node_id = destination;
	entry.next_hop.network_id = 1U;
	entry.next_hop.node_id = next_hop;
	entry.metric.valid = true;
	entry.metric.type = ENP_ROUTE_METRIC_HOP_COUNT;
	entry.metric.value = 2U;
	entry.route_sequence = 1U;
	entry.expires_at_ms = UINT32_MAX;
	entry.state = ENP_ROUTE_STATE_ACTIVE;
	return enp_route_table_insert(routes, &entry);
}

static bool make_packet(enp_packet_t *packet, uint8_t ttl, uint16_t sequence) {
	enp_address_t source = {.network = 1U, .node = 1U};
	enp_packet_init(packet, ENP_PACKET_APPLICATION, &source);
	enp_header_t *header = enp_packet_header(packet);
	if (header == NULL) {
		return false;
	}
	header->destination.network = 1U;
	header->destination.node = 3U;
	header->flags = ENP_FLAG_ACK_REQUIRED;
	header->ttl = ttl;
	header->sequence = sequence;
	const uint8_t payload[] = {0xE3U, 0x37U, 0x01U};
	memcpy(enp_packet_payload(packet), payload, sizeof(payload));
	return enp_packet_seal(packet, sizeof(payload)) == ESP_OK;
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 3 / E1 ROUTING DATA-PATH SELF-TEST");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	enp_route_table_t routes;
	enp_transport_t transport = {
		.send = mock_send,
	};
	enp_routing_data_path_t path;
	enp_packet_t packet;

	bool pass = true;

	if (!enp_route_table_init(&routes)) {
		ESP_LOGE(TAG, "FAIL: route table initialization");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: route table initialized");
	}

	if (!install_route(&routes, 3U, 2U)) {
		ESP_LOGE(TAG, "FAIL: route installation");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: active route C -> next hop B installed");
	}

	if (!enp_routing_data_path_init(&path, &routes, &transport,
									resolve_transport, NULL)) {
		ESP_LOGE(TAG, "FAIL: routing data path initialization");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: routing data path initialized");
	}

	memset(s_tx_buffer, 0, sizeof(s_tx_buffer));
	s_tx_length = 0U;
	s_tx_destination = 0U;

	if (!make_packet(&packet, 8U, 0x7301U)) {
		ESP_LOGE(TAG, "FAIL: reliable DATA packet construction");
		pass = false;
	}

	if (enp_routing_data_path_submit(&path, &packet) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: origin DATA submission");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: origin DATA submitted through active route");
	}

	if (s_tx_destination != 2U) {
		ESP_LOGE(TAG, "FAIL: selected transport next hop expected B(2), got %u",
				 (unsigned)s_tx_destination);
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: route selected next hop B");
	}

	enp_packet_t captured;
	memset(&captured, 0, sizeof(captured));
	memcpy(enp_packet_data(&captured), s_tx_buffer, s_tx_length);

	const enp_header_t *captured_header = enp_packet_header_const(&captured);
	const enp_header_t *original_header = enp_packet_header_const(&packet);
	if (captured_header == NULL || original_header == NULL ||
		captured_header->ttl != original_header->ttl ||
		captured_header->sequence != original_header->sequence ||
		captured_header->flags != original_header->flags) {
		ESP_LOGE(TAG, "FAIL: origin submission changed DATA identity/TTL");
		pass = false;
	} else {
		ESP_LOGI(TAG,
				 "PASS: origin submission preserves DATA identity and TTL");
	}

	if (enp_routing_data_path_forward(&path, &packet) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: DATA forwarding");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: DATA forwarding succeeded");
	}

	memset(&captured, 0, sizeof(captured));
	memcpy(enp_packet_data(&captured), s_tx_buffer, s_tx_length);
	captured_header = enp_packet_header_const(&captured);
	if (captured_header == NULL || captured_header->ttl != 7U) {
		ESP_LOGE(TAG, "FAIL: forwarding did not decrement TTL from 8 to 7");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: forwarding decremented TTL 8 -> 7");
	}

	if (enp_packet_header_const(&packet)->ttl != 8U) {
		ESP_LOGE(TAG, "FAIL: caller packet was modified during forwarding");
		pass = false;
	} else {
		ESP_LOGI(TAG, "PASS: forwarding preserved caller packet");
	}

	if (pass) {
		ESP_LOGI(TAG, "--------------------------------------");
		ESP_LOGI(TAG, "E3.3.7 Phase 3 / E1 self-test PASS");
		ESP_LOGI(TAG, "======================================");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 3 / E1 self-test FAIL");
	}
}
