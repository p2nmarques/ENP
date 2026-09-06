/*
 * enp_rerr_generation.c
 *
 *  Created on: Sep 5, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-G.4
 * Production RERR packet construction and generation service.
 *
 * ESP-IDF target: 6.0.2
 */

#include "enp_rerr_generation.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "enp_rerr_generation";

static bool destination_valid(enp_route_destination_t destination)
{
    return destination.network_id != 0U && destination.node_id != 0U;
}

static bool reason_valid(enp_route_error_reason_t reason)
{
    switch (reason) {
    case ENP_ROUTE_ERROR_NO_ROUTE:
    case ENP_ROUTE_ERROR_NEXT_HOP_UNREACHABLE:
    case ENP_ROUTE_ERROR_ROUTE_EXPIRED:
    case ENP_ROUTE_ERROR_LOCAL_REPAIR_FAILED:
    case ENP_ROUTE_ERROR_TTL_EXPIRED:
        return true;
    case ENP_ROUTE_ERROR_UNKNOWN:
    default:
        return false;
    }
}

static uint32_t allocate_packet_sequence(enp_rerr_generation_t *generation)
{
    uint32_t sequence = generation->next_packet_sequence;

    if (sequence == 0U) {
        sequence = 1U;
    }

    ++generation->next_packet_sequence;
    if (generation->next_packet_sequence == 0U) {
        generation->next_packet_sequence = 1U;
    }

    return sequence;
}

static esp_err_t build_packet(
    enp_rerr_generation_t *generation,
    const enp_rerr_generation_request_t *request,
    enp_route_destination_t recipient,
    enp_packet_t *packet)
{
    if (generation == NULL || request == NULL || packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!destination_valid(request->unreachable) ||
        !destination_valid(recipient) ||
        (recipient.network_id == generation->local_address.network &&
         recipient.node_id == generation->local_address.node) ||
        !reason_valid(request->reason)) {
        return ESP_ERR_INVALID_ARG;
    }

    enp_packet_init(packet, ENP_PACKET_ROUTE, &generation->local_address);

    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    header->destination.network = recipient.network_id;
    header->destination.node = recipient.node_id;
    header->flags = ENP_FLAG_NONE;
    header->sequence = allocate_packet_sequence(generation);
    header->ttl = ENP_DEFAULT_TTL;

    enp_routing_rerr_t *rerr =
        (enp_routing_rerr_t *)enp_packet_payload(packet);

    if (rerr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *rerr = (enp_routing_rerr_t){
        .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
        .subtype = ENP_ROUTING_SUBTYPE_RERR,
        .unreachable_network_id = request->unreachable.network_id,
        .unreachable_node_id = request->unreachable.node_id,
        .destination_sequence = request->destination_sequence,
        .reason = (uint8_t)request->reason,
        .reserved_0 = 0U,
        .reserved_1 = 0U,
    };

    return enp_packet_seal(packet, ENP_ROUTING_RERR_WIRE_SIZE);
}

bool enp_rerr_generation_init(
    enp_rerr_generation_t *generation,
    enp_address_t local_address,
    enp_rerr_submit_fn submit,
    void *submit_context,
    enp_rerr_recipient_fn recipient,
    void *recipient_context)
{
    if (generation == NULL || submit == NULL || recipient == NULL ||
        local_address.network == 0U || local_address.node == 0U) {
        return false;
    }

    memset(generation, 0, sizeof(*generation));

    generation->local_address = local_address;
    generation->submit = submit;
    generation->submit_context = submit_context;
    generation->recipient = recipient;
    generation->recipient_context = recipient_context;
    generation->next_packet_sequence = 1U;
    generation->initialized = true;

    return true;
}

esp_err_t enp_rerr_generation_submit(
    enp_rerr_generation_t *generation,
    const enp_rerr_generation_request_t *request)
{
    if (generation == NULL || request == NULL ||
        !generation->initialized || generation->submit == NULL ||
        generation->recipient == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!destination_valid(request->unreachable) ||
        !reason_valid(request->reason)) {
        ++generation->rejected_count;
        return ESP_ERR_INVALID_ARG;
    }

    ++generation->generated_count;

    size_t submitted = 0U;

    for (size_t index = 0U;
         index < ENP_RERR_GENERATION_MAX_RECIPIENTS;
         ++index) {
        enp_route_destination_t recipient = {0};

        if (!generation->recipient(
                generation->recipient_context, request, index, &recipient)) {
            break;
        }

        if (!destination_valid(recipient) ||
            (recipient.network_id == generation->local_address.network &&
             recipient.node_id == generation->local_address.node)) {
            ESP_LOGW(TAG,
                     "Rejecting invalid/local RERR recipient: "
                     "network=%u node=%u",
                     (unsigned)recipient.network_id,
                     (unsigned)recipient.node_id);
            ++generation->rejected_count;
            continue;
        }

        enp_packet_t packet;
        const esp_err_t build_err =
            build_packet(generation, request, recipient, &packet);

        if (build_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "RERR packet construction failed: err=%s",
                     esp_err_to_name(build_err));
            ++generation->rejected_count;
            continue;
        }

        const esp_err_t submit_err = generation->submit(
            generation->submit_context, recipient, &packet);

        if (submit_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "RERR submit failed: destination=%u/%u err=%s",
                     (unsigned)recipient.network_id,
                     (unsigned)recipient.node_id,
                     esp_err_to_name(submit_err));
            ++generation->submit_failure_count;
            return submit_err;
        }

        ++generation->submitted_count;
        ++submitted;

        ESP_LOGI(TAG,
                 "RERR submitted: unreachable=%u/%u seq=%lu "
                 "reason=%u recipient=%u/%u",
                 (unsigned)request->unreachable.network_id,
                 (unsigned)request->unreachable.node_id,
                 (unsigned long)request->destination_sequence,
                 (unsigned)request->reason,
                 (unsigned)recipient.network_id,
                 (unsigned)recipient.node_id);
    }

    if (submitted == 0U) {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}
