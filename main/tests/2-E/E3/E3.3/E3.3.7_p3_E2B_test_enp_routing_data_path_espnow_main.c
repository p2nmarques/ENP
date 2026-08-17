/*
 * E3.3.7_p3_E2B_test_enp_routing_data_path_espnow_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 3 / E2-B
 * Real ENP context + neighbor + ESP-NOW routing data path integration.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Test topology:
 *     Node 1 (sender)  --->  Node 2 (receiver)
 *
 * Node 1 uses the real ENP context, real neighbor table, real
 * enp_routing_data_path and real ESP-NOW transport. Node 2 validates
 * the received serialized ENP frame through the real transport receive
 * callback.
 *
 * 1. Node 2 first
 *
 * Flash with:
 *
 * CONFIG_ENP_E3_NODE_ID=2
 *
 * and capture its MAC.
 *
 * 2. Set Node 2 MAC in the E2-B source.
 *
 * 3. Build/flash Node 1
 *
 * with:
 *
 * CONFIG_ENP_E3_NODE_ID=1
 *
 * This is an integration test. The peer MAC below is intentionally
 * test-specific and is NOT an ENP logical node identifier.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/enp_config.h"
#include "core/enp_context.h"
#include "core/enp_transport.h"
#include "core/network/enp_neighbor.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

static const char *TAG = "E3_3_7_E2B";

#define TEST_NETWORK_ID ((enp_network_id_t)1U)
#define TEST_SENDER_NODE_ID ((enp_node_id_t)1U)
#define TEST_RECEIVER_NODE_ID ((enp_node_id_t)2U)
#define TEST_SEQUENCE ((enp_sequence_t)0x7501U)
#define TEST_TTL ((uint8_t)8U)
#define TEST_PAYLOAD_LENGTH 4U
#define TEST_WAIT_MS 5000U

/*
 * Replace these six bytes with Node 2's STA MAC address before running
 * Node 1. The value is a physical transport address, not an ENP node ID.
 * Node 2 prints its STA MAC at startup.
 */
#define E2B_PEER_MAC_0 0x78U
#define E2B_PEER_MAC_1 0x21U
#define E2B_PEER_MAC_2 0x84U
#define E2B_PEER_MAC_3 0xe6U
#define E2B_PEER_MAC_4 0x19U
#define E2B_PEER_MAC_5 0x84U

static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_routing_data_path_t s_routing_path;
static enp_packet_t s_tx_packet;
static enp_packet_t s_rx_packet;
static volatile bool s_rx_valid = false;
static volatile bool s_rx_seen = false;
static volatile bool s_rx_bad = false;

