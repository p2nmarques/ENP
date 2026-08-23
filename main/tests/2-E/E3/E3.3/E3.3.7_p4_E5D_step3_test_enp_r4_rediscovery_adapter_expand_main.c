/*
 * E3.3.7 Phase 4 / P4-E5D Step 3
 *
 *  Created on: Aug 21, 2026
 *      Author: Pedro Marques
 *
 * R4 rediscovery orchestration adapter — controlled integration self-test.
 *
 * Scope deliberately excludes reliability and hardware validation.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include <string.h>

#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_routing.h"
#include "core/routing/enp_route_repair.h"
#include "core/routing/enp_route_repair_adapter.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_route_metric.h"

static const char *TAG = "E3_3_7_P4_E5D";

static enp_route_table_t s_routes;
static enp_route_repair_t s_repair;
static enp_route_repair_adapter_t s_adapter;

static uint32_t s_now_ms = 1000U;
static uint32_t s_tx_count;
static uint8_t s_last_tx[ENP_MAX_FRAME_SIZE];
static size_t s_last_tx_length;
static enp_transport_address_t s_last_tx_destination;

static bool fake_select_next_hop(void *context,
                                 enp_route_destination_t destination,
                                 enp_route_destination_t failed_next_hop,
                                 enp_route_destination_t *next_hop) {
    (void)context;
    (void)destination;

    if (next_hop == NULL) {
        return false;
    }

    /* Controlled topology: node 3 is the valid alternative to failed node 2. */
    if (failed_next_hop.node_id == 3U) {
        next_hop->network_id = failed_next_hop.network_id;
        next_hop->node_id = 4U;
    } else {
        next_hop->network_id = 1U;
        next_hop->node_id = 3U;
    }

    return true;
}

static bool fake_resolve_transport(void *context,
                                   enp_route_destination_t next_hop,
                                   enp_transport_address_t *address) {
    (void)context;
    if (address == NULL || next_hop.network_id == 0U || next_hop.node_id == 0U) {
        return false;
    }

    memset(address, 0, sizeof(*address));
    address->length = 6U;
    address->value[0] = 0x02U;
    address->value[1] = 0x00U;
    address->value[2] = 0x00U;
    address->value[3] = 0x00U;
    address->value[4] = (uint8_t)next_hop.network_id;
    address->value[5] = (uint8_t)next_hop.node_id;
    return true;
}

static uint32_t fake_now_ms(void *context) {
    (void)context;
    return s_now_ms;
}

