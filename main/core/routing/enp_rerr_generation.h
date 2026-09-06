/*
 * enp_rerr_generation.h
 *
 *  Created on: Sep 5, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-G.4
 * Production RERR packet construction and generation service.
 *
 * ESP-IDF target: 6.0.2
 */
#ifndef ENP_RERR_GENERATION_H
#define ENP_RERR_GENERATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/enp_address.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_routing.h"
#include "core/routing/enp_route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENP_RERR_GENERATION_MAX_RECIPIENTS
#define ENP_RERR_GENERATION_MAX_RECIPIENTS 8U
#endif

/* Intrinsic information carried by one generated RERR. */
typedef struct {
    enp_route_destination_t unreachable;
    enp_route_sequence_t destination_sequence;
    enp_route_error_reason_t reason;
} enp_rerr_generation_request_t;

/*
 * Enumerate explicitly identified dependent recipients.
 *
 * Return true and write *recipient for a valid recipient at index.
 * Return false when no more recipients are available.
 */
typedef bool (*enp_rerr_recipient_fn)(
    void *context,
    const enp_rerr_generation_request_t *request,
    size_t index,
    enp_route_destination_t *recipient);

/*
 * Submit a fully constructed ENP RERR packet through the existing transport
 * integration boundary. This callback does not call esp_now_send() itself.
 */
typedef esp_err_t (*enp_rerr_submit_fn)(
    void *context,
    enp_route_destination_t recipient,
    const enp_packet_t *packet);

typedef struct {
    bool initialized;
    enp_address_t local_address;

    enp_rerr_submit_fn submit;
    void *submit_context;

    enp_rerr_recipient_fn recipient;
    void *recipient_context;

    uint32_t next_packet_sequence;
    uint32_t generated_count;
    uint32_t submitted_count;
    uint32_t rejected_count;
    uint32_t submit_failure_count;
} enp_rerr_generation_t;

bool enp_rerr_generation_init(
    enp_rerr_generation_t *generation,
    enp_address_t local_address,
    enp_rerr_submit_fn submit,
    void *submit_context,
    enp_rerr_recipient_fn recipient,
    void *recipient_context);

/* Generate and submit one RERR for each explicitly enumerated recipient. */
esp_err_t enp_rerr_generation_submit(
    enp_rerr_generation_t *generation,
    const enp_rerr_generation_request_t *request);

#ifdef __cplusplus
}
#endif

#endif /* ENP_RERR_GENERATION_H */