static void nvs_init(void) {
	esp_err_t err = nvs_flash_init();
	if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
		(err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

static enp_config_t make_config(enp_node_id_t node_id) {
	return (enp_config_t){
		.network_id = TEST_NETWORK_ID,
		.node_id = node_id,
		.role = (node_id == TEST_SENDER_NODE_ID) ? ENP_ROLE_GATEWAY
												 : ENP_ROLE_SENSOR,
	};
}

static enp_address_t make_address(enp_node_id_t node_id) {
	return (enp_address_t){
		.network = TEST_NETWORK_ID,
		.node = node_id,
	};
}

static enp_transport_address_t peer_transport_address(void) {
	return (enp_transport_address_t){
		.value =
			{
				E2B_PEER_MAC_0,
				E2B_PEER_MAC_1,
				E2B_PEER_MAC_2,
				E2B_PEER_MAC_3,
				E2B_PEER_MAC_4,
				E2B_PEER_MAC_5,
			},
		.length = 6U,
	};
}

static bool transport_address_is_zero(const enp_transport_address_t *address) {
	if ((address == NULL) || (address->length != 6U)) {
		return true;
	}

	for (size_t i = 0U; i < 6U; ++i) {
		if (address->value[i] != 0U) {
			return false;
		}
	}
	return true;
}

static bool install_sender_route(void) {
	const enp_route_entry_t entry = {
		.destination =
			{
				.network_id = TEST_NETWORK_ID,
				.node_id = TEST_RECEIVER_NODE_ID,
			},
		.next_hop =
			{
				.network_id = TEST_NETWORK_ID,
				.node_id = TEST_RECEIVER_NODE_ID,
			},
		.metric =
			{
				.valid = true,
				.type = ENP_ROUTE_METRIC_HOP_COUNT,
				.value = 1U,
			},
		.route_sequence = 1U,
		.expires_at_ms = UINT32_MAX,
		.state = ENP_ROUTE_STATE_ACTIVE,
	};

	return enp_route_table_insert(&s_routes, &entry);
}

static bool make_tx_packet(void) {
	const enp_address_t source = make_address(TEST_SENDER_NODE_ID);

	enp_packet_init(&s_tx_packet, ENP_PACKET_APPLICATION, &source);

	enp_header_t *header = enp_packet_header(&s_tx_packet);
	if (header == NULL) {
		return false;
	}

	header->destination = make_address(TEST_RECEIVER_NODE_ID);
	header->flags = ENP_FLAG_ACK_REQUIRED;
	header->ttl = TEST_TTL;
	header->sequence = TEST_SEQUENCE;

	const uint8_t payload[TEST_PAYLOAD_LENGTH] = {0xE3U, 0x37U, 0x0BU, 0x02U};

	memcpy(enp_packet_payload(&s_tx_packet), payload, sizeof(payload));

	return enp_packet_seal(&s_tx_packet, sizeof(payload)) == ESP_OK;
}

static void receive_callback(const enp_transport_address_t *source,
							 const void *data, size_t length) {
	if ((source == NULL) || (data == NULL) || (length == 0U) ||
		(length > sizeof(s_rx_packet))) {
		s_rx_bad = true;
		return;
	}

	s_rx_seen = true;

	memset(&s_rx_packet, 0, sizeof(s_rx_packet));
	memcpy(enp_packet_data(&s_rx_packet), data, length);

	if (!enp_packet_verify(&s_rx_packet)) {
		ESP_LOGE(TAG, "FAIL: received ENP packet CRC/frame validation");
		s_rx_bad = true;
		return;
	}

	const enp_header_t *header = enp_packet_header_const(&s_rx_packet);
	if (header == NULL) {
		ESP_LOGE(TAG, "FAIL: received ENP packet header unavailable");
		s_rx_bad = true;
		return;
	}

	const enp_address_t expected_source = make_address(TEST_SENDER_NODE_ID);
	const enp_address_t expected_destination =
		make_address(TEST_RECEIVER_NODE_ID);
	const uint8_t expected_payload[TEST_PAYLOAD_LENGTH] = {0xE3U, 0x37U, 0x0BU,
														   0x02U};

	if (!enp_address_equal(&header->source, &expected_source) ||
		!enp_address_equal(&header->destination, &expected_destination) ||
		header->sequence != TEST_SEQUENCE ||
		header->flags != ENP_FLAG_ACK_REQUIRED || header->ttl != TEST_TTL ||
		header->payload_length != TEST_PAYLOAD_LENGTH ||
		memcmp(enp_packet_payload_const(&s_rx_packet), expected_payload,
			   sizeof(expected_payload)) != 0) {
		ESP_LOGE(TAG, "FAIL: received packet identity/content mismatch");
		s_rx_bad = true;
		return;
	}

	ESP_LOGI(TAG, "PASS: real ESP-NOW receiver validated DATA frame");
	ESP_LOGI(TAG,
			 "PASS: source=network=%u node=%u destination=network=%u node=%u "
			 "seq=0x%08" PRIX32,
			 (unsigned)header->source.network, (unsigned)header->source.node,
			 (unsigned)header->destination.network,
			 (unsigned)header->destination.node, (uint32_t)header->sequence);
	ESP_LOGI(
		TAG,
		"PASS: received DATA preserved flags=0x%02X TTL=%u payload_length=%u",
		(unsigned)header->flags, (unsigned)header->ttl,
		(unsigned)header->payload_length);

	s_rx_valid = true;
}

static void log_local_mac(void) {
	uint8_t mac[6] = {0};
	if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
		ESP_LOGI(TAG, "STA MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
				 mac[2], mac[3], mac[4], mac[5]);
	}
}

static bool wifi_transport_setup(void) {
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
		vTaskDelay(pdMS_TO_TICKS(100));
	}

	ESP_LOGI(TAG, "Wi-Fi connected, channel=%u",
			 (unsigned)enp_wifi_get_channel());
	log_local_mac();
	return true;
}

