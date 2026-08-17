/*
 * enp_reliability.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.3.7 Reliability Layer
 * ESP-IDF 6.0.2 compatible.
 */

#include "enp_reliability.h"

#include <string.h>

#include "core/enp_address.h"
#include "core/protocol/payloads/enp_ack.h"
#include "core/protocol/payloads/enp_data.h"

/* --------------------------------------------------------------------------
 * Internal transaction representation
 * -------------------------------------------------------------------------- */

typedef struct {
	bool active;

	enp_reliability_handle_t handle;

	enp_address_t origin;
	enp_address_t destination;

	enp_sequence_t data_sequence;
	uint32_t application_sequence;

	uint8_t retry_count;
	uint32_t deadline_ms;

	enp_reliability_state_t state;

	/* Static retransmission copy. */
	enp_packet_t packet;

} enp_reliability_transaction_t;

static enp_reliability_transaction_t
	s_transactions[ENP_RELIABILITY_MAX_TRANSACTIONS];

static bool s_initialized;
static bool s_started;

static enp_reliability_submit_fn s_submit;
static void *s_submit_context;

static enp_reliability_result_fn s_result;
static void *s_result_context;

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static enp_reliability_transaction_t *
find_transaction(enp_reliability_handle_t handle) {
	if (handle == ENP_RELIABILITY_INVALID_HANDLE) {
		return NULL;
	}

	for (size_t i = 0U; i < ENP_RELIABILITY_MAX_TRANSACTIONS; ++i) {
		if (s_transactions[i].active && s_transactions[i].handle == handle) {
			return &s_transactions[i];
		}
	}

	return NULL;
}

static enp_reliability_transaction_t *allocate_transaction(void) {
	for (size_t i = 0U; i < ENP_RELIABILITY_MAX_TRANSACTIONS; ++i) {
		if (!s_transactions[i].active) {
			return &s_transactions[i];
		}
	}

	return NULL;
}

static enp_reliability_handle_t make_handle(size_t index) {
	return (enp_reliability_handle_t)(index + 1U);
}

static size_t
transaction_index(const enp_reliability_transaction_t *transaction) {
	return (size_t)(transaction - s_transactions);
}

static void notify_result(enp_reliability_handle_t handle,
						  enp_reliability_result_t result) {
	if (s_result != NULL) {
		s_result(handle, result, s_result_context);
	}
}

static bool packet_is_reliable_data(const enp_packet_t *packet) {
	if ((packet == NULL) || !enp_packet_verify(packet)) {
		return false;
	}

	const enp_header_t *header = enp_packet_header_const(packet);

	if ((header == NULL) || header->type != (uint8_t)ENP_PACKET_APPLICATION ||
		(header->flags & ENP_FLAG_ACK_REQUIRED) == 0U) {
		return false;
	}

	if (header->sequence == 0U || header->source.node == ENP_NODE_BROADCAST ||
		header->destination.node == ENP_NODE_BROADCAST) {
		return false;
	}

	if (header->payload_length < ENP_DATA_HEADER_SIZE) {
		return false;
	}

	const enp_data_header_t *data_header =
		(const enp_data_header_t *)enp_packet_payload_const(packet);

	return enp_data_header_valid(data_header) &&
		   enp_data_payload_length_valid(data_header,
										 (size_t)header->payload_length -
											 ENP_DATA_HEADER_SIZE);
}

