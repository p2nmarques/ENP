/*
 * test_enp_routing_main.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

#include "core/protocol/payloads/enp_routing.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ROUTETEST";

static int s_failures = 0;

#define TEST_PASS(name) ESP_LOGI(TAG, "PASS: %s", name)

#define TEST_FAIL(name)                                                        \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", name);                                       \
		s_failures++;                                                          \
	} while (0)

#define EXPECT_TRUE(condition, name)                                           \
	do {                                                                       \
		if (condition) {                                                       \
			TEST_PASS(name);                                                   \
		} else {                                                               \
			TEST_FAIL(name);                                                   \
		}                                                                      \
	} while (0)

static void test_rreq(void) {
	uint8_t buffer[ENP_ROUTING_RREQ_WIRE_SIZE];
	uint8_t expected[ENP_ROUTING_RREQ_WIRE_SIZE] = {
		0x01, 0x01, 0x34, 0x12, 0x78, 0x56, 0x04, 0x03, 0x02, 0x01,
		0x0D, 0x0C, 0x0B, 0x0A, 0x05, 0x08, 0x44, 0x33, 0x22, 0x11};

	enp_routing_rreq_t input = {.payload_version = ENP_ROUTING_PAYLOAD_VERSION,
								.subtype = ENP_ROUTING_SUBTYPE_RREQ,
								.destination_network_id = 0x1234,
								.destination_node_id = 0x5678,
								.route_request_id = 0x01020304,
								.destination_sequence = 0x0A0B0C0D,
								.hop_count = 5,
								.ttl = 8,
								.route_lifetime_ms = 0x11223344};

	enp_routing_rreq_t output;

	memset(buffer, 0xAA, sizeof(buffer));
	memset(&output, 0, sizeof(output));

	EXPECT_TRUE(enp_routing_rreq_encode(&input, buffer, sizeof(buffer)),
				"RREQ encode");

	EXPECT_TRUE(memcmp(buffer, expected, sizeof(expected)) == 0,
				"RREQ exact wire bytes");

	EXPECT_TRUE(enp_routing_rreq_decode(&output, buffer, sizeof(buffer)),
				"RREQ decode");

	EXPECT_TRUE(memcmp(&input, &output, sizeof(input)) == 0, "RREQ round-trip");

	EXPECT_TRUE(!enp_routing_rreq_decode(&output, buffer,
										 ENP_ROUTING_RREQ_WIRE_SIZE - 1U),
				"RREQ rejects short buffer");

	buffer[0] = 0xFF;
	EXPECT_TRUE(!enp_routing_rreq_decode(&output, buffer, sizeof(buffer)),
				"RREQ rejects invalid version");

	memcpy(buffer, expected, sizeof(buffer));
	buffer[1] = 0xFF;
	EXPECT_TRUE(!enp_routing_rreq_decode(&output, buffer, sizeof(buffer)),
				"RREQ rejects invalid subtype");

	EXPECT_TRUE(!enp_routing_rreq_encode(&input, buffer,
										 ENP_ROUTING_RREQ_WIRE_SIZE - 1U),
				"RREQ encode rejects short buffer");

	EXPECT_TRUE(!enp_routing_rreq_encode(NULL, buffer, sizeof(buffer)),
				"RREQ encode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rreq_decode(NULL, buffer, sizeof(buffer)),
				"RREQ decode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rreq_decode(&output, NULL, sizeof(buffer)),
				"RREQ decode rejects NULL buffer");
}

static void test_rrep(void) {
	uint8_t buffer[ENP_ROUTING_RREP_WIRE_SIZE];
	uint8_t expected[ENP_ROUTING_RREP_WIRE_SIZE] = {
		0x01, 0x02, 0x34, 0x12, 0x78, 0x56, 0x0D, 0x0C, 0x0B, 0x0A,
		0x07, 0x00, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00, 0x00};

	enp_routing_rrep_t input = {.payload_version = ENP_ROUTING_PAYLOAD_VERSION,
								.subtype = ENP_ROUTING_SUBTYPE_RREP,
								.destination_network_id = 0x1234,
								.destination_node_id = 0x5678,
								.destination_sequence = 0x0A0B0C0D,
								.hop_count = 7,
								.reserved_0 = 0,
								.route_lifetime_ms = 0x11223344,
								.reserved_1 = 0};

	enp_routing_rrep_t output;

	memset(buffer, 0xAA, sizeof(buffer));
	memset(&output, 0, sizeof(output));

	EXPECT_TRUE(enp_routing_rrep_encode(&input, buffer, sizeof(buffer)),
				"RREP encode");

	EXPECT_TRUE(memcmp(buffer, expected, sizeof(expected)) == 0,
				"RREP exact wire bytes");

	EXPECT_TRUE(enp_routing_rrep_decode(&output, buffer, sizeof(buffer)),
				"RREP decode");

	EXPECT_TRUE(memcmp(&input, &output, sizeof(input)) == 0, "RREP round-trip");

	EXPECT_TRUE(!enp_routing_rrep_decode(&output, buffer,
										 ENP_ROUTING_RREP_WIRE_SIZE - 1U),
				"RREP rejects short buffer");

	buffer[0] = 0xFF;
	EXPECT_TRUE(!enp_routing_rrep_decode(&output, buffer, sizeof(buffer)),
				"RREP rejects invalid version");

	memcpy(buffer, expected, sizeof(buffer));
	buffer[1] = 0xFF;
	EXPECT_TRUE(!enp_routing_rrep_decode(&output, buffer, sizeof(buffer)),
				"RREP rejects invalid subtype");

	EXPECT_TRUE(!enp_routing_rrep_encode(&input, buffer,
										 ENP_ROUTING_RREP_WIRE_SIZE - 1U),
				"RREP encode rejects short buffer");

	EXPECT_TRUE(!enp_routing_rrep_encode(NULL, buffer, sizeof(buffer)),
				"RREP encode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rrep_decode(NULL, buffer, sizeof(buffer)),
				"RREP decode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rrep_decode(&output, NULL, sizeof(buffer)),
				"RREP decode rejects NULL buffer");
}

static void test_rerr(void) {
	uint8_t buffer[ENP_ROUTING_RERR_WIRE_SIZE];
	uint8_t expected[ENP_ROUTING_RERR_WIRE_SIZE] = {
		0x01, 0x03, 0x34, 0x12, 0x78, 0x56, 0x0D, 0x0C,
		0x0B, 0x0A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

	enp_routing_rerr_t input = {.payload_version = ENP_ROUTING_PAYLOAD_VERSION,
								.subtype = ENP_ROUTING_SUBTYPE_RERR,
								.unreachable_network_id = 0x1234,
								.unreachable_node_id = 0x5678,
								.destination_sequence = 0x0A0B0C0D,
								.reason = ENP_ROUTE_ERROR_NEXT_HOP_UNREACHABLE,
								.reserved_0 = 0,
								.reserved_1 = 0};

	enp_routing_rerr_t output;

	memset(buffer, 0xAA, sizeof(buffer));
	memset(&output, 0, sizeof(output));

	EXPECT_TRUE(enp_routing_rerr_encode(&input, buffer, sizeof(buffer)),
				"RERR encode");

	EXPECT_TRUE(memcmp(buffer, expected, sizeof(expected)) == 0,
				"RERR exact wire bytes");

	EXPECT_TRUE(enp_routing_rerr_decode(&output, buffer, sizeof(buffer)),
				"RERR decode");

	EXPECT_TRUE(memcmp(&input, &output, sizeof(input)) == 0, "RERR round-trip");

	EXPECT_TRUE(!enp_routing_rerr_decode(&output, buffer,
										 ENP_ROUTING_RERR_WIRE_SIZE - 1U),
				"RERR rejects short buffer");

	buffer[0] = 0xFF;
	EXPECT_TRUE(!enp_routing_rerr_decode(&output, buffer, sizeof(buffer)),
				"RERR rejects invalid version");

	memcpy(buffer, expected, sizeof(buffer));
	buffer[1] = 0xFF;
	EXPECT_TRUE(!enp_routing_rerr_decode(&output, buffer, sizeof(buffer)),
				"RERR rejects invalid subtype");

	EXPECT_TRUE(!enp_routing_rerr_encode(&input, buffer,
										 ENP_ROUTING_RERR_WIRE_SIZE - 1U),
				"RERR encode rejects short buffer");

	EXPECT_TRUE(!enp_routing_rerr_encode(NULL, buffer, sizeof(buffer)),
				"RERR encode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rerr_decode(NULL, buffer, sizeof(buffer)),
				"RERR decode rejects NULL message");

	EXPECT_TRUE(!enp_routing_rerr_decode(&output, NULL, sizeof(buffer)),
				"RERR decode rejects NULL buffer");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "ENP v0.2 routing serialization test");
	ESP_LOGI(TAG, "RREQ wire size: %u", ENP_ROUTING_RREQ_WIRE_SIZE);
	ESP_LOGI(TAG, "RREP wire size: %u", ENP_ROUTING_RREP_WIRE_SIZE);
	ESP_LOGI(TAG, "RERR wire size: %u", ENP_ROUTING_RERR_WIRE_SIZE);
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "RREQ tests");
	test_rreq();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "RREP tests");
	test_rrep();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "RERR tests");
	test_rerr();

	ESP_LOGI(TAG, "======================================");

	if (s_failures == 0) {
		ESP_LOGI(TAG, "ALL ROUTING SERIALIZATION TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "%d ROUTING SERIALIZATION TEST(S) FAILED", s_failures);
	}

	ESP_LOGI(TAG, "======================================");
}
