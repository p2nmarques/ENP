/*
 * enp_e5e.c
 *
*  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — Phase 4 / P4-E5E
 * Static Reliability <-> transport/repair correlation layer.
 *
 * ESP-IDF 6.0.2 compatible.
 */

#include "enp_e5e.h"

#include <string.h>

#include "core/enp_address.h"
#include "core/protocol/payloads/enp_data.h"

#define ENP_E5E_MAX_REPAIR_IDS ENP_E5E_MAX_CORRELATIONS

typedef struct {
    bool active;
    enp_e5e_correlation_id_t correlation_id;
    enp_reliability_handle_t handle;
    enp_reliability_repair_id_t repair_id;
    enp_address_t source;
    enp_address_t destination;
    enp_sequence_t data_sequence;
    uint32_t application_sequence;
    enp_route_destination_t failed_next_hop;
} enp_e5e_correlation_t;

static enp_e5e_correlation_t
    s_correlations[ENP_E5E_MAX_CORRELATIONS];

static bool s_initialized;
static enp_e5e_repair_request_fn s_repair_request;
static void *s_repair_request_context;
static enp_e5e_correlation_allocated_fn s_correlation_allocated_observer;
static void *s_correlation_allocated_context;
static enp_e5e_correlation_id_t s_next_correlation_id = 1U;
static enp_reliability_repair_id_t s_next_repair_id = 1U;

static enp_e5e_correlation_t *find_correlation(
    enp_e5e_correlation_id_t correlation_id) {
    if (correlation_id == ENP_E5E_INVALID_CORRELATION_ID) {
        return NULL;
    }

    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        if (s_correlations[i].active &&
            s_correlations[i].correlation_id == correlation_id) {
            return &s_correlations[i];
        }
    }
    return NULL;
}

static enp_e5e_correlation_t *find_handle(
    enp_reliability_handle_t handle) {
    if (handle == ENP_RELIABILITY_INVALID_HANDLE) {
        return NULL;
    }

    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        if (s_correlations[i].active && s_correlations[i].handle == handle) {
            return &s_correlations[i];
        }
    }
    return NULL;
}

static enp_e5e_correlation_t *allocate_correlation(void) {
    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        if (!s_correlations[i].active) {
            return &s_correlations[i];
        }
    }
    return NULL;
}

static uint32_t next_id(uint32_t *counter) {
    uint32_t id = *counter;
    if (id == 0U) {
        id = 1U;
    }
    ++(*counter);
    if (*counter == 0U) {
        *counter = 1U;
    }
    return id;
}

static bool packet_identity(const enp_packet_t *packet,
                            enp_address_t *source,
                            enp_address_t *destination,
                            enp_sequence_t *data_sequence,
                            uint32_t *application_sequence) {
    if ((packet == NULL) || !enp_packet_verify(packet) || source == NULL ||
        destination == NULL || data_sequence == NULL ||
        application_sequence == NULL) {
        return false;
    }

    const enp_header_t *header = enp_packet_header_const(packet);
    if (header == NULL || header->type != (uint8_t)ENP_PACKET_APPLICATION ||
        header->payload_length < ENP_DATA_HEADER_SIZE) {
        return false;
    }

    const enp_data_header_t *data_header =
        (const enp_data_header_t *)enp_packet_payload_const(packet);
    if (!enp_data_header_valid(data_header)) {
        return false;
    }

    *source = header->source;
    *destination = header->destination;
    *data_sequence = header->sequence;
    *application_sequence = data_header->application_sequence;
    return true;
}

bool enp_e5e_init(enp_e5e_repair_request_fn repair_request, void *context) {
    if (repair_request == NULL) {
        return false;
    }

    memset(s_correlations, 0, sizeof(s_correlations));
    s_initialized = true;
    s_repair_request = repair_request;
    s_repair_request_context = context;
    s_correlation_allocated_observer = NULL;
    s_correlation_allocated_context = NULL;
    s_next_correlation_id = 1U;
    s_next_repair_id = 1U;
    return true;
}

bool enp_e5e_set_correlation_allocated_observer(
    enp_e5e_correlation_allocated_fn observer, void *context) {
    if (!s_initialized) {
        return false;
    }
    s_correlation_allocated_observer = observer;
    s_correlation_allocated_context = context;
    return true;
}

void enp_e5e_deinit(void) {
    memset(s_correlations, 0, sizeof(s_correlations));
    s_initialized = false;
    s_repair_request = NULL;
    s_repair_request_context = NULL;
    s_correlation_allocated_observer = NULL;
    s_correlation_allocated_context = NULL;
    s_next_correlation_id = 1U;
    s_next_repair_id = 1U;
}

