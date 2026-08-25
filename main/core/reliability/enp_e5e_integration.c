/*
 * enp_e5e_integration.c
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 */

#include "enp_e5e_integration.h"

#include "core/reliability/enp_reliability.h"
#include "core/routing/enp_routing_data_path.h"

static enp_routing_data_path_t *s_routing_path;
static bool s_initialized;

static esp_err_t e5e_reliability_submit(
    const enp_packet_t *packet,
    enp_reliability_handle_t handle,
    void *context) {
    enp_routing_data_path_t *path = (enp_routing_data_path_t *)context;
    if (path == NULL || packet == NULL || handle == ENP_RELIABILITY_INVALID_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }

    const enp_header_t *header = enp_packet_header_const(packet);
    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    enp_route_destination_t next_hop = {0};
    if (!enp_routing_data_path_get_next_hop(path, &header->destination, &next_hop)) {
        return ESP_ERR_NOT_FOUND;
    }

    enp_e5e_correlation_id_t correlation_id = ENP_E5E_INVALID_CORRELATION_ID;
    if (!enp_e5e_associate(handle, packet, next_hop, &correlation_id)) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = enp_routing_data_path_submit_correlated(
        path, packet, correlation_id);
    if (err != ESP_OK) {
        (void)enp_e5e_release(correlation_id);
    }
    return err;
}

static void e5e_transport_result(
    void *context,
    const enp_transport_address_t *destination,
    esp_err_t result,
    enp_transport_correlation_id_t correlation_id) {
    (void)destination;
    (void)context;

    if (!s_initialized || correlation_id == ENP_E5E_INVALID_CORRELATION_ID) {
        return;
    }

    (void)enp_e5e_on_transport_result(
        (enp_e5e_correlation_id_t)correlation_id, result);
}

static void e5e_reliability_result(
    enp_reliability_handle_t handle,
    enp_reliability_result_t result,
    void *context) {
    (void)context;

    if (!s_initialized) {
        return;
    }

    if (result == ENP_RELIABILITY_RESULT_DELIVERED ||
        result == ENP_RELIABILITY_RESULT_FAILED ||
        result == ENP_RELIABILITY_RESULT_CANCELLED) {
        (void)enp_e5e_release_handle(handle);
    }
}

bool enp_e5e_integration_init(
    enp_routing_data_path_t *routing_path,
    enp_e5e_repair_request_fn repair_request,
    void *repair_request_context) {
    if (routing_path == NULL || routing_path->transport == NULL ||
        repair_request == NULL || s_initialized) {
        return false;
    }

    if (!enp_e5e_init(repair_request, repair_request_context)) {
        return false;
    }

    if (!enp_reliability_set_submit_callback_ex(
            e5e_reliability_submit, routing_path) ||
        !enp_reliability_set_result_callback(
            e5e_reliability_result, NULL) ||
        !enp_routing_data_path_set_correlated_failure_callback(
            routing_path, e5e_transport_result, NULL)) {
        enp_e5e_deinit();
        return false;
    }

    s_routing_path = routing_path;
    s_initialized = true;
    return true;
}

void enp_e5e_integration_deinit(void) {
    if (!s_initialized) {
        return;
    }

    (void)enp_reliability_set_result_callback(NULL, NULL);
    enp_e5e_deinit();
    s_routing_path = NULL;
    s_initialized = false;
}
