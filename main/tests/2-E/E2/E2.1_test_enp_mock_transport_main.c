/*
 * E2.1_test_enp_mock_transport_main.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E2.1 mock transport adapter integration test.
 *
 * This test validates the frozen generic transport abstraction without
 * using ESP-NOW, Wi-Fi, sockets, or any link-specific implementation.
 *
 * Scope:
 *   - enp_transport_t adapter lifecycle
 *   - send through the generic wrapper
 *   - receive callback registration and delivery
 *   - invalid argument handling
 *   - enp_context_t integration with a supplied transport
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "core/enp_context.h"
#include "core/enp_transport.h"

static const char *TAG = "E2MOCK";
static int s_failures;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)
#define FAIL(name)                                                             \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", name);                                       \
		++s_failures;                                                          \
	} while (0)

#define EXPECT_TRUE(condition, name)                                           \
	do {                                                                       \
		if (condition) {                                                       \
			PASS(name);                                                        \
		} else {                                                               \
			FAIL(name);                                                        \
		}                                                                      \
	} while (0)

#define MOCK_MAX_PAYLOAD 256U

typedef struct {
	bool initialized;
	bool deinitialized;
	bool callback_registered;

	bool init_ok;
	bool deinit_ok;
	bool send_ok;
	bool callback_ok;

	unsigned init_calls;
	unsigned deinit_calls;
	unsigned send_calls;
	unsigned callback_set_calls;
	unsigned receive_deliveries;

	enp_config_t last_config;

	enp_transport_address_t last_destination;

	uint8_t last_payload[MOCK_MAX_PAYLOAD];
	size_t last_payload_length;

	enp_transport_receive_callback_t receive_callback;

	enp_transport_address_t last_source;
	uint8_t last_received_payload[MOCK_MAX_PAYLOAD];
	size_t last_received_length;
} mock_transport_state_t;

static mock_transport_state_t s_mock;

static void mock_reset(void) {
	memset(&s_mock, 0, sizeof(s_mock));

	s_mock.init_ok = true;
	s_mock.deinit_ok = true;
	s_mock.send_ok = true;
	s_mock.callback_ok = true;
}

static esp_err_t mock_init(const enp_config_t *config) {
	++s_mock.init_calls;

	if (!s_mock.init_ok) {
		return ESP_FAIL;
	}

	if (config == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	s_mock.last_config = *config;
	s_mock.initialized = true;
	s_mock.deinitialized = false;

	return ESP_OK;
}

static esp_err_t mock_deinit(void) {
	++s_mock.deinit_calls;

	if (!s_mock.deinit_ok) {
		return ESP_FAIL;
	}

	s_mock.deinitialized = true;
	s_mock.initialized = false;

	return ESP_OK;
}

static esp_err_t mock_send(const enp_transport_address_t *destination,
						   const void *data, size_t length) {
	++s_mock.send_calls;

	if (!s_mock.send_ok) {
		return ESP_FAIL;
	}

	if ((destination == NULL) || (data == NULL) || (length == 0U) ||
		(length > MOCK_MAX_PAYLOAD)) {
		return ESP_ERR_INVALID_ARG;
	}

	s_mock.last_destination = *destination;
	memcpy(s_mock.last_payload, data, length);

	s_mock.last_payload_length = length;

	return ESP_OK;
}

static esp_err_t
mock_set_receive_callback(enp_transport_receive_callback_t callback) {
	++s_mock.callback_set_calls;

	if (!s_mock.callback_ok) {
		return ESP_FAIL;
	}

	if (callback == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	s_mock.receive_callback = callback;
	s_mock.callback_registered = true;

	return ESP_OK;
}

static enp_transport_t s_mock_transport = {.init = mock_init,
										   .deinit = mock_deinit,
										   .send = mock_send,
										   .set_receive_callback =
											   mock_set_receive_callback};

static void receive_callback(const enp_transport_address_t *source,
							 const void *data, size_t length) {
	if ((source == NULL) || (data == NULL) || (length > MOCK_MAX_PAYLOAD)) {
		return;
	}

	++s_mock.receive_deliveries;

	s_mock.last_source = *source;

	memcpy(s_mock.last_received_payload, data, length);

	s_mock.last_received_length = length;
}

static void mock_deliver(const enp_transport_address_t *source,
						 const void *data, size_t length) {
	if (s_mock.receive_callback == NULL) {
		return;
	}

	s_mock.receive_callback(source, data, length);
}

static enp_transport_address_t make_address(uint8_t seed) {
	enp_transport_address_t address = {.length = 6U};

	for (size_t i = 0; i < address.length; ++i) {
		address.value[i] = (uint8_t)(seed + i);
	}

	return address;
}

static enp_config_t make_config(void) {
	return (enp_config_t){
		.network_id = 1U, .node_id = 42U, .role = ENP_ROLE_RELAY};
}

static void test_transport_initialization(void) {
	enp_config_t config = make_config();

	mock_reset();

	EXPECT_TRUE(enp_transport_init(&s_mock_transport, &config) == ESP_OK,
				"mock transport initializes");

	EXPECT_TRUE(s_mock.init_calls == 1U,
				"transport init callback invoked once");

	EXPECT_TRUE(s_mock.initialized, "mock transport reports initialized");

	EXPECT_TRUE(s_mock.last_config.network_id == 1U &&
					s_mock.last_config.node_id == 42U &&
					s_mock.last_config.role == ENP_ROLE_RELAY,
				"transport receives configuration unchanged");
}

static void test_receive_registration(void) {
	EXPECT_TRUE(enp_transport_set_receive_callback(&s_mock_transport,
												   receive_callback) == ESP_OK,
				"receive callback registers");

	EXPECT_TRUE(s_mock.callback_set_calls == 1U,
				"receive callback registration invoked once");

	EXPECT_TRUE(s_mock.callback_registered,
				"mock transport stores receive callback");

	enp_transport_address_t source = make_address(0x10U);

	const uint8_t payload[] = {0x01U, 0x02U, 0x03U, 0x04U};

	mock_deliver(&source, payload, sizeof(payload));

	EXPECT_TRUE(s_mock.receive_deliveries == 1U,
				"mock transport delivers received payload");

	EXPECT_TRUE(
		s_mock.last_source.length == source.length &&
			memcmp(s_mock.last_source.value, source.value, source.length) == 0,
		"receive callback receives source address");

	EXPECT_TRUE(
		s_mock.last_received_length == sizeof(payload) &&
			memcmp(s_mock.last_received_payload, payload, sizeof(payload)) == 0,
		"receive callback receives payload unchanged");
}

static void test_send(void) {
	enp_transport_address_t destination = make_address(0x20U);

	const uint8_t payload[] = {0xA1U, 0xB2U, 0xC3U, 0xD4U, 0xE5U};

	EXPECT_TRUE(enp_transport_send(&s_mock_transport, &destination, payload,
								   sizeof(payload)) == ESP_OK,
				"send succeeds through generic transport API");

	EXPECT_TRUE(s_mock.send_calls == 1U,
				"transport send callback invoked once");

	EXPECT_TRUE(s_mock.last_destination.length == destination.length &&
					memcmp(s_mock.last_destination.value, destination.value,
						   destination.length) == 0,
				"send receives destination unchanged");

	EXPECT_TRUE(s_mock.last_payload_length == sizeof(payload) &&
					memcmp(s_mock.last_payload, payload, sizeof(payload)) == 0,
				"send receives payload unchanged");
}

static void test_transport_errors(void) {
	enp_config_t config = make_config();

	EXPECT_TRUE(enp_transport_init(NULL, &config) == ESP_ERR_INVALID_ARG,
				"NULL transport rejected");

	EXPECT_TRUE(enp_transport_init(&s_mock_transport, NULL) ==
					ESP_ERR_INVALID_ARG,
				"NULL configuration rejected");

	EXPECT_TRUE(enp_transport_send(&s_mock_transport, NULL, "x", 1U) ==
					ESP_ERR_INVALID_ARG,
				"NULL destination rejected");

	EXPECT_TRUE(enp_transport_send(&s_mock_transport,
								   &(enp_transport_address_t){0}, NULL,
								   1U) == ESP_ERR_INVALID_ARG,
				"NULL payload rejected");

	EXPECT_TRUE(enp_transport_send(&s_mock_transport,
								   &(enp_transport_address_t){0}, "x",
								   0U) == ESP_ERR_INVALID_ARG,
				"zero-length payload rejected");

	EXPECT_TRUE(enp_transport_set_receive_callback(&s_mock_transport, NULL) ==
					ESP_ERR_INVALID_ARG,
				"NULL receive callback rejected");

	s_mock.send_ok = false;

	enp_transport_address_t destination = make_address(0x30U);

	EXPECT_TRUE(enp_transport_send(&s_mock_transport, &destination, "x", 1U) ==
					ESP_FAIL,
				"transport send failure propagates");

	s_mock.send_ok = true;

	s_mock.callback_ok = false;

	EXPECT_TRUE(enp_transport_set_receive_callback(
					&s_mock_transport, receive_callback) == ESP_FAIL,
				"transport callback-registration failure propagates");

	s_mock.callback_ok = true;
}

static void test_context_integration(void) {
	enp_context_t context;
	enp_config_t config = make_config();

	mock_reset();

	EXPECT_TRUE(enp_context_init(&context, &s_mock_transport, &config) ==
					ESP_OK,
				"ENP context initializes with mock transport");

	EXPECT_TRUE(context.transport == &s_mock_transport,
				"context stores supplied transport");

	EXPECT_TRUE(context.network.id == config.network_id,
				"context stores network ID");

	EXPECT_TRUE(context.network.local.id == config.node_id,
				"context stores local node ID");

	EXPECT_TRUE(context.network.local.role == config.role,
				"context stores local node role");

	EXPECT_TRUE(s_mock.init_calls == 1U,
				"context initialization invokes transport init");

	EXPECT_TRUE(enp_context_deinit(&context) == ESP_OK,
				"ENP context deinitializes");

	EXPECT_TRUE(s_mock.deinit_calls == 1U,
				"context deinitialization invokes transport deinit");

	EXPECT_TRUE(s_mock.deinitialized, "mock transport reports deinitialized");
}

static void test_context_invalid(void) {
	enp_context_t context;
	enp_config_t config = make_config();

	mock_reset();

	EXPECT_TRUE(enp_context_init(NULL, &s_mock_transport, &config) ==
					ESP_ERR_INVALID_ARG,
				"context rejects NULL context");

	EXPECT_TRUE(enp_context_init(&context, NULL, &config) ==
					ESP_ERR_INVALID_ARG,
				"context rejects NULL transport");

	EXPECT_TRUE(enp_context_init(&context, &s_mock_transport, NULL) ==
					ESP_ERR_INVALID_ARG,
				"context rejects NULL configuration");

	EXPECT_TRUE(enp_context_deinit(NULL) == ESP_ERR_INVALID_ARG,
				"context deinit rejects NULL");
}

static void test_transport_deinitialization(void) {
	mock_reset();

	enp_config_t config = make_config();

	EXPECT_TRUE(enp_transport_init(&s_mock_transport, &config) == ESP_OK,
				"transport reinitializes for deinit test");

	EXPECT_TRUE(enp_transport_deinit(&s_mock_transport) == ESP_OK,
				"transport deinitializes");

	EXPECT_TRUE(s_mock.deinit_calls == 1U,
				"transport deinit callback invoked once");

	EXPECT_TRUE(s_mock.deinitialized, "transport reports deinitialized");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "ENP v0.2 E2.1 mock transport adapter");
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Transport initialization tests");
	test_transport_initialization();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Receive callback tests");
	test_receive_registration();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Send tests");
	test_send();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Transport error tests");
	test_transport_errors();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "ENP context integration tests");
	test_context_integration();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "ENP context validation tests");
	test_context_invalid();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Transport deinitialization tests");
	test_transport_deinitialization();

	ESP_LOGI(TAG, "======================================");

	if (s_failures == 0) {
		ESP_LOGI(TAG, "ALL E2.1 MOCK TRANSPORT TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "%d E2.1 TEST(S) FAILED", s_failures);
	}

	ESP_LOGI(TAG, "======================================");
}