static bool
ack_matches_transaction(const enp_reliability_transaction_t *transaction,
						const enp_packet_t *ack_packet) {
	if ((transaction == NULL) || !transaction->active || (ack_packet == NULL) ||
		!enp_packet_verify(ack_packet)) {
		return false;
	}

	const enp_header_t *header = enp_packet_header_const(ack_packet);

	if ((header == NULL) || header->type != (uint8_t)ENP_PACKET_ACK ||
		header->payload_length != ENP_ACK_WIRE_SIZE) {
		return false;
	}

	if (!enp_address_equal(&header->source, &transaction->destination) ||
		!enp_address_equal(&header->destination, &transaction->origin)) {
		return false;
	}

	const enp_ack_payload_t *ack =
		(const enp_ack_payload_t *)enp_packet_payload_const(ack_packet);

	if (!enp_ack_payload_valid(ack)) {
		return false;
	}

	return ack->data_packet_sequence == transaction->data_sequence &&
		   ack->application_sequence == transaction->application_sequence;
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void complete_transaction(enp_reliability_transaction_t *transaction,
								 enp_reliability_state_t state,
								 enp_reliability_result_t result) {
	if (transaction == NULL) {
		return;
	}

	const enp_reliability_handle_t handle = transaction->handle;

	transaction->state = state;
	transaction->active = false;

	notify_result(handle, result);
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

bool enp_reliability_init(void) {
	memset(s_transactions, 0, sizeof(s_transactions));

	s_initialized = true;
	s_started = false;

	s_submit = NULL;
	s_submit_context = NULL;
	s_result = NULL;
	s_result_context = NULL;

	return true;
}

bool enp_reliability_start(void) {
	if (!s_initialized) {
		return false;
	}

	s_started = true;
	return true;
}

void enp_reliability_deinit(void) {
	s_started = false;
	s_initialized = false;

	memset(s_transactions, 0, sizeof(s_transactions));

	s_submit = NULL;
	s_submit_context = NULL;
	s_result = NULL;
	s_result_context = NULL;
}

/* --------------------------------------------------------------------------
 * Integration callbacks
 * -------------------------------------------------------------------------- */

bool enp_reliability_set_submit_callback(enp_reliability_submit_fn submit,
										 void *user_context) {
	if (!s_initialized || (submit == NULL)) {
		return false;
	}

	s_submit = submit;
	s_submit_context = user_context;
	return true;
}

bool enp_reliability_set_result_callback(enp_reliability_result_fn result,
										 void *user_context) {
	if (!s_initialized) {
		return false;
	}

	s_result = result;
	s_result_context = user_context;
	return true;
}

/* --------------------------------------------------------------------------
 * Transaction API
 * -------------------------------------------------------------------------- */

bool enp_reliability_send(const enp_packet_t *packet, uint32_t now_ms,
						  enp_reliability_handle_t *handle) {
	if ((handle == NULL) || !s_initialized || !s_started ||
		(s_submit == NULL) || !packet_is_reliable_data(packet)) {
		return false;
	}

	enp_reliability_transaction_t *transaction = allocate_transaction();

	if (transaction == NULL) {
		notify_result(ENP_RELIABILITY_INVALID_HANDLE,
					  ENP_RELIABILITY_RESULT_NO_RESOURCES);
		return false;
	}

	memset(transaction, 0, sizeof(*transaction));

	const size_t index = transaction_index(transaction);

	transaction->active = true;
	transaction->handle = make_handle(index);
	transaction->state = ENP_RELIABILITY_STATE_CREATED;
	transaction->packet = *packet;

	const enp_header_t *header = enp_packet_header_const(packet);

	const enp_data_header_t *data_header =
		(const enp_data_header_t *)enp_packet_payload_const(packet);

	transaction->origin = header->source;
	transaction->destination = header->destination;
	transaction->data_sequence = header->sequence;
	transaction->application_sequence = data_header->application_sequence;
	transaction->retry_count = 0U;
	transaction->deadline_ms = now_ms + ENP_RELIABILITY_ACK_TIMEOUT_MS;

	/* The initial submission is not a retry. */
	const esp_err_t err = s_submit(&transaction->packet, s_submit_context);

	if (err != ESP_OK) {
		transaction->active = false;
		transaction->state = ENP_RELIABILITY_STATE_FAILED;
		return false;
	}

	transaction->state = ENP_RELIABILITY_STATE_WAITING_FOR_ACK;

	*handle = transaction->handle;
	return true;
}

bool enp_reliability_process_ack(const enp_packet_t *ack_packet,
								 uint32_t now_ms) {
	(void)now_ms;

	if (!s_initialized || !s_started || (ack_packet == NULL)) {
		return false;
	}

	const enp_header_t *header = enp_packet_header_const(ack_packet);

	if ((header == NULL) || header->type != (uint8_t)ENP_PACKET_ACK ||
		!enp_packet_verify(ack_packet)) {
		return false;
	}

	const enp_ack_payload_t *ack =
		(const enp_ack_payload_t *)enp_packet_payload_const(ack_packet);

	if (!enp_ack_payload_valid(ack)) {
		return false;
	}

	for (size_t i = 0U; i < ENP_RELIABILITY_MAX_TRANSACTIONS; ++i) {
		enp_reliability_transaction_t *transaction = &s_transactions[i];

		if (!transaction->active) {
			continue;
		}

		if (!ack_matches_transaction(transaction, ack_packet)) {
			continue;
		}

		complete_transaction(transaction, ENP_RELIABILITY_STATE_DELIVERED,
							 ENP_RELIABILITY_RESULT_DELIVERED);

		return true;
	}

	/*
	 * A duplicate ACK after completion is intentionally harmless. The
	 * completed transaction is no longer active, so it cannot complete
	 * anything a second time.
	 */
	return false;
}

void enp_reliability_tick(uint32_t now_ms) {
	if (!s_initialized || !s_started || (s_submit == NULL)) {
		return;
	}

	for (size_t i = 0U; i < ENP_RELIABILITY_MAX_TRANSACTIONS; ++i) {
		enp_reliability_transaction_t *transaction = &s_transactions[i];

		if (!transaction->active ||
			transaction->state != ENP_RELIABILITY_STATE_WAITING_FOR_ACK ||
			!time_reached(now_ms, transaction->deadline_ms)) {
			continue;
		}

		if (transaction->retry_count >= ENP_RELIABILITY_MAX_RETRIES) {
			complete_transaction(transaction, ENP_RELIABILITY_STATE_FAILED,
								 ENP_RELIABILITY_RESULT_FAILED);
			continue;
		}

		transaction->retry_count++;
		transaction->state = ENP_RELIABILITY_STATE_RETRYING;

		const esp_err_t err = s_submit(&transaction->packet, s_submit_context);

		if (err != ESP_OK) {
			complete_transaction(transaction, ENP_RELIABILITY_STATE_FAILED,
								 ENP_RELIABILITY_RESULT_FAILED);
			continue;
		}

		transaction->deadline_ms = now_ms + ENP_RELIABILITY_ACK_TIMEOUT_MS;
		transaction->state = ENP_RELIABILITY_STATE_WAITING_FOR_ACK;
	}
}

bool enp_reliability_get_state(enp_reliability_handle_t handle,
							   enp_reliability_state_t *state) {
	if ((state == NULL) || !s_initialized) {
		return false;
	}

	enp_reliability_transaction_t *transaction = find_transaction(handle);

	if (transaction == NULL) {
		return false;
	}

	*state = transaction->state;
	return true;
}

bool enp_reliability_get_retry_count(enp_reliability_handle_t handle,
									 uint8_t *retry_count) {
	if ((retry_count == NULL) || !s_initialized) {
		return false;
	}

	enp_reliability_transaction_t *transaction = find_transaction(handle);

	if (transaction == NULL) {
		return false;
	}

	*retry_count = transaction->retry_count;
	return true;
}

bool enp_reliability_cancel(enp_reliability_handle_t handle) {
	if (!s_initialized) {
		return false;
	}

	enp_reliability_transaction_t *transaction = find_transaction(handle);

	if (transaction == NULL) {
		return false;
	}

	complete_transaction(transaction, ENP_RELIABILITY_STATE_CANCELLED,
						 ENP_RELIABILITY_RESULT_CANCELLED);

	return true;
}

/* --------------------------------------------------------------------------
 * Phase 1 self-test
 * -------------------------------------------------------------------------- */

#define SELFTEST_NETWORK_ID 1U
#define SELFTEST_ORIGIN_NODE 1U
#define SELFTEST_DEST_NODE 3U
#define SELFTEST_DATA_SEQUENCE 0x7001U
#define SELFTEST_APP_SEQUENCE 0x0042U
#define SELFTEST_ACK_SEQUENCE 0x9001U
#define SELFTEST_PAYLOAD_TEXT "E3.3.7-SELFTEST"

static unsigned s_selftest_submit_count;
static unsigned s_selftest_result_count;
static enp_reliability_result_t s_selftest_last_result;
static enp_packet_t s_selftest_last_submitted;

static esp_err_t selftest_submit(const enp_packet_t *packet,
								 void *user_context) {
	(void)user_context;

	if (packet == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	s_selftest_submit_count++;
	s_selftest_last_submitted = *packet;
	return ESP_OK;
}

static void selftest_result(enp_reliability_handle_t handle,
							enp_reliability_result_t result,
							void *user_context) {
	(void)handle;
	(void)user_context;

	s_selftest_result_count++;
	s_selftest_last_result = result;
}

static bool selftest_make_data(enp_packet_t *packet) {
	const enp_address_t origin = {.network = SELFTEST_NETWORK_ID,
								  .node = SELFTEST_ORIGIN_NODE};

	const enp_address_t destination = {.network = SELFTEST_NETWORK_ID,
									   .node = SELFTEST_DEST_NODE};

	static const uint8_t payload[] = SELFTEST_PAYLOAD_TEXT;

	enp_packet_init(packet, ENP_PACKET_APPLICATION, &origin);

	enp_header_t *header = enp_packet_header(packet);
	if (header == NULL) {
		return false;
	}

	header->destination = destination;
	header->flags = ENP_FLAG_ACK_REQUIRED;
	header->sequence = SELFTEST_DATA_SEQUENCE;

	enp_data_header_t *data_header =
		(enp_data_header_t *)enp_packet_payload(packet);

	enp_data_header_init(data_header, ENP_DATA_SUBTYPE_APPLICATION,
						 ENP_DATA_FLAG_NONE, SELFTEST_APP_SEQUENCE,
						 (uint16_t)sizeof(payload));

	memcpy((uint8_t *)data_header + ENP_DATA_HEADER_SIZE, payload,
		   sizeof(payload));

	return enp_packet_seal(packet, (uint16_t)(ENP_DATA_HEADER_SIZE +
											  sizeof(payload))) == ESP_OK;
}

static bool selftest_make_ack(enp_packet_t *packet) {
	const enp_address_t origin = {.network = SELFTEST_NETWORK_ID,
								  .node = SELFTEST_DEST_NODE};

	const enp_address_t destination = {.network = SELFTEST_NETWORK_ID,
									   .node = SELFTEST_ORIGIN_NODE};

	enp_packet_init(packet, ENP_PACKET_ACK, &origin);

	enp_header_t *header = enp_packet_header(packet);
	if (header == NULL) {
		return false;
	}

	header->destination = destination;
	header->sequence = SELFTEST_ACK_SEQUENCE;

	enp_ack_payload_t *ack = (enp_ack_payload_t *)enp_packet_payload(packet);

	enp_ack_payload_init(ack, SELFTEST_DATA_SEQUENCE, SELFTEST_APP_SEQUENCE);

	return enp_packet_seal(packet, ENP_ACK_WIRE_SIZE) == ESP_OK;
}

static bool selftest_expect(bool condition) { return condition; }

bool enp_reliability_self_test(void) {
	enp_packet_t data;
	enp_packet_t ack;
	enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;

	s_selftest_submit_count = 0U;
	s_selftest_result_count = 0U;
	s_selftest_last_result = ENP_RELIABILITY_RESULT_NONE;
	memset(&s_selftest_last_submitted, 0, sizeof(s_selftest_last_submitted));

	if (!enp_reliability_init() ||
		!enp_reliability_set_submit_callback(selftest_submit, NULL) ||
		!enp_reliability_set_result_callback(selftest_result, NULL) ||
		!enp_reliability_start()) {
		return false;
	}

	/* Test 1: reliable DATA creates a pending transaction. */
	if (!selftest_make_data(&data) ||
		!enp_reliability_send(&data, 1000U, &handle) ||
		handle == ENP_RELIABILITY_INVALID_HANDLE ||
		s_selftest_submit_count != 1U) {
		enp_reliability_deinit();
		return false;
	}

	/* The retransmission copy must preserve the transaction identity. */
	if (memcmp(&data, &s_selftest_last_submitted, sizeof(data)) != 0) {
		enp_reliability_deinit();
		return false;
	}

	enp_reliability_state_t state = ENP_RELIABILITY_STATE_INVALID;
	if (!enp_reliability_get_state(handle, &state) ||
		state != ENP_RELIABILITY_STATE_WAITING_FOR_ACK) {
		enp_reliability_deinit();
		return false;
	}

	/* Test 2: one timeout causes exactly one retransmission. */
	enp_reliability_tick(1999U);
	if (s_selftest_submit_count != 1U) {
		enp_reliability_deinit();
		return false;
	}

	enp_reliability_tick(2000U);
	if (s_selftest_submit_count != 2U) {
		enp_reliability_deinit();
		return false;
	}

	uint8_t retry_count = 0U;
	if (!enp_reliability_get_retry_count(handle, &retry_count) ||
		retry_count != 1U) {
		enp_reliability_deinit();
		return false;
	}

	if (memcmp(&data, &s_selftest_last_submitted, sizeof(data)) != 0) {
		enp_reliability_deinit();
		return false;
	}

	/* Test 3: a valid correlated ACK completes the transaction once. */
	if (!selftest_make_ack(&ack) || !enp_reliability_process_ack(&ack, 2050U) ||
		s_selftest_result_count != 1U ||
		s_selftest_last_result != ENP_RELIABILITY_RESULT_DELIVERED) {
		enp_reliability_deinit();
		return false;
	}

	/* A duplicate ACK must not create a second completion. */
	if (enp_reliability_process_ack(&ack, 2100U) ||
		s_selftest_result_count != 1U) {
		enp_reliability_deinit();
		return false;
	}

	/* Test 4: fresh transaction exhausts exactly three retries. */
	handle = ENP_RELIABILITY_INVALID_HANDLE;
	s_selftest_result_count = 0U;
	s_selftest_last_result = ENP_RELIABILITY_RESULT_NONE;

	if (!enp_reliability_send(&data, 3000U, &handle)) {
		enp_reliability_deinit();
		return false;
	}

	enp_reliability_tick(4000U);
	enp_reliability_tick(5000U);
	enp_reliability_tick(6000U);

	if (s_selftest_submit_count != 6U) {
		enp_reliability_deinit();
		return false;
	}

	/* No fourth retry: the next timeout transitions to FAILED. */
	enp_reliability_tick(7000U);

	if (s_selftest_submit_count != 6U || s_selftest_result_count != 1U ||
		s_selftest_last_result != ENP_RELIABILITY_RESULT_FAILED) {
		enp_reliability_deinit();
		return false;
	}

	if (!selftest_expect(true)) {
		enp_reliability_deinit();
		return false;
	}

	enp_reliability_deinit();
	return true;
}
