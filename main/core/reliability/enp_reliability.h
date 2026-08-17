/*
 * enp_reliability.h
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.3.7 Reliability Layer
 * ESP-IDF 6.0.2 compatible.
 *
 * Phase 1 implementation:
 *   - fixed-size transaction table
 *   - ACK correlation
 *   - timeout/retry state machine
 *   - duplicate ACK completion suppression
 *   - transport/routing-independent submit callback
 *
 * The reliability layer owns transaction state. Routing/transport owns
 * packet forwarding and transmission.
 */

#ifndef ENP_RELIABILITY_H
#define ENP_RELIABILITY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/protocol/enp_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Initial E3.3.7 implementation parameters
 * -------------------------------------------------------------------------- */

#define ENP_RELIABILITY_ACK_TIMEOUT_MS 1000U
#define ENP_RELIABILITY_MAX_RETRIES 3U
#define ENP_RELIABILITY_MAX_TRANSACTIONS 8U

/* --------------------------------------------------------------------------
 * Public types
 * -------------------------------------------------------------------------- */

typedef uint16_t enp_reliability_handle_t;

#define ENP_RELIABILITY_INVALID_HANDLE ((enp_reliability_handle_t)0U)

typedef enum {
	ENP_RELIABILITY_STATE_INVALID = 0,
	ENP_RELIABILITY_STATE_CREATED,
	ENP_RELIABILITY_STATE_WAITING_FOR_ACK,
	ENP_RELIABILITY_STATE_RETRYING,
	ENP_RELIABILITY_STATE_DELIVERED,
	ENP_RELIABILITY_STATE_FAILED,
	ENP_RELIABILITY_STATE_CANCELLED
} enp_reliability_state_t;

typedef enum {
	ENP_RELIABILITY_RESULT_NONE = 0,
	ENP_RELIABILITY_RESULT_PENDING,
	ENP_RELIABILITY_RESULT_DELIVERED,
	ENP_RELIABILITY_RESULT_FAILED,
	ENP_RELIABILITY_RESULT_CANCELLED,
	ENP_RELIABILITY_RESULT_NO_RESOURCES
} enp_reliability_result_t;

/*
 * Submit callback used by the reliability layer to hand a DATA packet to
 * the next ENP layer. The callback does not transfer ownership of packet;
 * the reliability layer retains its own static copy for retransmission.
 */
typedef esp_err_t (*enp_reliability_submit_fn)(const enp_packet_t *packet,
											   void *user_context);

/*
 * Optional transaction result callback.
 */
typedef void (*enp_reliability_result_fn)(enp_reliability_handle_t handle,
										  enp_reliability_result_t result,
										  void *user_context);

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

bool enp_reliability_init(void);

bool enp_reliability_start(void);

void enp_reliability_deinit(void);

/* --------------------------------------------------------------------------
 * Integration callbacks
 * -------------------------------------------------------------------------- */

bool enp_reliability_set_submit_callback(enp_reliability_submit_fn submit,
										 void *user_context);

bool enp_reliability_set_result_callback(enp_reliability_result_fn result,
										 void *user_context);

/* --------------------------------------------------------------------------
 * Transaction API
 * -------------------------------------------------------------------------- */

bool enp_reliability_send(const enp_packet_t *packet, uint32_t now_ms,
						  enp_reliability_handle_t *handle);

bool enp_reliability_process_ack(const enp_packet_t *ack_packet,
								 uint32_t now_ms);

void enp_reliability_tick(uint32_t now_ms);

bool enp_reliability_get_state(enp_reliability_handle_t handle,
							   enp_reliability_state_t *state);

bool enp_reliability_get_retry_count(enp_reliability_handle_t handle,
									 uint8_t *retry_count);

bool enp_reliability_cancel(enp_reliability_handle_t handle);

/* --------------------------------------------------------------------------
 * E3.3.7 Phase 1 self-test
 * -------------------------------------------------------------------------- */

bool enp_reliability_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ENP_RELIABILITY_H */