static bool run_sender(void) {
	ESP_LOGI(TAG, "Role: NODE 1 / sender");

	const enp_transport_address_t peer = peer_transport_address();
	const enp_address_t receiver_address = make_address(TEST_RECEIVER_NODE_ID);
	if (transport_address_is_zero(&peer)) {
		ESP_LOGE(TAG, "FAIL: E2B peer MAC is still 00:00:00:00:00:00");
		ESP_LOGE(TAG, "Set E2B_PEER_MAC_0..5 to Node 2 STA MAC and rebuild");
		return false;
	}

	enp_transport_t *transport = enp_transport_espnow_get();
	if (transport == NULL) {
		ESP_LOGE(TAG, "FAIL: ESP-NOW transport unavailable");
		return false;
	}

	const enp_config_t config = make_config(TEST_SENDER_NODE_ID);

	if (enp_context_init(&s_context, transport, &config) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ENP context initialization");
		return false;
	}
	ESP_LOGI(TAG, "PASS: ENP context initialized with real ESP-NOW transport");

	if (enp_neighbor_update(&s_context.neighbors, &receiver_address, &peer,
							ENP_ROLE_SENSOR, 0U, 1U, 0,
							enp_context_time_ms(&s_context)) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: real neighbor table update");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(TAG,
			 "PASS: real neighbor logical -> ESP-NOW MAC mapping installed");

	if (!enp_route_table_init(&s_routes)) {
		ESP_LOGE(TAG, "FAIL: route table initialization");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(TAG, "PASS: route Node 2 -> next hop Node 2 installed");

	if (!install_sender_route()) {
		ESP_LOGE(TAG, "FAIL: route installation");
		(void)enp_context_deinit(&s_context);
		return false;
	}

	/* E2-B uses the real context resolver in finish_sender(). */
	return true;
}

static bool
resolve_transport_from_context(void *context, enp_route_destination_t next_hop,
							   enp_transport_address_t *transport_address) {
	if ((context == NULL) || (transport_address == NULL)) {
		return false;
	}

	enp_context_t *enp_context = (enp_context_t *)context;
	const enp_address_t logical = {
		.network = next_hop.network_id,
		.node = next_hop.node_id,
	};

	return enp_neighbor_get_transport_address(&enp_context->neighbors, &logical,
											  transport_address) == ESP_OK;
}

static bool finish_sender(void) {
	enp_transport_t *transport = s_context.transport;

	if (!enp_routing_data_path_init(&s_routing_path, &s_routes, transport,
									resolve_transport_from_context,
									&s_context)) {
		ESP_LOGE(TAG, "FAIL: routing data path initialization");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(TAG,
			 "PASS: routing data path initialized with real context resolver");

	if (!make_tx_packet()) {
		ESP_LOGE(TAG, "FAIL: DATA packet construction");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(TAG, "PASS: DATA packet constructed");

	if (enp_routing_data_path_submit(&s_routing_path, &s_tx_packet) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: routing data path submission to ESP-NOW");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(
		TAG,
		"PASS: routing data path submitted DATA to real ESP-NOW transport");
	ESP_LOGI(TAG, "Waiting for Node 2 to validate the received frame...");

	vTaskDelay(pdMS_TO_TICKS(TEST_WAIT_MS));
	(void)enp_context_deinit(&s_context);

	ESP_LOGI(TAG, "E2-B sender transmission path complete");
	return true;
}

static bool run_receiver(void) {
	ESP_LOGI(TAG, "Role: NODE 2 / receiver");

	enp_transport_t *transport = enp_transport_espnow_get();
	if (transport == NULL) {
		ESP_LOGE(TAG, "FAIL: ESP-NOW transport unavailable");
		return false;
	}

	const enp_config_t config = make_config(TEST_RECEIVER_NODE_ID);

	if (enp_context_init(&s_context, transport, &config) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ENP context initialization");
		return false;
	}
	ESP_LOGI(TAG, "PASS: ENP context initialized with real ESP-NOW transport");

	if (enp_transport_set_receive_callback(s_context.transport,
										   receive_callback) != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: receive callback registration");
		(void)enp_context_deinit(&s_context);
		return false;
	}
	ESP_LOGI(TAG, "PASS: real ESP-NOW receive callback registered");
	ESP_LOGI(TAG, "Waiting up to %u ms for Node 1 DATA...", TEST_WAIT_MS);

	const TickType_t deadline =
		xTaskGetTickCount() + pdMS_TO_TICKS(TEST_WAIT_MS);

	while (!s_rx_seen && (xTaskGetTickCount() < deadline)) {
		vTaskDelay(pdMS_TO_TICKS(50));
	}

	(void)enp_context_deinit(&s_context);

	if (s_rx_valid && !s_rx_bad) {
		ESP_LOGI(TAG, "E2-B receiver validation PASS");
		return true;
	}

	ESP_LOGE(TAG, "E2-B receiver validation FAIL");
	return false;
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 3 / E2-B REAL ESP-NOW INTEGRATION");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	if (!wifi_transport_setup()) {
		ESP_LOGE(TAG, "E2-B aborted during Wi-Fi setup");
		return;
	}

#if CONFIG_ENP_E3_NODE_ID == 1
	bool pass = run_sender();
	if (pass) {
		pass = finish_sender();
	}
#else
	bool pass = run_receiver();
#endif

	ESP_LOGI(TAG, "--------------------------------------");
	if (pass) {
		ESP_LOGI(TAG, "E3.3.7 Phase 3 / E2-B local test PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 3 / E2-B local test FAIL");
	}
	ESP_LOGI(TAG, "======================================");
}