bool enp_e5e_associate(enp_reliability_handle_t handle,
                       const enp_packet_t *packet,
                       enp_route_destination_t failed_next_hop,
                       enp_e5e_correlation_id_t *correlation_id) {
    if (!s_initialized || correlation_id == NULL ||
        handle == ENP_RELIABILITY_INVALID_HANDLE ||
        failed_next_hop.network_id == 0U || failed_next_hop.node_id == 0U) {
        return false;
    }

    if (find_handle(handle) != NULL) {
        /* One active physical transmission association per Reliability
         * transaction. A retransmission must first retire the previous
         * transport result association. */
        return false;
    }

    enp_address_t source = {0};
    enp_address_t destination = {0};
    enp_sequence_t data_sequence = 0U;
    uint32_t application_sequence = 0U;
    if (!packet_identity(packet, &source, &destination, &data_sequence,
                         &application_sequence)) {
        return false;
    }

    enp_e5e_correlation_t *slot = allocate_correlation();
    if (slot == NULL) {
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->correlation_id =
        (enp_e5e_correlation_id_t)next_id(&s_next_correlation_id);
    slot->handle = handle;
    slot->repair_id = ENP_RELIABILITY_INVALID_REPAIR_ID;
    slot->source = source;
    slot->destination = destination;
    slot->data_sequence = data_sequence;
    slot->application_sequence = application_sequence;
    slot->failed_next_hop = failed_next_hop;

    *correlation_id = slot->correlation_id;

    if (s_correlation_allocated_observer != NULL) {
        s_correlation_allocated_observer(s_correlation_allocated_context,
                                         handle, slot->correlation_id);
    }

    return true;
}

bool enp_e5e_release(enp_e5e_correlation_id_t correlation_id) {
    enp_e5e_correlation_t *slot = find_correlation(correlation_id);
    if (slot == NULL) {
        return false;
    }
    memset(slot, 0, sizeof(*slot));
    return true;
}

bool enp_e5e_on_transport_result(enp_e5e_correlation_id_t correlation_id,
                                 esp_err_t result) {
    enp_e5e_correlation_t *slot = find_correlation(correlation_id);
    if (!s_initialized || slot == NULL) {
        return false;
    }

    if (result == ESP_OK) {
        /* Transport success does not complete Reliability; the ACK path does.
         * It only retires the physical-send correlation. */
        memset(slot, 0, sizeof(*slot));
        return true;
    }

    if (slot->repair_id != ENP_RELIABILITY_INVALID_REPAIR_ID) {
        return false;
    }

    const enp_reliability_repair_id_t repair_id =
        (enp_reliability_repair_id_t)next_id(&s_next_repair_id);

    if (!enp_reliability_begin_repair(slot->handle, repair_id)) {
        return false;
    }

    slot->repair_id = repair_id;

    if (!s_repair_request(s_repair_request_context,
                          (enp_route_destination_t){
                              .network_id = slot->destination.network,
                              .node_id = slot->destination.node},
                          slot->failed_next_hop, repair_id)) {
        (void)enp_reliability_repair_result(slot->handle, repair_id, false,
                                            0U);
        memset(slot, 0, sizeof(*slot));
        return false;
    }

    return true;
}

bool enp_e5e_on_repair_result(enp_reliability_repair_id_t repair_id,
                              bool success, uint32_t now_ms) {
    if (!s_initialized || repair_id == ENP_RELIABILITY_INVALID_REPAIR_ID) {
        return false;
    }

    bool matched = false;
    bool all_ok = true;

    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        enp_e5e_correlation_t *slot = &s_correlations[i];
        if (!slot->active || slot->repair_id != repair_id) {
            continue;
        }

        matched = true;
        const enp_reliability_handle_t handle = slot->handle;

        /* On successful repair, the Reliability API immediately performs the
         * normal retransmission. Retire the old physical-send correlation
         * before that call so the new handle-aware submit path can allocate a
         * fresh correlation for the retransmission. */
        if (success) {
            memset(slot, 0, sizeof(*slot));
        }

        const bool result_ok =
            enp_reliability_repair_result(handle, repair_id, success, now_ms);
        if (!result_ok) {
            all_ok = false;
            continue;
        }

        if (!success) {
            memset(slot, 0, sizeof(*slot));
        }
    }

    /* A duplicate/stale completion is intentionally harmless. */
    return matched && all_ok;
}

size_t enp_e5e_release_handle(enp_reliability_handle_t handle) {
    if (!s_initialized || handle == ENP_RELIABILITY_INVALID_HANDLE) {
        return 0U;
    }

    size_t released = 0U;
    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        if (s_correlations[i].active && s_correlations[i].handle == handle) {
            memset(&s_correlations[i], 0, sizeof(s_correlations[i]));
            ++released;
        }
    }
    return released;
}

bool enp_e5e_get_correlation(enp_reliability_handle_t handle,
                              enp_e5e_correlation_id_t *correlation_id) {
    if (!s_initialized || correlation_id == NULL) {
        return false;
    }

    enp_e5e_correlation_t *slot = find_handle(handle);
    if (slot == NULL) {
        return false;
    }

    *correlation_id = slot->correlation_id;
    return true;
}

size_t enp_e5e_active_count(void) {
    size_t count = 0U;
    for (size_t i = 0U; i < ENP_E5E_MAX_CORRELATIONS; ++i) {
        if (s_correlations[i].active) {
            ++count;
        }
    }
    return count;
}
