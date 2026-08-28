/*
 * enp_routing_runtime.c
 *
 *  Created on: Aug 28, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-F
 *
 * Dedicated production routing runtime owner.
 *
 * ESP-IDF target: 6.0.2
 */

#include "enp_routing_runtime.h"

#include <string.h>

#include "core/routing/enp_route_failure_coalescer.h"
#include "core/routing/enp_route_repair.h"

static enp_route_table_t s_route_table;
static enp_route_repair_t s_route_repair;
static enp_route_repair_adapter_t s_repair_adapter;
static enp_route_failure_coalescer_t s_failure_coalescer;
static enp_routing_data_path_t s_data_path;

static bool s_initialized;

static void routing_runtime_route_failure(
    void *context, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop) {
    enp_route_failure_coalescer_t *coalescer = context;

    if (coalescer == NULL) {
        return;
    }

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

    if (!enp_routing_data_path_set_route_failure_callback(
            &s_data_path, routing_runtime_route_failure,
            &s_failure_coalescer)) {
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
