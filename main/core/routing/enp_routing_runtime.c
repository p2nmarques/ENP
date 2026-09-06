/*
 * enp_routing_runtime.c
 *
 *  Created on: Aug 28, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-F; IG-G
 *
 * Dedicated production routing runtime owner.
 *
 * ESP-IDF target: 6.0.2
 */

#include "enp_routing_runtime.h"

#include <string.h>

#include "core/routing/enp_route_failure_coalescer.h"
#include "core/routing/enp_route_repair.h"
#include "core/routing/enp_rerr_generation.h"
#include "core/routing/enp_rerr_recipient_selection.h"

static enp_route_table_t s_route_table;
static enp_route_repair_t s_route_repair;
static enp_route_repair_adapter_t s_repair_adapter;
static enp_route_failure_coalescer_t s_failure_coalescer;
static enp_routing_data_path_t s_data_path;
static enp_rerr_generation_t s_rerr_generation;
static enp_rerr_recipient_selection_t s_rerr_recipient_selection;

static esp_err_t routing_runtime_rerr_submit(
    void *context, enp_route_destination_t recipient,
    const enp_packet_t *packet) {
    (void)context;
    return enp_routing_runtime_submit_packet(recipient, packet);
}

static bool s_initialized;

static void routing_runtime_route_failure_ex(
    void *context, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop,
    enp_route_destination_t upstream) {
    enp_route_failure_coalescer_t *coalescer = context;

    if (coalescer == NULL) {
        return;
    }

    /*
     * The extended E5C route-failure callback provides upstream forwarding
     * evidence, but the existing IG-D coalescer contract intentionally
     * coalesces failures using only (destination, failed_next_hop).
     *
     * Therefore upstream is deliberately terminated at this runtime
     * boundary. It must not be added to the coalescer event or propagated
     * into the existing E5D repair request without a separate contract
     * change.
     */
    (void)upstream;

    (void)enp_route_failure_coalescer_observe(
        coalescer, destination, failed_next_hop);
}

bool enp_routing_runtime_init(
    const enp_routing_runtime_config_t *config) {
    if (config == NULL || config->transport == NULL ||
        config->select_next_hop == NULL ||
        config->resolve_transport == NULL || config->now_ms == NULL ||
        config->local_address.network == 0U ||
        config->local_address.node == 0U || s_initialized) {
        return false;
    }

    if (!enp_route_table_init(&s_route_table)) {
        return false;
    }

    if (!enp_route_repair_adapter_init(
            &s_repair_adapter, &s_route_repair, &s_route_table,
            config->transport, config->local_address,
            config->select_next_hop, config->select_next_hop_context,
            config->resolve_transport, config->resolve_transport_context,
            config->now_ms, config->now_ms_context)) {
        return false;
    }

    if (!enp_route_repair_init(
            &s_route_repair,
            enp_route_repair_adapter_consume,
            &s_repair_adapter)) {
        return false;
    }

    if (!enp_route_failure_coalescer_init(
            &s_failure_coalescer, &s_route_repair)) {
        return false;
    }

    if (!enp_routing_data_path_init(
            &s_data_path, &s_route_table, config->transport,
            config->resolve_transport, config->resolve_transport_context)) {
        return false;
    }

    if (!enp_routing_data_path_set_route_failure_callback_ex(
            &s_data_path, routing_runtime_route_failure_ex,
            &s_failure_coalescer)) {
        return false;
    }

    if (!enp_rerr_recipient_selection_init(
            &s_rerr_recipient_selection, &s_route_table,
            config->local_address)) {
        return false;
    }

    if (!enp_rerr_generation_init(
            &s_rerr_generation, config->local_address,
            routing_runtime_rerr_submit, NULL,
            enp_rerr_recipient_selection_next,
            &s_rerr_recipient_selection)) {
        return false;
    }

    s_initialized = true;
    return true;
}

bool enp_routing_runtime_is_initialized(void) {
    return s_initialized;
}

enp_route_table_t *enp_routing_runtime_route_table(void) {
    return s_initialized ? &s_route_table : NULL;
}

enp_routing_data_path_t *enp_routing_runtime_data_path(void) {
    return s_initialized ? &s_data_path : NULL;
}

enp_route_repair_adapter_t *enp_routing_runtime_repair_adapter(void) {
    return s_initialized ? &s_repair_adapter : NULL;
}

/*
 * Test-only/read-only access to the runtime-owned IG-D component. This
 * accessor transfers no ownership and performs no routing operation.
 */
enp_route_failure_coalescer_t *enp_routing_runtime_failure_coalescer(void) {
    return s_initialized ? &s_failure_coalescer : NULL;
}

esp_err_t enp_routing_runtime_submit_packet(
    enp_route_destination_t recipient, const enp_packet_t *packet) {
    if (!s_initialized || packet == NULL || s_data_path.transport == NULL ||
        s_data_path.resolve_transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (recipient.network_id == 0U || recipient.node_id == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    enp_transport_address_t transport_address = {0};

    if (!s_data_path.resolve_transport(
            s_data_path.resolve_context, recipient, &transport_address)) {
        return ESP_ERR_NOT_FOUND;
    }

    const size_t frame_length = enp_packet_length(packet);
    if (frame_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return enp_transport_send(
        s_data_path.transport, &transport_address,
        enp_packet_data_const(packet), frame_length);
}


esp_err_t enp_routing_runtime_generate_authorized_rerr(
    enp_route_destination_t unreachable, enp_route_error_reason_t reason) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (unreachable.network_id == 0U || unreachable.node_id == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const enp_route_entry_t *route =
        enp_route_table_lookup_const(&s_route_table, unreachable);

    if (route == NULL || route->state == ENP_ROUTE_STATE_INVALID) {
        return ESP_ERR_NOT_FOUND;
    }

    const enp_rerr_generation_request_t request = {
        .unreachable = unreachable,
        .destination_sequence = route->route_sequence,
        .reason = reason,
    };

    return enp_rerr_generation_submit(&s_rerr_generation, &request);
}
