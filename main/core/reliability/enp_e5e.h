/*
 * enp_e5e.h
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — Phase 4 / P4-E5E
 * Static Reliability <-> transport/repair correlation layer.
 *
 * ESP-IDF 6.0.2 compatible.
 */

#ifndef ENP_E5E_H
#define ENP_E5E_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/protocol/enp_packet.h"
#include "core/reliability/enp_reliability.h"
#include "core/routing/enp_route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENP_E5E_MAX_CORRELATIONS ENP_RELIABILITY_MAX_TRANSACTIONS

typedef uint32_t enp_e5e_correlation_id_t;
#define ENP_E5E_INVALID_CORRELATION_ID ((enp_e5e_correlation_id_t)0U)

/* E5D remains unaware of Reliability. This callback is the E5E -> repair
 * request boundary. The repair_id is owned by E5E and is returned later by
 * the repair-observation boundary. */
typedef bool (*enp_e5e_repair_request_fn)(
    void *context, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop,
    enp_reliability_repair_id_t repair_id);

/* Optional diagnostic/test observer invoked whenever a new physical-send
 * correlation is allocated. The observer does not own or retain the slot. */
typedef void (*enp_e5e_correlation_allocated_fn)(
    void *context, enp_reliability_handle_t handle,
    enp_e5e_correlation_id_t correlation_id);

/*
 * Initialise the bounded E5E correlation table.
 * No dynamic allocation is performed.
 */
bool enp_e5e_init(enp_e5e_repair_request_fn repair_request, void *context);

/* Install an optional bounded diagnostic/test observer. */
bool enp_e5e_set_correlation_allocated_observer(
    enp_e5e_correlation_allocated_fn observer, void *context);

void enp_e5e_deinit(void);

/*
 * Associate one Reliability transaction with one physical DATA submission.
 * The association must be created before the asynchronous transport result
 * can arrive. The packet is copied only for identity/audit purposes; E5E does
 * not take ownership of the Reliability packet.
 */
bool enp_e5e_associate(enp_reliability_handle_t handle,
                       const enp_packet_t *packet,
                       enp_route_destination_t failed_next_hop,
                       enp_e5e_correlation_id_t *correlation_id);

/*
 * Release an active physical-send association without changing Reliability.
 * Used for successful transport completion and terminal transaction cleanup.
 */
bool enp_e5e_release(enp_e5e_correlation_id_t correlation_id);

/*
 * Resolve an asynchronous transport result against an exact correlation ID.
 * ESP_OK releases the physical-send association. A transport failure moves
 * the Reliability transaction to REPAIR_PENDING and requests E5D repair.
 */
bool enp_e5e_on_transport_result(enp_e5e_correlation_id_t correlation_id,
                                 esp_err_t result);

/*
 * Deliver an E5D repair-operation completion to all E5E transaction
 * associations carrying that repair_id. Duplicate/stale completions are
 * harmless.
 */
bool enp_e5e_on_repair_result(enp_reliability_repair_id_t repair_id,
                                bool success, uint32_t now_ms);

/*
 * Release every correlation associated with a completed/cancelled
 * Reliability transaction. This never changes Reliability state.
 */
size_t enp_e5e_release_handle(enp_reliability_handle_t handle);

bool enp_e5e_get_correlation(enp_reliability_handle_t handle,
                              enp_e5e_correlation_id_t *correlation_id);

size_t enp_e5e_active_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ENP_E5E_H */
