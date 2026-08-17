/*
 * E3.3.7_p4_E4B_test_enp_production_receive_path_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7 Phase 4 / P4-E4B
 * ENP production receive-path integration self-test.
 *
 * Target: ESP-IDF 6.0.2
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "config/enp_config.h"
#include "core/dispatcher/enp_dispatcher.h"
#include "core/enp_context.h"
#include "core/enp_receive_path.h"
#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_metric.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "core/service/enp_service.h"

static const char *TAG = "E3_3_7_P4_E4B";

static enp_transport_receive_callback_t s_rx_callback = NULL;
static size_t s_transport_send_count = 0U;
static enp_transport_address_t s_last_destination;
static enp_packet_t s_last_sent_packet;
static size_t s_application_calls = 0U;
static size_t s_ack_calls = 0U;
static size_t s_discovery_calls = 0U;

static esp_err_t controlled_init(const enp_config_t *config) {
	(void)config;
	return ESP_OK;
}

static esp_err_t controlled_deinit(void) { return ESP_OK; }

static esp_err_t controlled_send(const enp_transport_address_t *destination,
								 const void *data, size_t length) {
	if ((destination == NULL) || (data == NULL) || (length == 0U) ||
		(length > sizeof(enp_packet_t))) {
		return ESP_ERR_INVALID_ARG;
	}

	s_last_destination = *destination;
	memset(&s_last_sent_packet, 0, sizeof(s_last_sent_packet));
	memcpy(enp_packet_data(&s_last_sent_packet), data, length);
	++s_transport_send_count;
	return ESP_OK;
}

static esp_err_t
controlled_set_receive_callback(enp_transport_receive_callback_t callback) {
	s_rx_callback = callback;
	return ESP_OK;
}

static enp_transport_t s_transport = {.init = controlled_init,
									  .deinit = controlled_deinit,
									  .send = controlled_send,
									  .set_receive_callback =
										  controlled_set_receive_callback};

static esp_err_t application_process(enp_context_t *context,
									 const enp_packet_t *packet,
									 const enp_transport_address_t *source) {
	(void)context;
	(void)packet;
	(void)source;
	++s_application_calls;
	return ESP_OK;
}

static esp_err_t ack_process(enp_context_t *context, const enp_packet_t *packet,
							 const enp_transport_address_t *source) {
	(void)context;
	(void)packet;
	(void)source;
	++s_ack_calls;
	return ESP_OK;
}

static esp_err_t discovery_process(enp_context_t *context,
								   const enp_packet_t *packet,
								   const enp_transport_address_t *source) {
	(void)context;
	(void)packet;
	(void)source;
	++s_discovery_calls;
	return ESP_OK;
}

static const enp_service_t s_application_service = {
	.name = "test_application",
	.packet_type = ENP_PACKET_APPLICATION,
	.init = NULL,
	.process = application_process};

static const enp_service_t s_ack_service = {.name = "test_ack",
											.packet_type = ENP_PACKET_ACK,
											.init = NULL,
											.process = ack_process};

static const enp_service_t s_discovery_service = {.name = "test_discovery",
												  .packet_type =
													  ENP_PACKET_DISCOVERY,
												  .init = NULL,
												  .process = discovery_process};

static bool resolve_transport(void *context, enp_route_destination_t next_hop,
							  enp_transport_address_t *transport_address) {
	(void)context;

	if (transport_address == NULL) {
		return false;
	}

	memset(transport_address, 0, sizeof(*transport_address));
	transport_address->length = 6U;
	transport_address->value[0] = (uint8_t)next_hop.node_id;
	transport_address->value[1] = 0xE4U;
	transport_address->value[2] = 0xB0U;
	transport_address->value[3] = 0x00U;
	transport_address->value[4] = 0x00U;
	transport_address->value[5] = 0x01U;
	return true;
}

static void expect(bool condition, const char *message, bool *pass) {
	if (condition) {
		ESP_LOGI(TAG, "PASS: %s", message);
	} else {
		ESP_LOGE(TAG, "FAIL: %s", message);
		*pass = false;
	}
}

static void make_packet(enp_packet_t *packet, enp_packet_type_t type,
						enp_address_t source, enp_address_t destination,
						enp_sequence_t sequence, uint8_t flags) {
	enp_packet_init(packet, type, &source);

	enp_header_t *header = enp_packet_header(packet);
	header->destination = destination;
	header->sequence = sequence;
	header->flags = flags;
	header->ttl = 8U;

	uint8_t *payload = (uint8_t *)enp_packet_payload(packet);
	payload[0] = 0xE4U;
	payload[1] = 0xB0U;
	payload[2] = 0x01U;
	payload[3] = 0x02U;

	(void)enp_packet_seal(packet, 4U);
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E4B PRODUCTION RECEIVE PATH");
	ESP_LOGI(
		TAG,
		"Transport RX -> DATA/ACK data plane; other packets -> dispatcher");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	bool pass = true;
	/*
	 * These runtime objects are intentionally static. The ESP-IDF main task
	 * stack is only 3584 bytes in the test configuration, while the ENP
	 * context, route table and data-plane state contain fixed-size tables.
	 * Keeping them in app_main() would create an oversized stack frame before
	 * the first test statement executes.
	 */
	static enp_context_t context;
	static enp_route_table_t routes;
	static enp_routing_data_path_t routing_path;
	static enp_receive_path_t receive_path;

	const enp_config_t config = {
		.network_id = 1U, .node_id = 1U, .role = ENP_ROLE_GATEWAY};

	enp_transport_address_t source = {0};
	source.length = 6U;
	source.value[0] = 0xAAU;
	source.value[1] = 0xBBU;
	source.value[2] = 0xCCU;
	source.value[3] = 0xDDU;
	source.value[4] = 0xEEU;
	source.value[5] = 0xFFU;

	memset(&context, 0, sizeof(context));
	memset(&routes, 0, sizeof(routes));
	memset(&routing_path, 0, sizeof(routing_path));
	memset(&receive_path, 0, sizeof(receive_path));

	s_rx_callback = NULL;
	s_transport_send_count = 0U;
	s_application_calls = 0U;
	s_ack_calls = 0U;
	s_discovery_calls = 0U;

	expect(enp_context_init(&context, &s_transport, &config) == ESP_OK,
		   "ENP context initialized", &pass);

	expect(enp_dispatcher_init(&context) == ESP_OK, "dispatcher initialized",
		   &pass);

	expect(enp_dispatcher_register(&s_application_service) == ESP_OK,
		   "application service registered", &pass);
	expect(enp_dispatcher_register(&s_ack_service) == ESP_OK,
		   "ACK service registered", &pass);
	expect(enp_dispatcher_register(&s_discovery_service) == ESP_OK,
		   "discovery service registered", &pass);

	expect(enp_route_table_init(&routes), "route table initialized", &pass);

	enp_route_metric_t metric;
	expect(enp_route_metric_init(&metric, ENP_ROUTE_METRIC_HOP_COUNT),
		   "route metric initialized", &pass);

	const enp_route_entry_t route = {
		.destination = {.network_id = 1U, .node_id = 3U},
		.next_hop = {.network_id = 1U, .node_id = 2U},
		.metric = metric,
		.route_sequence = 1U,
		.expires_at_ms = UINT32_MAX,
		.state = ENP_ROUTE_STATE_ACTIVE};

	expect(enp_route_table_insert(&routes, &route),
		   "active route to Node 3 installed", &pass);

	expect(enp_routing_data_path_init(&routing_path, &routes, &s_transport,
									  resolve_transport, &context),
		   "routing data path initialized", &pass);

	expect(enp_receive_path_init(&receive_path, &context, &routing_path) ==
			   ESP_OK,
		   "production receive path initialized", &pass);

	expect(enp_receive_path_bind(&receive_path) == ESP_OK,
		   "production receive path bound", &pass);

	expect(enp_transport_set_receive_callback(
			   &s_transport, enp_receive_path_transport_callback) == ESP_OK,
		   "transport receive callback registered with production path", &pass);

	expect(s_rx_callback != NULL,
		   "controlled transport retained production receive callback", &pass);

	enp_packet_t packet;

	/* Local DATA: data-plane duplicate domain, then dispatcher local boundary.
	 */
	make_packet(&packet, ENP_PACKET_APPLICATION, (enp_address_t){1U, 2U},
				(enp_address_t){1U, 1U}, 0xE4B1U, 0U);

	const size_t app_before = s_application_calls;
	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_application_calls == app_before + 1U,
		   "local DATA reached dispatcher local service through data plane",
		   &pass);

	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_application_calls == app_before + 1U,
		   "duplicate local DATA was suppressed by data-plane domain", &pass);

	/* Local ACK: independently classified into ACK data-plane domain. */
	make_packet(&packet, ENP_PACKET_ACK, (enp_address_t){1U, 2U},
				(enp_address_t){1U, 1U}, 0xE4B2U, 0U);

	const size_t ack_before = s_ack_calls;
	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_ack_calls == ack_before + 1U,
		   "local ACK reached dispatcher local service through data plane",
		   &pass);

	/* Non-DATA/ACK packet: remains on normal dispatcher path. */
	make_packet(&packet, ENP_PACKET_DISCOVERY, (enp_address_t){1U, 2U},
				(enp_address_t){1U, 1U}, 0xE4B3U, 0U);

	const size_t discovery_before = s_discovery_calls;
	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_discovery_calls == discovery_before + 1U,
		   "non-DATA/ACK traffic remained on normal dispatcher path", &pass);

	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_discovery_calls == discovery_before + 1U,
		   "normal dispatcher duplicate cache suppressed duplicate discovery",
		   &pass);

	/* Non-local DATA: data plane forwards through the routing path. */
	make_packet(&packet, ENP_PACKET_APPLICATION, (enp_address_t){1U, 1U},
				(enp_address_t){1U, 3U}, 0xE4B4U, 0U);

	const size_t sends_before = s_transport_send_count;
	s_rx_callback(&source, enp_packet_data_const(&packet),
				  enp_packet_length(&packet));
	expect(s_transport_send_count == sends_before + 1U,
		   "non-local DATA reached routing data path and transport", &pass);
	expect(enp_packet_header_const(&s_last_sent_packet)->ttl == 7U,
		   "forwarded DATA decremented TTL 8 -> 7", &pass);
	expect(enp_packet_header_const(&s_last_sent_packet)->sequence == 0xE4B4U,
		   "forwarded DATA preserved transaction identity", &pass);
	expect(s_last_destination.length == 6U && s_last_destination.value[0] == 2U,
		   "forwarded DATA selected configured next-hop transport address",
		   &pass);

	expect(enp_receive_path_deinit(&receive_path) == ESP_OK,
		   "production receive path deinitialized", &pass);
	expect(enp_dispatcher_deinit() == ESP_OK, "dispatcher deinitialized",
		   &pass);
	expect(enp_context_deinit(&context) == ESP_OK, "ENP context deinitialized",
		   &pass);

	ESP_LOGI(TAG, "--------------------------------------");

	if (pass) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E4B self-test PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E4B self-test FAIL");
	}

	ESP_LOGI(TAG, "======================================");
}