static esp_err_t fake_send(const enp_transport_address_t *destination,
                           const void *data, size_t length) {
    if (destination == NULL || data == NULL || length > sizeof(s_last_tx)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_last_tx_destination = *destination;
    memcpy(s_last_tx, data, length);
    s_last_tx_length = length;
    ++s_tx_count;
    return ESP_OK;
}

static esp_err_t fake_init(const enp_config_t *config) {
    (void)config;
    return ESP_OK;
}

static esp_err_t fake_deinit(void) { return ESP_OK; }

static esp_err_t fake_set_receive(enp_transport_receive_callback_t callback) {
    (void)callback;
    return ESP_OK;
}

static esp_err_t fake_set_send_result(enp_transport_send_result_callback_t callback,
                                      void *context) {
    (void)callback;
    (void)context;
    return ESP_OK;
}

static enp_transport_t s_transport = {
    .init = fake_init,
    .deinit = fake_deinit,
    .send = fake_send,
    .set_receive_callback = fake_set_receive,
    .set_send_result_callback = fake_set_send_result,
};

static bool install_route(enp_route_destination_t destination,
                          enp_route_destination_t next_hop,
                          uint32_t sequence, enp_route_state_t state) {
    enp_route_entry_t entry = {0};
    entry.destination = destination;
    entry.next_hop = next_hop;
    entry.route_sequence = sequence;
    entry.expires_at_ms = s_now_ms + 10000U;
    entry.state = state;

    if (!enp_route_metric_init(&entry.metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
        return false;
    }
    entry.metric.value = 1U;
    entry.metric.valid = true;

    if (state == ENP_ROUTE_STATE_STALE) {
        /* Route-table insertion accepts STALE entries, but the test needs the
         * exact retained sequence metadata. */
        return enp_route_table_insert(&s_routes, &entry);
    }

    return enp_route_table_insert(&s_routes, &entry);
}

static void check(bool condition, const char *message, bool *failed) {
    if (condition) {
        ESP_LOGI(TAG, "PASS: %s", message);
    } else {
        ESP_LOGE(TAG, "FAIL: %s", message);
        *failed = true;
    }
}

void app_main(void) {
    bool failed = false;

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5D STEP 3");
    ESP_LOGI(TAG, "R4 rediscovery orchestration adapter");
    ESP_LOGI(TAG, "Controlled integration self-test");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "Reliability and hardware excluded");
    ESP_LOGI(TAG, "======================================");

    check(enp_route_table_init(&s_routes), "route table initialized", &failed);

    const enp_route_destination_t destination = {.network_id = 1U, .node_id = 10U};
    const enp_route_destination_t failed_next_hop = {.network_id = 1U, .node_id = 2U};
    const enp_route_destination_t unrelated_destination = {.network_id = 1U, .node_id = 12U};
    const enp_route_destination_t unrelated_next_hop = {.network_id = 1U, .node_id = 3U};

    check(install_route(destination, failed_next_hop, 7U, ENP_ROUTE_STATE_ACTIVE),
          "route A installed with sequence 7", &failed);
    check(install_route(unrelated_destination, unrelated_next_hop, 4U,
                        ENP_ROUTE_STATE_ACTIVE),
          "unrelated route C installed", &failed);

    check(enp_route_table_invalidate(&s_routes, destination),
          "route A invalidated to STALE before repair", &failed);

    const enp_transport_address_t dummy = {.length = 6U,
                                           .value = {0x02U, 0, 0, 0, 1, 3}};
    (void)dummy;

    check(enp_route_repair_adapter_init(
              &s_adapter, &s_repair, &s_routes, &s_transport,
              (enp_address_t){.network = 1U, .node = 1U},
              fake_select_next_hop, NULL, fake_resolve_transport, NULL,
              fake_now_ms, NULL),
          "Step-3 adapter initialized", &failed);

    check(enp_route_repair_init(&s_repair,
                                enp_route_repair_adapter_consume,
                                &s_adapter),
          "E5D repair coordinator initialized with Step-3 consume boundary",
          &failed);

    check(enp_route_repair_request(&s_repair, destination, failed_next_hop),
          "E5D repair request accepted", &failed);

    vTaskDelay(pdMS_TO_TICKS(50U));

    check(enp_route_repair_adapter_is_active(&s_adapter),
          "E5D request created an active R4 discovery", &failed);
    check(s_adapter.discovery.destination_sequence == 7U,
          "stale route sequence 7 carried into R4-A", &failed);
    check(s_tx_count == 1U, "initial RREQ transmitted exactly once", &failed);

    enp_packet_t tx_packet;
    memset(&tx_packet, 0, sizeof(tx_packet));
    if (s_last_tx_length <= sizeof(tx_packet)) {
        memcpy(enp_packet_data(&tx_packet), s_last_tx, s_last_tx_length);
    }

    const enp_header_t *tx_header = enp_packet_header_const(&tx_packet);
    check(tx_header != NULL && tx_header->type == ENP_PACKET_ROUTE,
          "RREQ uses ENP route packet", &failed);
    check(tx_header != NULL && tx_header->destination.node == 10U,
          "RREQ destination is repair destination", &failed);
    check(tx_header != NULL && tx_header->sequence != 0U,
          "RREQ has a valid ENP packet sequence", &failed);

    enp_routing_rreq_t tx_rreq = {0};
    if (enp_packet_payload_const(&tx_packet) != NULL) {
        memcpy(&tx_rreq, enp_packet_payload_const(&tx_packet), sizeof(tx_rreq));
    }
    check(tx_rreq.route_request_id != 0U && tx_rreq.destination_sequence == 7U,
          "RREQ carries repair discovery request identity and retained sequence",
          &failed);
    check(s_last_tx_destination.value[5] == 3U,
          "RREQ selected allowed alternative next-hop 3", &failed);

    enp_routing_rrep_t failed_rrep = {
        .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
        .subtype = ENP_ROUTING_SUBTYPE_RREP,
        .destination_network_id = 1U,
        .destination_node_id = 10U,
        .destination_sequence = 7U,
        .hop_count = 1U,
        .reserved_0 = 0U,
        .route_lifetime_ms = 10000U,
        .reserved_1 = 0U};

    enp_rrep_result_t result = enp_route_repair_adapter_handle_rrep(
        &s_adapter, (enp_rrep_node_t){.network_id = 1U, .node_id = 2U},
        &failed_rrep, &(enp_address_t){.network = 1U, .node = 10U}, 2U);

    check(result == ENP_RREP_RESULT_REJECT,
          "RREP candidate using failed next-hop is rejected", &failed);
    check(s_adapter.rejected_failed_next_hop_count == 1U,
          "failed-next-hop rejection counted exactly once", &failed);
    check(enp_route_table_lookup_const(&s_routes, destination) == NULL,
          "failed candidate did not install an ACTIVE route", &failed);
    check(enp_route_repair_adapter_is_active(&s_adapter),
          "discovery remains active after failed candidate", &failed);

    enp_routing_rrep_t valid_rrep = failed_rrep;
    result = enp_route_repair_adapter_handle_rrep(
        &s_adapter, (enp_rrep_node_t){.network_id = 1U, .node_id = 3U},
        &valid_rrep, &(enp_address_t){.network = 1U, .node = 10U}, 3U);

    check(result == ENP_RREP_RESULT_COMPLETE,
          "RREP candidate using alternative next-hop completes discovery", &failed);

    const enp_route_entry_t *repaired =
        enp_route_table_lookup_const(&s_routes, destination);
    check(repaired != NULL && repaired->state == ENP_ROUTE_STATE_ACTIVE,
          "repaired route is ACTIVE", &failed);
    check(repaired != NULL && repaired->next_hop.node_id == 3U,
          "repaired route uses alternative next-hop 3", &failed);
    check(repaired != NULL && repaired->route_sequence == 7U,
          "repaired route preserves destination sequence 7", &failed);
    check(!enp_route_repair_adapter_is_active(&s_adapter),
          "repair lifecycle is complete", &failed);

    /* ---------------------------------------------------------------
     * Controlled failure-path expansion:
     *   - old/insufficient RREP sequence is rejected;
     *   - duplicate repair while active is suppressed by the adapter;
     *   - a second affected destination is queued behind the active repair;
     *   - discovery timeout retries the RREQ and eventually fails;
     *   - the queued destination then starts its own discovery;
     *   - the failed destination remains STALE after repair failure.
     * --------------------------------------------------------------- */
    const enp_route_destination_t second_destination =
        {.network_id = 1U, .node_id = 11U};
    check(install_route(second_destination, failed_next_hop, 9U,
                        ENP_ROUTE_STATE_ACTIVE),
          "route B installed with sequence 9", &failed);
    check(enp_route_table_invalidate(&s_routes, destination),
          "route A invalidated again for timeout validation", &failed);
    check(enp_route_table_invalidate(&s_routes, second_destination),
          "route B invalidated to STALE before queued repair", &failed);

    const uint32_t tx_before_second_repair = s_tx_count;
    check(enp_route_repair_request(&s_repair, destination, failed_next_hop),
          "second repair request for route A accepted", &failed);
    vTaskDelay(pdMS_TO_TICKS(50U));
    check(enp_route_repair_adapter_is_active(&s_adapter),
          "second repair request created a new active discovery", &failed);

    check(enp_route_repair_request(&s_repair, destination, failed_next_hop),
          "duplicate route A repair request accepted by E5D boundary", &failed);
    vTaskDelay(pdMS_TO_TICKS(50U));
    check(s_adapter.pending_repair_count == 0U,
          "duplicate route A repair request suppressed while active", &failed);

    check(enp_route_repair_request(&s_repair, second_destination,
                                   failed_next_hop),
          "route B repair request accepted while route A is active", &failed);
    vTaskDelay(pdMS_TO_TICKS(50U));
    check(s_adapter.pending_repair_count == 1U,
          "second destination queued behind active repair", &failed);

    enp_routing_rrep_t old_rrep = valid_rrep;
    old_rrep.destination_sequence = 6U;
    enp_rrep_result_t old_result = enp_route_repair_adapter_handle_rrep(
        &s_adapter, (enp_rrep_node_t){.network_id = 1U, .node_id = 3U},
        &old_rrep, &(enp_address_t){.network = 1U, .node = 10U}, 3U);

    check(old_result == ENP_RREP_RESULT_REJECT,
          "old RREP sequence is rejected", &failed);
    check(enp_route_repair_adapter_is_active(&s_adapter),
          "old RREP leaves discovery active", &failed);
    check(enp_route_table_lookup_const(&s_routes, destination) == NULL,
          "old RREP does not install an ACTIVE route", &failed);

    const uint32_t tx_before_timeout = s_tx_count;

    /*
     * R4-A owns the retry budget. ENP_MAX_RETRIES timeout events produce
     * ENP_MAX_RETRIES retransmissions; the following timeout transitions the
     * discovery to FAILED. The failure transition immediately activates the
     * next queued E5D repair, so the adapter may already be active again by
     * the time the final timeout returns. Validate route-A failure separately
     * from overall adapter activity.
     */
    for (uint32_t retry = 0U; retry < ENP_MAX_RETRIES; ++retry) {
        s_now_ms = s_adapter.discovery.deadline_ms;
        (void)enp_route_repair_adapter_tick(&s_adapter, s_now_ms);
    }

    check(s_tx_count == tx_before_timeout + ENP_MAX_RETRIES,
          "route A timeout produced exactly ENP_MAX_RETRIES retransmissions",
          &failed);

    /* One additional timeout transitions route A discovery to FAILED and
     * starts the queued route-B repair. Its initial RREQ is therefore sent
     * after the route-A retry-count assertion above. */
    s_now_ms = s_adapter.discovery.deadline_ms;
    (void)enp_route_repair_adapter_tick(&s_adapter, s_now_ms);

    check(enp_route_repair_adapter_is_active(&s_adapter),
          "route A discovery failed and queued repair became active", &failed);
    check(enp_route_repair_adapter_active_request(&s_adapter) != NULL &&
              enp_route_repair_adapter_active_request(&s_adapter)->destination.node_id ==
                  second_destination.node_id,
          "route A failure handed active ownership to queued route B", &failed);

    const enp_route_entry_t *failed_route =
        enp_route_table_lookup_const(&s_routes, destination);
    check(failed_route == NULL,
          "failed route A remains without an ACTIVE route", &failed);

    bool route_a_stale = false;
    for (size_t i = 0U; i < s_routes.count; ++i) {
        if (destination.node_id == s_routes.entries[i].destination.node_id &&
            destination.network_id == s_routes.entries[i].destination.network_id) {
            route_a_stale = s_routes.entries[i].state == ENP_ROUTE_STATE_STALE &&
                            s_routes.entries[i].route_sequence == 7U;
            break;
        }
    }
    check(route_a_stale,
          "failed route A remains STALE and preserves sequence 7", &failed);

    check(enp_route_repair_adapter_is_active(&s_adapter),
          "queued route B repair started after route A failure", &failed);
    check(enp_route_repair_adapter_active_request(&s_adapter) != NULL &&
              enp_route_repair_adapter_active_request(&s_adapter)->destination.node_id ==
                  second_destination.node_id,
          "queued route B became the active repair destination", &failed);
    check(s_tx_count == tx_before_timeout + ENP_MAX_RETRIES + 1U,
          "queued route B transmitted its initial RREQ exactly once", &failed);

    enp_packet_t route_b_tx_packet;
    memset(&route_b_tx_packet, 0, sizeof(route_b_tx_packet));
    if (s_last_tx_length <= sizeof(route_b_tx_packet)) {
        memcpy(enp_packet_data(&route_b_tx_packet), s_last_tx, s_last_tx_length);
    }
    enp_routing_rreq_t route_b_tx_rreq = {0};
    if (enp_packet_payload_const(&route_b_tx_packet) != NULL) {
        memcpy(&route_b_tx_rreq, enp_packet_payload_const(&route_b_tx_packet),
               sizeof(route_b_tx_rreq));
    }
    check(route_b_tx_rreq.destination_node_id == second_destination.node_id &&
              route_b_tx_rreq.destination_sequence == 9U,
          "queued route B RREQ preserves its destination and sequence", &failed);

    enp_routing_rrep_t route_b_rrep = valid_rrep;
    route_b_rrep.destination_node_id = second_destination.node_id;
    route_b_rrep.destination_sequence = 9U;
    enp_rrep_result_t route_b_result = enp_route_repair_adapter_handle_rrep(
        &s_adapter, (enp_rrep_node_t){.network_id = 1U, .node_id = 3U},
        &route_b_rrep, &(enp_address_t){.network = 1U, .node = 11U}, 3U);

    check(route_b_result == ENP_RREP_RESULT_COMPLETE,
          "queued route B alternative RREP completes discovery", &failed);

    const enp_route_entry_t *repaired_b =
        enp_route_table_lookup_const(&s_routes, second_destination);
    check(repaired_b != NULL && repaired_b->state == ENP_ROUTE_STATE_ACTIVE,
          "route B is ACTIVE after its independent repair", &failed);
    check(repaired_b != NULL && repaired_b->next_hop.node_id == 3U,
          "route B uses alternative next-hop 3", &failed);
    check(repaired_b != NULL && repaired_b->route_sequence == 9U,
          "route B preserves destination sequence 9", &failed);

    check(enp_route_repair_adapter_active_request(&s_adapter) == NULL,
          "all controlled repair work is complete", &failed);

    const enp_route_entry_t *unrelated =
        enp_route_table_lookup_const(&s_routes, unrelated_destination);
    check(unrelated != NULL && unrelated->next_hop.node_id == 3U &&
              unrelated->route_sequence == 4U,
          "unrelated route remains unchanged", &failed);

    check(s_tx_count >= tx_before_second_repair + 1U,
          "controlled repair sequence produced additional RREQ traffic", &failed);

    ESP_LOGI(TAG, "--------------------------------------");
    if (failed) {
        ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5D Step 3 self-test FAIL");
    } else {
        ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5D Step 3 self-test PASS");
    }
    ESP_LOGI(TAG, "======================================");
}
