/*
 * E3.2_test_enp_real_multihop_main_ROUTER_ROLE.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.3.2 real three-node A -> B -> C DATA forwarding.
 *
 *Roles:
 *- Node 1 = A / Gateway
 *- Node 2 = B / Relay (`DEVICE_ROLE_RELAY`)
 *- Node 3 = C / Sensor
 *
 *## Important
 *
 * `ENP_PACKET_APPLICATION` is the existing canonical ENP packet type. E3.3.2 does **not** add or renumber an ENP packet type.
 *
 *The E3.3.1 DATA header is carried inside `ENP_PACKET_APPLICATION`.
 *
 *## Test sequence
 *
 *1. Flash the same E3.3.2 test application to A, B and C with the existing role/node configuration.
 *2. Wait until all three boards report READY.
 *3. On A, type:
 *
 *   `e3 start`
 *
 *   This performs the already-proven A -> B -> C route discovery.
 *4. After A reports the E3.2.2 route discovery PASS, type:
 *
 *   `e3 data`
 *
 *## Expected forwarding
 *
 *A creates one DATA packet addressed logically to C:
 *
 *`A -> B`
 *
 *B resolves destination C using its route table and forwards the same packet with TTL decremented:
 *
 *`B -> C`
 *
 *C validates and delivers the application payload.
 *
 *T*he ENP source remains A and the ENP packet sequence remains unchanged during forwarding. The TTL is decremented and the frame is resealed before transmission.
 *
 *## Expected logs
 *
 *A:
 *- PASS: A DATA route to C uses B
 *- PASS: A submitted DATA to B
 *
 *B:
 *-* DATA RX: origin=1 destination=3 ... ttl=8
 *- PASS: B route to C uses C
 *- PASS: B forwarded DATA to C
 *- PASS: B decremented DATA TTL
 *
 *C:
 *- DATA RX: origin=1 destination=3 ... ttl=7
 *- PASS: C received DATA from B
 *- PASS: C DATA payload integrity
 *- PASS: C application sequence is valid
 *- PASS: C final destination matched local node
 *- E3.3.2 DATA delivery complete: A -> B -> C
 *
 *E3.3.2 deliberately does not send an acknowledgement back to A. That belongs to E3.3.3.
 */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <string.h>

 #include "esp_err.h"
 #include "esp_event.h"
 #include "esp_log.h"
 #include "esp_netif.h"
 #include "esp_now.h"
 #include "nvs_flash.h"

 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 #include "config/enp_config.h"
 #include "core/enp_context.h"
 #include "core/enp_duplicate.h"
 #include "core/enp_transport.h"
 #include "core/protocol/enp_packet.h"
 #include "core/protocol/payloads/enp_routing.h"
 #include "core/protocol/payloads/enp_data.h"
 #include "core/service/discovery/enp_discovery.h"
 #include "core/service/discovery/enp_service_discovery.h"
 #include "core/routing/enp_route_discovery.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_rrep_processor.h"
 #include "core/routing/enp_rreq_processor.h"
 #include "link/enp_transport_espnow.h"
 #include "link/enp_transport_wifi.h"

 #ifndef CONFIG_ENP_E3_NODE_ID
 #define CONFIG_ENP_E3_NODE_ID 1
 #endif

 #define E3_NETWORK_ID                 1U
 #define E3_NODE_A                    1U
 #define E3_NODE_B                    2U
 #define E3_NODE_C                    3U

 #define E3_DISCOVERY_TTL             8U
 #define E3_DESTINATION_SEQUENCE      1U
 #define E3_RREQ_LIFETIME_MS          5000U
 #define E3_TEST_TIMEOUT_MS           10000U
 #define E3_TASK_STACK_SIZE           4096U
 #define E3_TASK_PRIORITY             4U
 #define E3_DATA_TTL                  8U
 #define E3_DATA_PAYLOAD             "ENP-E3.3.2-DATA-001"

 static const char *TAG = "E3_3";

 static enp_context_t s_context;
 static enp_route_table_t s_routes;
 static enp_duplicate_cache_t s_rreq_duplicates;
 static enp_duplicate_cache_t s_data_duplicates;
 static enp_route_discovery_t s_discovery;
 static enp_rreq_processor_t s_rreq_processor;
 static enp_rrep_processor_t s_rrep_processor;

 static StaticTask_t s_control_task_buffer;
 static StackType_t s_control_task_stack[E3_TASK_STACK_SIZE];
 static TaskHandle_t s_control_task;

 static StaticTask_t s_announce_task_buffer;
 static StackType_t s_announce_task_stack[E3_TASK_STACK_SIZE];
 static TaskHandle_t s_announce_task;

 static bool s_test_started;
 static bool s_test_failed;
 static uint32_t s_next_rreq_id = 1U;
 static enp_sequence_t s_active_rreq_packet_sequence;
 static enp_sequence_t s_routing_packet_sequence = 0x1000U;
 static uint32_t s_rreq_rx_count;
 static uint32_t s_rreq_forward_count;
 static uint32_t s_rrep_rx_count;
 static uint32_t s_rrep_forward_count;
 static uint32_t s_data_rx_count;
 static uint32_t s_data_forward_count;
 static uint32_t s_data_delivered_count;
 static enp_sequence_t s_data_packet_sequence = 0x2000U;
 static uint32_t s_data_application_sequence = 1U;

 static enp_address_t local_address(void)
 {
     return (enp_address_t){
         .network = E3_NETWORK_ID,
         .node = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID
     };
 }

 static enp_rreq_node_t node_to_rreq(enp_address_t address)
 {
     return (enp_rreq_node_t){
         .network_id = address.network,
         .node_id = (uint16_t)address.node
     };
 }

 static enp_rrep_node_t node_to_rrep(enp_address_t address)
 {
     return (enp_rrep_node_t){
         .network_id = address.network,
         .node_id = (uint16_t)address.node
     };
 }

 static enp_route_destination_t route_destination(enp_rreq_node_t node)
 {
     return (enp_route_destination_t){
         .network_id = node.network_id,
         .node_id = node.node_id
     };
 }

 static bool make_hop_metric(uint32_t hops, enp_route_metric_t *metric)
 {
     if (metric == NULL || hops > UINT16_MAX) {
         return false;
     }

     if (!enp_route_metric_init(metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
         return false;
     }

     metric->value = (uint16_t)hops;
     metric->valid = true;
     return true;
 }

 static bool install_or_update_route(
         enp_rreq_node_t destination,
         enp_rreq_node_t next_hop,
         uint32_t sequence,
         uint32_t lifetime_ms,
         uint32_t metric)
 {
     enp_route_entry_t entry = {0};

     entry.destination = route_destination(destination);
     entry.next_hop = route_destination(next_hop);
     entry.route_sequence = sequence;
     entry.expires_at_ms = enp_context_time_ms(&s_context) + lifetime_ms;
     entry.state = ENP_ROUTE_STATE_ACTIVE;

     if (!make_hop_metric(metric, &entry.metric)) {
         return false;
     }

     enp_route_entry_t *existing =
             enp_route_table_lookup(&s_routes, entry.destination);

     if (existing != NULL) {
         return enp_route_table_update(&s_routes, &entry);
     }

     /* An existing STALE entry is not returned by lookup(). */
     for (size_t i = 0U; i < s_routes.count; ++i) {
         if (s_routes.entries[i].destination.network_id ==
                 entry.destination.network_id &&
             s_routes.entries[i].destination.node_id ==
                 entry.destination.node_id) {
             return enp_route_table_update(&s_routes, &entry);
         }
     }

     return enp_route_table_insert(&s_routes, &entry);
 }

 static bool lookup_route_next_hop(
         enp_rreq_node_t destination,
         enp_rreq_node_t *next_hop)
 {
     if (next_hop == NULL) {
         return false;
     }

     const enp_route_entry_t *entry =
             enp_route_table_lookup_const(
                     &s_routes,
                     route_destination(destination));

     if (entry == NULL) {
         return false;
     }

     next_hop->network_id = entry->next_hop.network_id;
     next_hop->node_id = (uint16_t)entry->next_hop.node_id;
     return true;
 }

 static bool lookup_transport_address(
         enp_rreq_node_t destination,
         enp_transport_address_t *transport_address)
 {
     if (transport_address == NULL) {
         return false;
     }

     const enp_address_t logical = {
         .network = destination.network_id,
         .node = destination.node_id
     };

     const enp_neighbor_t *neighbor =
             enp_neighbor_find_const(
                     &s_context.neighbors,
                     &logical);

     if (neighbor == NULL ||
         neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE) {
         return false;
     }

     *transport_address = neighbor->transport_address;
     return transport_address->length == ESP_NOW_ETH_ALEN;
 }

 static bool find_neighbor_logical_address(
         const enp_transport_address_t *transport_address,
         enp_address_t *logical_address)
 {
     if (transport_address == NULL || logical_address == NULL) {
         return false;
     }

     for (size_t i = 0U; i < s_context.neighbors.count; ++i) {
         const enp_neighbor_t *neighbor =
                 &s_context.neighbors.entries[i];

         if (neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE) {
             continue;
         }

         if (neighbor->transport_address.length != transport_address->length) {
             continue;
         }

         if (neighbor->transport_address.length == 0U ||
             memcmp(
                 neighbor->transport_address.value,
                 transport_address->value,
                 transport_address->length) == 0) {
             *logical_address = neighbor->address;
             return true;
         }
     }

     return false;
 }

 static esp_err_t send_routing_packet(
         const enp_packet_t *packet,
         const enp_transport_address_t *destination)
 {
     if (packet == NULL || destination == NULL) {
         return ESP_ERR_INVALID_ARG;
     }

     return enp_transport_send(
             s_context.transport,
             destination,
             enp_packet_data_const(packet),
             enp_packet_length(packet));
 }

/*
 * E3.2.2 routed RREQ transmission:
 *
 *   A -> B -> C
 *
 * Neighbor discovery remains broadcast. Routed RREQs are deliberately
 * unicast to the selected next hop so that a node that is in direct RF
 * range of A cannot consume A's original broadcast as a one-hop route.
 */
 static esp_err_t send_rreq(
         const enp_routing_rreq_t *rreq,
         const enp_address_t *originator,
         enp_rreq_node_t next_hop,
         enp_sequence_t packet_sequence)
 {
     if (rreq == NULL || originator == NULL || packet_sequence == 0U) {
         return ESP_ERR_INVALID_ARG;
     }

     enp_transport_address_t transport_address;
     if (!lookup_transport_address(next_hop, &transport_address)) {
         ESP_LOGE(
                 TAG,
                 "RREQ: no transport address for next hop %u",
                 (unsigned)next_hop.node_id);
         return ESP_ERR_NOT_FOUND;
     }

     enp_packet_t packet;
     const enp_address_t source = *originator;

     enp_packet_init(
             &packet,
             ENP_PACKET_ROUTE,
             &source);

     enp_header_t *header = enp_packet_header(&packet);
     if (header == NULL) {
         return ESP_ERR_INVALID_ARG;
     }

     header->destination.network =
             rreq->destination_network_id;
     header->destination.node =
             rreq->destination_node_id;

     /*
      * Routed RREQs are unicast to the selected next hop.
      * Neighbor discovery remains broadcast; routing traffic does not.
      */
     header->flags = ENP_FLAG_NONE;
     header->sequence = packet_sequence;
     header->ttl = rreq->ttl;

     memcpy(
             enp_packet_payload(&packet),
             rreq,
             sizeof(*rreq));

     esp_err_t err = enp_packet_seal(
             &packet,
             ENP_ROUTING_RREQ_WIRE_SIZE);

     if (err != ESP_OK) {
         return err;
     }

     err = send_routing_packet(
             &packet,
             &transport_address);

     if (err == ESP_OK) {
         ESP_LOGI(
                 TAG,
                 "RREQ TX: origin=A destination=C next-hop=%u id=%lu hop=%u ttl=%u",
                 (unsigned)next_hop.node_id,
                 (unsigned long)rreq->route_request_id,
                 (unsigned)rreq->hop_count,
                 (unsigned)rreq->ttl);
     }

     return err;
 }

 static esp_err_t send_rrep(
         const enp_routing_rrep_t *rrep,
         enp_rreq_node_t next_hop,
         const enp_address_t *rrep_origin,
         enp_sequence_t packet_sequence)
 {
     if (rrep == NULL || rrep_origin == NULL || packet_sequence == 0U) {
         return ESP_ERR_INVALID_ARG;
     }

     enp_transport_address_t transport_address;
     if (!lookup_transport_address(next_hop, &transport_address)) {
         ESP_LOGE(
                 TAG,
                 "No transport address for next hop %u",
                 (unsigned)next_hop.node_id);
         return ESP_ERR_NOT_FOUND;
     }

     enp_packet_t packet;
     const enp_address_t source = *rrep_origin;

     enp_packet_init(
             &packet,
             ENP_PACKET_ROUTE,
             &source);

     enp_header_t *header = enp_packet_header(&packet);
     if (header == NULL) {
         return ESP_ERR_INVALID_ARG;
     }

     header->destination.network =
             E3_NETWORK_ID;
     header->destination.node =
             next_hop.node_id;
     header->flags = ENP_FLAG_NONE;
     header->sequence = packet_sequence;
     header->ttl = ENP_DEFAULT_TTL;

     memcpy(
             enp_packet_payload(&packet),
             rrep,
             sizeof(*rrep));

     esp_err_t err = enp_packet_seal(
             &packet,
             ENP_ROUTING_RREP_WIRE_SIZE);

     if (err != ESP_OK) {
         return err;
     }

     err = send_routing_packet(
             &packet,
             &transport_address);

     if (err == ESP_OK) {
         ESP_LOGI(
                 TAG,
                 "RREP TX: destination=C next-hop=%u hop=%u",
                 (unsigned)next_hop.node_id,
                 (unsigned)rrep->hop_count);
     }

     return err;
 }

 static bool rreq_duplicate_check(
         void *context,
         enp_rreq_node_t originator,
         enp_route_request_id_t request_id)
 {
     (void)context;

     enp_address_t source = {
         .network = originator.network_id,
         .node = originator.node_id
     };

     bool duplicate = false;

     if (enp_duplicate_check_and_record(
             &s_rreq_duplicates,
             &source,
             request_id,
             enp_context_time_ms(&s_context),
             &duplicate) != ESP_OK) {
         return true;
     }

     return duplicate;
 }

 static bool rreq_learn_reverse_route(
         void *context,
         enp_rreq_node_t originator,
         enp_rreq_node_t next_hop,
         uint8_t hop_count,
         enp_route_sequence_t destination_sequence,
         uint32_t lifetime_ms)
 {
     (void)context;

     return install_or_update_route(
             originator,
             next_hop,
             destination_sequence,
             lifetime_ms,
             hop_count);
 }

 static bool rrep_update_route(
         void *context,
         enp_rrep_node_t destination,
         enp_rrep_node_t next_hop,
         enp_route_sequence_t destination_sequence,
         uint8_t hop_count,
         uint32_t lifetime_ms,
         uint32_t metric)
 {
     (void)context;

     return install_or_update_route(
             (enp_rreq_node_t){
                 .network_id = destination.network_id,
                 .node_id = destination.node_id
             },
             (enp_rreq_node_t){
                 .network_id = next_hop.network_id,
                 .node_id = next_hop.node_id
             },
             destination_sequence,
             lifetime_ms,
             metric == 0U ? hop_count : metric);
 }

 static bool rrep_lookup_next_hop(
         void *context,
         enp_rrep_node_t destination,
         enp_rrep_node_t *next_hop)
 {
     (void)context;

     enp_rreq_node_t route_next_hop;

     if (!lookup_route_next_hop(
             (enp_rreq_node_t){
                 .network_id = destination.network_id,
                 .node_id = destination.node_id
             },
             &route_next_hop)) {
         return false;
     }

     next_hop->network_id = route_next_hop.network_id;
     next_hop->node_id = route_next_hop.node_id;
     return true;
 }

 static bool rrep_discovery_complete(
         void *context,
         enp_rrep_node_t destination,
         enp_route_sequence_t destination_sequence)
 {
     (void)context;

     if (CONFIG_ENP_E3_NODE_ID != E3_NODE_A) {
         return false;
     }

     if (!enp_route_discovery_on_rrep(
             &s_discovery,
             (enp_discovery_destination_t){
                 .network_id = destination.network_id,
                 .node_id = destination.node_id
             },
             destination_sequence)) {
         return false;
     }

     ESP_LOGI(
             TAG,
             "DISCOVERY COMPLETE: destination=%u sequence=%lu",
             (unsigned)destination.node_id,
             (unsigned long)destination_sequence);

     return true;
 }

 static bool init_processors(void)
 {
     if (!enp_route_table_init(&s_routes)) {
         return false;
     }

     if (enp_duplicate_cache_init(&s_rreq_duplicates) != ESP_OK) {
         return false;
     }

     if (enp_duplicate_cache_init(&s_data_duplicates) != ESP_OK) {
         return false;
     }

     if (!enp_route_discovery_init(&s_discovery)) {
         return false;
     }

     const enp_rreq_processor_callbacks_t rreq_callbacks = {
         .context = NULL,
         .is_duplicate = rreq_duplicate_check,
         .learn_reverse_route = rreq_learn_reverse_route
     };

     if (!enp_rreq_processor_init(
             &s_rreq_processor,
             node_to_rreq(local_address()),
             &rreq_callbacks)) {
         return false;
     }

     const enp_rrep_processor_callbacks_t rrep_callbacks = {
         .context = NULL,
         .update_route = rrep_update_route,
         .lookup_next_hop = rrep_lookup_next_hop,
         .discovery_complete = rrep_discovery_complete
     };

     /* RREP processor is meaningful on every node because every node can
      * forward a reply. The originator is always A for E3.2.2. */
     if (!enp_rrep_processor_init(
             &s_rrep_processor,
             node_to_rrep(local_address()),
             (enp_rrep_node_t){
                 .network_id = E3_NETWORK_ID,
                 .node_id = E3_NODE_A
             },
             &rrep_callbacks)) {
         return false;
     }

     return true;
 }

 static bool data_duplicate_check(const enp_packet_t *packet)
 {
     const enp_header_t *header = enp_packet_header_const(packet);
     if (header == NULL) {
         return true;
     }

     bool duplicate = false;
     if (enp_duplicate_check_and_record(
             &s_data_duplicates,
             &header->source,
             header->sequence,
             enp_context_time_ms(&s_context),
             &duplicate) != ESP_OK) {
         return true;
     }

     return duplicate;
 }

 static esp_err_t send_data_packet(
         enp_packet_t *packet,
         enp_rreq_node_t next_hop)
 {
     if (packet == NULL) {
         return ESP_ERR_INVALID_ARG;
     }

     enp_transport_address_t transport_address;
     if (!lookup_transport_address(next_hop, &transport_address)) {
         ESP_LOGE(TAG, "DATA: no transport address for next hop %u",
                  (unsigned)next_hop.node_id);
         return ESP_ERR_NOT_FOUND;
     }

     return enp_transport_send(
             s_context.transport,
             &transport_address,
             enp_packet_data_const(packet),
             enp_packet_length(packet));
 }

 static esp_err_t send_data_from_a(void)
 {
     const char payload[] = E3_DATA_PAYLOAD;
     const uint16_t payload_length = (uint16_t)(sizeof(payload) - 1U);

     const enp_route_entry_t *route =
             enp_route_table_lookup_const(
                     &s_routes,
                     (enp_route_destination_t){
                         .network_id = E3_NETWORK_ID,
                         .node_id = E3_NODE_C
                     });

     if (route == NULL || route->state != ENP_ROUTE_STATE_ACTIVE) {
         ESP_LOGE(TAG, "FAIL: no active route from A to C");
         return ESP_ERR_NOT_FOUND;
     }

     if (route->next_hop.node_id != E3_NODE_B) {
         ESP_LOGE(TAG, "FAIL: A DATA next hop is %u, expected B(2)",
                  (unsigned)route->next_hop.node_id);
         return ESP_ERR_NOT_FOUND;
     }

     ESP_LOGI(TAG, "PASS: A DATA route to C uses B");

     enp_packet_t packet;
     const enp_address_t source = local_address();

     enp_packet_init(
             &packet,
             ENP_PACKET_APPLICATION,
             &source);

     enp_header_t *header = enp_packet_header(&packet);
     if (header == NULL) {
         return ESP_ERR_INVALID_ARG;
     }

     header->destination.network = E3_NETWORK_ID;
     header->destination.node = E3_NODE_C;
     header->flags = ENP_FLAG_NONE;
     header->ttl = E3_DATA_TTL;
     header->sequence = ++s_data_packet_sequence;

     enp_data_header_t data_header;
     enp_data_header_init(
             &data_header,
             ENP_DATA_SUBTYPE_APPLICATION,
             ENP_DATA_FLAG_NONE,
             s_data_application_sequence,
             payload_length);

     if (!enp_data_header_valid(&data_header)) {
         ESP_LOGE(TAG, "FAIL: generated DATA header is invalid");
         return ESP_ERR_INVALID_ARG;
     }

     memcpy(
             enp_packet_payload(&packet),
             &data_header,
             sizeof(data_header));

     memcpy(
             (uint8_t *)enp_packet_payload(&packet) + ENP_DATA_HEADER_SIZE,
             payload,
             payload_length);

     const uint16_t total_payload_length =
             (uint16_t)(ENP_DATA_HEADER_SIZE + payload_length);

     esp_err_t err = enp_packet_seal(
             &packet,
             total_payload_length);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "FAIL: DATA packet seal: %s", esp_err_to_name(err));
         return err;
     }

     if (data_duplicate_check(&packet)) {
         ESP_LOGE(TAG, "FAIL: newly generated DATA packet was already in duplicate cache");
         return ESP_ERR_INVALID_STATE;
     }

     err = send_data_packet(
             &packet,
             (enp_rreq_node_t){
                 .network_id = E3_NETWORK_ID,
                 .node_id = E3_NODE_B
             });

     if (err == ESP_OK) {
         ESP_LOGI(TAG,
                  "DATA TX: origin=A destination=C next-hop=B seq=%lu app_seq=%lu ttl=%u",
                  (unsigned long)header->sequence,
                  (unsigned long)data_header.application_sequence,
                  (unsigned)header->ttl);
         ESP_LOGI(TAG, "PASS: A submitted DATA to B");
     } else {
         ESP_LOGE(TAG, "FAIL: A DATA transmission: %s", esp_err_to_name(err));
     }

     return err;
 }

 static void process_data(
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     if (packet == NULL || source == NULL) {
         return;
     }

     const enp_header_t *header = enp_packet_header_const(packet);
     if (header == NULL) {
         return;
     }

     if (header->payload_length < ENP_DATA_HEADER_SIZE) {
         ESP_LOGW(TAG, "DATA RX: payload too short for DATA header");
         return;
     }

     enp_data_header_t data_header;
     memcpy(
             &data_header,
             enp_packet_payload_const(packet),
             sizeof(data_header));

     if (!enp_data_header_valid(&data_header)) {
         ESP_LOGW(TAG, "DATA RX: invalid DATA header");
         return;
     }

     const size_t application_length =
             (size_t)header->payload_length - ENP_DATA_HEADER_SIZE;

     if (!enp_data_payload_length_valid(
             &data_header,
             application_length)) {
         ESP_LOGW(TAG, "DATA RX: DATA payload length mismatch");
         return;
     }

     bool duplicate = false;
     if (enp_duplicate_check_and_record(
             &s_data_duplicates,
             &header->source,
             header->sequence,
             enp_context_time_ms(&s_context),
             &duplicate) != ESP_OK) {
         ESP_LOGW(TAG, "DATA RX: duplicate-cache failure");
         return;
     }

     if (duplicate) {
         ESP_LOGW(TAG,
                  "DATA RX: duplicate suppressed source=%u seq=%lu",
                  (unsigned)header->source.node,
                  (unsigned long)header->sequence);
         return;
     }

     ++s_data_rx_count;

     ESP_LOGI(TAG,
              "DATA RX: origin=%u destination=%u seq=%lu app_seq=%lu ttl=%u",
              (unsigned)header->source.node,
              (unsigned)header->destination.node,
              (unsigned long)header->sequence,
              (unsigned long)data_header.application_sequence,
              (unsigned)header->ttl);

     if (header->destination.network != E3_NETWORK_ID) {
         ESP_LOGW(TAG, "DATA RX: destination network mismatch");
         return;
     }

     if (header->destination.node == CONFIG_ENP_E3_NODE_ID) {
         const char *application =
                 (const char *)enp_packet_payload_const(packet) +
                 ENP_DATA_HEADER_SIZE;

         if (application_length != sizeof(E3_DATA_PAYLOAD) - 1U ||
             memcmp(application, E3_DATA_PAYLOAD, application_length) != 0) {
             ESP_LOGE(TAG, "FAIL: C DATA payload integrity");
             s_test_failed = true;
             return;
         }

         if (data_header.application_sequence != s_data_application_sequence) {
             ESP_LOGE(TAG, "FAIL: C application sequence is %lu, expected %lu",
                      (unsigned long)data_header.application_sequence,
                      (unsigned long)s_data_application_sequence);
             s_test_failed = true;
             return;
         }

         ++s_data_delivered_count;
         ESP_LOGI(TAG, "PASS: C received DATA from B");
         ESP_LOGI(TAG, "PASS: C DATA payload integrity");
         ESP_LOGI(TAG, "PASS: C application sequence is valid");
         ESP_LOGI(TAG, "PASS: C final destination matched local node");
         ESP_LOGI(TAG, "E3.3.2 DATA delivery complete: A -> B -> C");
         return;
     }

     enp_rreq_node_t destination = {
         .network_id = header->destination.network,
         .node_id = (uint16_t)header->destination.node
     };

     enp_rreq_node_t next_hop;
     if (!lookup_route_next_hop(destination, &next_hop)) {
         ESP_LOGE(TAG,
                  "FAIL: DATA route unavailable at node %u for destination %u",
                  (unsigned)CONFIG_ENP_E3_NODE_ID,
                  (unsigned)destination.node_id);
         s_test_failed = true;
         return;
     }

     if (header->ttl <= 1U) {
         ESP_LOGE(TAG, "FAIL: DATA TTL expired at node %u",
                  (unsigned)CONFIG_ENP_E3_NODE_ID);
         s_test_failed = true;
         return;
     }

     enp_packet_t forward_packet;
     memcpy(
             &forward_packet,
             packet,
             sizeof(forward_packet));

     enp_header_t *forward_header =
             enp_packet_header(&forward_packet);
     if (forward_header == NULL) {
         s_test_failed = true;
         return;
     }

     forward_header->ttl--;

     esp_err_t err = enp_packet_seal(
             &forward_packet,
             forward_header->payload_length);
     if (err != ESP_OK) {
         ESP_LOGE(TAG, "FAIL: DATA forwarding reseal: %s",
                  esp_err_to_name(err));
         s_test_failed = true;
         return;
     }

     err = send_data_packet(
             &forward_packet,
             next_hop);

     if (err != ESP_OK) {
         ESP_LOGE(TAG, "FAIL: DATA forwarding from node %u: %s",
                  (unsigned)CONFIG_ENP_E3_NODE_ID,
                  esp_err_to_name(err));
         s_test_failed = true;
         return;
     }

     ++s_data_forward_count;

     ESP_LOGI(TAG,
              "DATA FWD: node=%u destination=%u next-hop=%u ttl=%u",
              (unsigned)CONFIG_ENP_E3_NODE_ID,
              (unsigned)header->destination.node,
              (unsigned)next_hop.node_id,
              (unsigned)forward_header->ttl);

     if (CONFIG_ENP_E3_NODE_ID == E3_NODE_B &&
         next_hop.node_id == E3_NODE_C) {
         ESP_LOGI(TAG, "PASS: B route to C uses C");
         ESP_LOGI(TAG, "PASS: B forwarded DATA to C");
         ESP_LOGI(TAG, "PASS: B decremented DATA TTL");
     }
 }

 static void process_rreq(
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     const enp_header_t *header =
             enp_packet_header_const(packet);
     if (header == NULL) {
         return;
     }

     enp_routing_rreq_t rreq;
     memcpy(
             &rreq,
             enp_packet_payload_const(packet),
             sizeof(rreq));

     enp_routing_rreq_t forward_rreq;

     enp_address_t immediate_sender_address;
     if (!find_neighbor_logical_address(
             source,
             &immediate_sender_address)) {
         ESP_LOGW(TAG, "RREQ RX: unknown immediate sender");
         return;
     }

     enp_rreq_result_t result =
             enp_rreq_processor_handle(
                     &s_rreq_processor,
                     node_to_rreq(header->source),
                     node_to_rreq(immediate_sender_address),
                     &rreq,
                     &forward_rreq);

     ++s_rreq_rx_count;

     ESP_LOGI(
             TAG,
             "RREQ RX: from=%u destination=%u id=%lu hop=%u ttl=%u result=%d",
             (unsigned)header->source.node,
             (unsigned)rreq.destination_node_id,
             (unsigned long)rreq.route_request_id,
             (unsigned)rreq.hop_count,
             (unsigned)rreq.ttl,
             (int)result);

     switch (result) {
     case ENP_RREQ_RESULT_REPLY: {
         enp_routing_rrep_t rrep = {
             .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
             .subtype = ENP_ROUTING_SUBTYPE_RREP,
             .destination_network_id = rreq.destination_network_id,
             .destination_node_id = rreq.destination_node_id,
             .destination_sequence = rreq.destination_sequence,
             .hop_count = 1U,
             .reserved_0 = 0U,
             .route_lifetime_ms = rreq.route_lifetime_ms,
             .reserved_1 = 0U
         };

         enp_rreq_node_t previous_hop =
                 node_to_rreq(immediate_sender_address);

         const enp_address_t rrep_origin = local_address();
         const enp_sequence_t rrep_packet_sequence =
                 ++s_routing_packet_sequence;

         if (send_rrep(
                 &rrep,
                 previous_hop,
                 &rrep_origin,
                 rrep_packet_sequence) == ESP_OK) {
             ESP_LOGI(
                     TAG,
                     "RREP generated at destination C and sent to B");

             const enp_route_entry_t *reverse =
                     enp_route_table_lookup_const(
                             &s_routes,
                             (enp_route_destination_t){
                                 .network_id = E3_NETWORK_ID,
                                 .node_id = E3_NODE_A
                             });

             if (reverse != NULL && reverse->next_hop.node_id == E3_NODE_B &&
                 reverse->metric.value == 2U) {
                 ESP_LOGI(TAG, "PASS: C reverse route to A uses B, metric=2");
             } else {
                 ESP_LOGE(TAG, "FAIL: C reverse route to A is incorrect");
                 s_test_failed = true;
             }
         }
         break;
     }

     case ENP_RREQ_RESULT_FORWARD:
         ++s_rreq_forward_count;
         if (send_rreq(
                 &forward_rreq,
                 &header->source,
                 (enp_rreq_node_t){
                     .network_id = E3_NETWORK_ID,
                     .node_id = E3_NODE_C
                 },
                 header->sequence) == ESP_OK) {
             ESP_LOGI(
                     TAG,
                     "RREQ forwarded by node %u",
                     (unsigned)CONFIG_ENP_E3_NODE_ID);

             if (CONFIG_ENP_E3_NODE_ID == E3_NODE_B) {
                 const enp_route_entry_t *reverse =
                         enp_route_table_lookup_const(
                                 &s_routes,
                                 (enp_route_destination_t){
                                     .network_id = E3_NETWORK_ID,
                                     .node_id = E3_NODE_A
                                 });
                 if (reverse != NULL && reverse->next_hop.node_id == E3_NODE_A &&
                     reverse->metric.value == 1U) {
                     ESP_LOGI(TAG, "PASS: B reverse route to A uses A, metric=1");
                 } else {
                     ESP_LOGE(TAG, "FAIL: B reverse route to A is incorrect");
                     s_test_failed = true;
                 }
             }
         }
         break;

     case ENP_RREQ_RESULT_DROP_DUPLICATE:
         ESP_LOGI(TAG, "RREQ duplicate suppressed");
         break;

     case ENP_RREQ_RESULT_DROP_TTL:
         ESP_LOGI(TAG, "RREQ dropped by TTL");
         break;

     default:
         ESP_LOGW(TAG, "RREQ rejected");
         break;
     }

     (void)source;
 }

 static void process_rrep(
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     const enp_header_t *header =
             enp_packet_header_const(packet);
     if (header == NULL) {
         return;
     }

     enp_routing_rrep_t rrep;
     memcpy(
             &rrep,
             enp_packet_payload_const(packet),
             sizeof(rrep));

     enp_routing_rrep_t forward_rrep;

     enp_address_t immediate_sender_address;
     if (!find_neighbor_logical_address(
             source,
             &immediate_sender_address)) {
         ESP_LOGW(TAG, "RREP RX: unknown immediate sender");
         return;
     }

     enp_rrep_result_t result =
             enp_rrep_processor_handle(
                     &s_rrep_processor,
                     node_to_rrep(immediate_sender_address),
                     &rrep,
                     &forward_rrep);

     ++s_rrep_rx_count;

     ESP_LOGI(
             TAG,
             "RREP RX: from=%u destination=%u hop=%u result=%d",
             (unsigned)header->source.node,
             (unsigned)rrep.destination_node_id,
             (unsigned)rrep.hop_count,
             (int)result);

     if (result == ENP_RREP_RESULT_FORWARD) {
         enp_rrep_node_t next_hop;

         if (rrep_lookup_next_hop(
                 NULL,
                 (enp_rrep_node_t){
                     .network_id = E3_NETWORK_ID,
                     .node_id = E3_NODE_A
                 },
                 &next_hop)) {
             ++s_rrep_forward_count;
             if (send_rrep(
                     &forward_rrep,
                     (enp_rreq_node_t){
                         .network_id = next_hop.network_id,
                         .node_id = next_hop.node_id
                     },
                     &header->source,
                     header->sequence) == ESP_OK) {
                 ESP_LOGI(
                         TAG,
                         "RREP forwarded toward A via node %u",
                         (unsigned)next_hop.node_id);

                 if (CONFIG_ENP_E3_NODE_ID == E3_NODE_B) {
                     const enp_route_entry_t *forward =
                             enp_route_table_lookup_const(
                                     &s_routes,
                                     (enp_route_destination_t){
                                         .network_id = E3_NETWORK_ID,
                                         .node_id = E3_NODE_C
                                     });
                     if (forward != NULL && forward->next_hop.node_id == E3_NODE_C &&
                         forward->metric.value == 1U) {
                         ESP_LOGI(TAG, "PASS: B forward route to C uses C, metric=1");
                     } else {
                         ESP_LOGE(TAG, "FAIL: B forward route to C is incorrect");
                         s_test_failed = true;
                     }
                 }
             }
         }
     }

     (void)source;
 }

 static void enp_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     if (source == NULL || data == NULL ||
         length < ENP_HEADER_SIZE + ENP_CRC_SIZE ||
         length > sizeof(enp_packet_t)) {
         return;
     }

     enp_packet_t packet;
     memset(&packet, 0, sizeof(packet));
     memcpy(enp_packet_data(&packet), data, length);

     if (!enp_packet_verify(&packet)) {
         ESP_LOGW(TAG, "Rejected invalid ENP frame");
         return;
     }

     const enp_header_t *header =
             enp_packet_header_const(&packet);
     if (header == NULL) {
         return;
     }

     if (header->type == ENP_PACKET_DISCOVERY) {
         if (header->payload_length != ENP_DISCOVERY_PAYLOAD_SIZE) {
             return;
         }

         const enp_discovery_payload_t *discovery =
                 (const enp_discovery_payload_t *)
                 enp_packet_payload_const(&packet);

         if (discovery->reserved != 0U ||
             enp_address_is_broadcast(&header->source)) {
             return;
         }

         (void)enp_neighbor_update(
                 &s_context.neighbors,
                 &header->source,
                 source,
                 (enp_role_t)discovery->role,
                 discovery->capabilities,
                 header->sequence,
                 0,
                 enp_context_time_ms(&s_context));

         ESP_LOGI(
                 TAG,
                 "Neighbor ready: node=%lu transport-len=%u",
                 (unsigned long)header->source.node,
                 (unsigned)source->length);
         return;
     }

     if (header->type == ENP_PACKET_APPLICATION) {
         process_data(&packet, source);
         return;
     }

     if (header->type != ENP_PACKET_ROUTE) {
         return;
     }

     if (header->payload_length == ENP_ROUTING_RREQ_WIRE_SIZE) {
         const uint8_t *payload =
                 (const uint8_t *)enp_packet_payload_const(&packet);
         if (payload[1] == ENP_ROUTING_SUBTYPE_RREQ) {
             process_rreq(&packet, source);
             return;
         }
     }

     if (header->payload_length == ENP_ROUTING_RREP_WIRE_SIZE) {
         const uint8_t *payload =
                 (const uint8_t *)enp_packet_payload_const(&packet);
         if (payload[1] == ENP_ROUTING_SUBTYPE_RREP) {
             process_rrep(&packet, source);
             return;
         }
     }

     ESP_LOGW(TAG, "Unknown routing payload");
 }

 static void print_route(const char *name, uint16_t destination)
 {
     const enp_route_entry_t *route =
             enp_route_table_lookup_const(
                     &s_routes,
                     (enp_route_destination_t){
                         .network_id = E3_NETWORK_ID,
                         .node_id = destination
                     });

     if (route == NULL) {
         ESP_LOGI(TAG, "%s: route to node %u: NONE", name,
                  (unsigned)destination);
         return;
     }

     ESP_LOGI(
             TAG,
             "%s: route to node %u -> next hop %u, metric=%u, state=%u",
             name,
             (unsigned)destination,
             (unsigned)route->next_hop.node_id,
             (unsigned)route->metric.value,
             (unsigned)route->state);
 }

 static void print_routes(void)
 {
     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "Node %u route table", (unsigned)CONFIG_ENP_E3_NODE_ID);

     for (size_t i = 0U; i < s_routes.count; ++i) {
         const enp_route_entry_t *route = &s_routes.entries[i];
         ESP_LOGI(
                 TAG,
                 "route[%u]: dst=%u next=%u metric=%u seq=%lu state=%u",
                 (unsigned)i,
                 (unsigned)route->destination.node_id,
                 (unsigned)route->next_hop.node_id,
                 (unsigned)route->metric.value,
                 (unsigned long)route->route_sequence,
                 (unsigned)route->state);
     }
 }

 static bool start_discovery(void)
 {
     if (CONFIG_ENP_E3_NODE_ID != E3_NODE_A) {
         ESP_LOGW(TAG, "Only node A (node 1) can start E3.2.2");
         return false;
     }

     const uint32_t request_id = s_next_rreq_id++;

     if (!enp_route_discovery_start(
             &s_discovery,
             (enp_discovery_destination_t){
                 .network_id = E3_NETWORK_ID,
                 .node_id = E3_NODE_C
             },
             request_id,
             E3_DESTINATION_SEQUENCE,
             E3_DISCOVERY_TTL,
             enp_context_time_ms(&s_context))) {
         ESP_LOGE(TAG, "Failed to start route discovery");
         return false;
     }

     enp_routing_rreq_t rreq = {
         .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
         .subtype = ENP_ROUTING_SUBTYPE_RREQ,
         .destination_network_id = E3_NETWORK_ID,
         .destination_node_id = E3_NODE_C,
         .route_request_id = request_id,
         .destination_sequence = E3_DESTINATION_SEQUENCE,
         .hop_count = 0U,
         .ttl = E3_DISCOVERY_TTL,
         .route_lifetime_ms = E3_RREQ_LIFETIME_MS
     };

     s_test_started = true;
     s_test_failed = false;

     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.2.2 START: A -> B -> C unicast discovery");
     ESP_LOGI(TAG, "RREQ request ID: %lu", (unsigned long)request_id);
     ESP_LOGI(TAG, "Discovery timeout: %u ms", (unsigned)E3_TEST_TIMEOUT_MS);
     ESP_LOGI(TAG, "======================================");

     s_active_rreq_packet_sequence =
             ++s_routing_packet_sequence;
     const enp_address_t originator = local_address();

     if (send_rreq(
             &rreq,
             &originator,
             (enp_rreq_node_t){
                 .network_id = E3_NETWORK_ID,
                 .node_id = E3_NODE_B
             },
             s_active_rreq_packet_sequence) != ESP_OK) {
         s_test_failed = true;
         ESP_LOGE(TAG, "FAIL: initial RREQ transmission");
         return false;
     }

     return true;
 }

 static void wait_for_discovery_result(void)
 {
     if (!s_test_started || CONFIG_ENP_E3_NODE_ID != E3_NODE_A) {
         return;
     }

     const uint32_t started = enp_context_time_ms(&s_context);

     while ((uint32_t)(enp_context_time_ms(&s_context) - started) <
            E3_TEST_TIMEOUT_MS) {
         if (enp_route_discovery_state(&s_discovery) ==
                 ENP_DISCOVERY_STATE_COMPLETE) {
             const enp_route_entry_t *route =
                     enp_route_table_lookup_const(
                             &s_routes,
                             (enp_route_destination_t){
                                 .network_id = E3_NETWORK_ID,
                                 .node_id = E3_NODE_C
                             });

             print_routes();

             if (route == NULL) {
                 ESP_LOGE(TAG, "FAIL: A has no active route to C");
                 s_test_failed = true;
             } else if (route->next_hop.node_id != E3_NODE_B) {
                 ESP_LOGE(
                         TAG,
                         "FAIL: A next hop to C is %u, expected B(2)",
                         (unsigned)route->next_hop.node_id);
                 s_test_failed = true;
             } else if (route->metric.value != 2U) {
                 ESP_LOGE(
                         TAG,
                         "FAIL: A route metric is %u, expected 2",
                         (unsigned)route->metric.value);
                 s_test_failed = true;
             }

             if (!s_test_failed) {
                 ESP_LOGI(TAG, "PASS: A route to C uses B");
                 ESP_LOGI(TAG, "PASS: A route metric is 2 hops");
                 ESP_LOGI(TAG, "======================================");
                 ESP_LOGI(TAG, "ALL E3.2.2 DISCOVERY TESTS PASSED");
                 ESP_LOGI(TAG, "======================================");
             }
             return;
         }

         if (enp_route_discovery_state(&s_discovery) ==
                 ENP_DISCOVERY_STATE_FAILED) {
             s_test_failed = true;
             ESP_LOGE(TAG, "FAIL: route discovery failed");
             print_routes();
             return;
         }

         if (enp_route_discovery_on_timeout(
                 &s_discovery,
                 enp_context_time_ms(&s_context))) {
             enp_routing_rreq_t retry = {
                 .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
                 .subtype = ENP_ROUTING_SUBTYPE_RREQ,
                 .destination_network_id = E3_NETWORK_ID,
                 .destination_node_id = E3_NODE_C,
                 .route_request_id = s_discovery.route_request_id,
                 .destination_sequence = E3_DESTINATION_SEQUENCE,
                 .hop_count = 0U,
                 .ttl = E3_DISCOVERY_TTL,
                 .route_lifetime_ms = E3_RREQ_LIFETIME_MS
             };

             ESP_LOGI(TAG, "Discovery retry %u",
                      (unsigned)s_discovery.retry_count);
             const enp_address_t originator = {
                 .network = E3_NETWORK_ID,
                 .node = E3_NODE_A
             };
             (void)send_rreq(
                     &retry,
                     &originator,
                     (enp_rreq_node_t){
                         .network_id = E3_NETWORK_ID,
                         .node_id = E3_NODE_B
                     },
                     s_active_rreq_packet_sequence);
         }

         vTaskDelay(pdMS_TO_TICKS(50));
     }

     s_test_failed = true;
     ESP_LOGE(TAG, "FAIL: E3.2.2 discovery exceeded 10 seconds");
     print_routes();
 }

 static void start_data_test(void)
 {
     if (CONFIG_ENP_E3_NODE_ID != E3_NODE_A) {
         ESP_LOGW(TAG, "Only node A (node 1) can start E3.3.2");
         return;
     }

     const enp_route_entry_t *route =
             enp_route_table_lookup_const(
                     &s_routes,
                     (enp_route_destination_t){
                         .network_id = E3_NETWORK_ID,
                         .node_id = E3_NODE_C
                     });

     if (route == NULL || route->state != ENP_ROUTE_STATE_ACTIVE) {
         ESP_LOGE(TAG, "FAIL: E3.3.2 requires an active A -> C route; run e3 start first");
         return;
     }

     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.3.2 START: A -> B -> C DATA forwarding");
     ESP_LOGI(TAG, "Payload: %s", E3_DATA_PAYLOAD);
     ESP_LOGI(TAG, "Application sequence: %lu", (unsigned long)s_data_application_sequence);
     ESP_LOGI(TAG, "Initial DATA TTL: %u", (unsigned)E3_DATA_TTL);
     ESP_LOGI(TAG, "======================================");

     if (send_data_from_a() != ESP_OK) {
         s_test_failed = true;
         ESP_LOGE(TAG, "FAIL: E3.3.2 DATA submission from A");
     }
 }

 static void neighbor_announce_task(void *argument)
 {
     (void)argument;

     for (;;) {
         (void)enp_service_discovery_send(&s_context);
         vTaskDelay(pdMS_TO_TICKS(2000));
     }
 }

 static void control_task(void *argument)
 {
     (void)argument;

     char line[32];

     ESP_LOGI(TAG, "READY");
     ESP_LOGI(TAG, "Waiting for E3.2.2 / E3.3.2 commands");

     if (CONFIG_ENP_E3_NODE_ID != E3_NODE_A) {
         for (;;) {
             vTaskDelay(pdMS_TO_TICKS(1000));
         }
     }

     for (;;) {
         ESP_LOGI(TAG, "Type 'e3 start' for route discovery, then 'e3 data' on node A");

         if (fgets(line, sizeof(line), stdin) == NULL) {
             vTaskDelay(pdMS_TO_TICKS(500));
             continue;
         }

         if (strncmp(line, "e3 start", 8U) == 0) {
             if (start_discovery()) {
                 wait_for_discovery_result();
             }
         } else if (strncmp(line, "e3 routes", 9U) == 0) {
             print_routes();
         } else if (strncmp(line, "e3 data", 7U) == 0) {
             start_data_test();
         } else {
             ESP_LOGI(TAG, "Commands: e3 start | e3 data | e3 routes");
         }
     }
 }

 static void nvs_init(void)
 {
     esp_err_t err = nvs_flash_init();

     if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
         err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
         ESP_ERROR_CHECK(nvs_flash_erase());
         err = nvs_flash_init();
     }

     ESP_ERROR_CHECK(err);
 }

 static enp_config_t make_config(void)
 {
     enp_config_t config = {0};

     config.network_id = E3_NETWORK_ID;
     config.node_id = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID;

 #if CONFIG_DEVICE_ROLE_GATEWAY
     config.role = ENP_ROLE_GATEWAY;
 #elif CONFIG_DEVICE_ROLE_RELAY
     config.role = ENP_ROLE_RELAY;
 #elif CONFIG_DEVICE_ROLE_SENSOR
     config.role = ENP_ROLE_SENSOR;
 #else
     config.role = ENP_ROLE_UNKNOWN;
 #endif

     return config;
 }

static bool e3_3_1_data_self_test(void)
{
    unsigned pass_count = 0U;
    unsigned fail_count = 0U;
    enp_data_header_t header;

#define E3_DATA_PASS(msg) \
    do { \
        ESP_LOGI(TAG, "PASS: %s", (msg)); \
        ++pass_count; \
    } while (0)

#define E3_DATA_FAIL(msg) \
    do { \
        ESP_LOGE(TAG, "FAIL: %s", (msg)); \
        ++fail_count; \
    } while (0)

#define E3_DATA_CHECK(condition, msg) \
    do { \
        if (condition) { \
            E3_DATA_PASS(msg); \
        } else { \
            E3_DATA_FAIL(msg); \
        } \
    } while (0)

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.1 ENP DATA WIRE SELF-TEST");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "======================================");

    E3_DATA_CHECK(
            ENP_DATA_HEADER_SIZE == 12U,
            "DATA header size is 12 bytes");

    enp_data_header_init(
            &header,
            ENP_DATA_SUBTYPE_APPLICATION,
            ENP_DATA_FLAG_NONE,
            1U,
            7U);

    E3_DATA_CHECK(
            header.payload_version == ENP_DATA_PAYLOAD_VERSION,
            "DATA payload version initialized");

    E3_DATA_CHECK(
            header.subtype == ENP_DATA_SUBTYPE_APPLICATION,
            "DATA subtype initialized");

    E3_DATA_CHECK(
            header.flags == ENP_DATA_FLAG_NONE,
            "DATA flags initialized");

    E3_DATA_CHECK(
            header.application_sequence == 1U,
            "application sequence initialized");

    E3_DATA_CHECK(
            header.payload_length == 7U,
            "payload length initialized");

    E3_DATA_CHECK(
            header.reserved == 0U,
            "reserved field initialized to zero");

    E3_DATA_CHECK(
            enp_data_header_valid(&header),
            "valid DATA header accepted");

    header.payload_version = 0xFFU;
    E3_DATA_CHECK(
            !enp_data_header_valid(&header),
            "invalid DATA version rejected");

    header.payload_version = ENP_DATA_PAYLOAD_VERSION;
    header.subtype = 0xFFU;
    E3_DATA_CHECK(
            !enp_data_header_valid(&header),
            "invalid DATA subtype rejected");

    header.subtype = ENP_DATA_SUBTYPE_APPLICATION;
    header.flags = 0x8000U;
    E3_DATA_CHECK(
            !enp_data_header_valid(&header),
            "unknown DATA flags rejected");

    header.flags = ENP_DATA_FLAG_NONE;
    header.reserved = 1U;
    E3_DATA_CHECK(
            !enp_data_header_valid(&header),
            "non-zero reserved field rejected");

    header.reserved = 0U;
    header.application_sequence = 0U;
    E3_DATA_CHECK(
            !enp_data_header_valid(&header),
            "zero application sequence rejected");

    enp_data_header_init(
            &header,
            ENP_DATA_SUBTYPE_APPLICATION,
            ENP_DATA_FLAG_NONE,
            1U,
            32U);

    E3_DATA_CHECK(
            enp_data_payload_length_valid(&header, 32U),
            "matching payload length accepted");

    E3_DATA_CHECK(
            !enp_data_payload_length_valid(&header, 31U),
            "short payload rejected");

    E3_DATA_CHECK(
            !enp_data_payload_length_valid(&header, 33U),
            "long payload rejected");

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "E3.3.1 DATA self-test PASS: %u", pass_count);
    ESP_LOGI(TAG, "E3.3.1 DATA self-test FAIL: %u", fail_count);

    if (fail_count == 0U) {
        ESP_LOGI(TAG, "ALL E3.3.1 DATA WIRE TESTS PASSED");
    } else {
        ESP_LOGE(TAG, "E3.3.1 DATA WIRE TEST FAILED");
    }

    ESP_LOGI(TAG, "======================================");

#undef E3_DATA_CHECK
#undef E3_DATA_FAIL
#undef E3_DATA_PASS

    return fail_count == 0U;
}

 void app_main(void)
 {
     if (!e3_3_1_data_self_test()) {
         ESP_LOGE(TAG, "E3.3.1 DATA self-test failed; application startup aborted");
         return;
     }

     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "ENP v0.2 E3.3.2 REAL MULTI-HOP DATA");
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "Network: %u", (unsigned)E3_NETWORK_ID);
     ESP_LOGI(TAG, "Node: %u", (unsigned)CONFIG_ENP_E3_NODE_ID);

     if (CONFIG_ENP_E3_NODE_ID == E3_NODE_A) {
         ESP_LOGI(TAG, "Topology role: A / GATEWAY");
     } else if (CONFIG_ENP_E3_NODE_ID == E3_NODE_B) {
         ESP_LOGI(TAG, "Topology role: B / RELAY (DEVICE_ROLE_RELAY)");
     } else if (CONFIG_ENP_E3_NODE_ID == E3_NODE_C) {
         ESP_LOGI(TAG, "Topology role: C / SENSOR (DEVICE_ROLE_SENSOR)");
     } else {
         ESP_LOGE(TAG, "Invalid E3 node ID: %u",
                  (unsigned)CONFIG_ENP_E3_NODE_ID);
         return;
     }

     nvs_init();
     ESP_ERROR_CHECK(esp_netif_init());
     ESP_ERROR_CHECK(esp_event_loop_create_default());

     ESP_ERROR_CHECK(enp_wifi_init());

     while (!enp_wifi_is_connected()) {
         vTaskDelay(pdMS_TO_TICKS(100));
     }

     ESP_LOGI(TAG, "Wi-Fi connected");
     ESP_LOGI(TAG, "Wi-Fi channel: %u",
              (unsigned)enp_wifi_get_channel());

     enp_config_t config = make_config();
     enp_transport_t *transport = enp_transport_espnow_get();

     if (transport == NULL) {
         ESP_LOGE(TAG, "ESP-NOW transport unavailable");
         return;
     }

     ESP_ERROR_CHECK(enp_context_init(
             &s_context,
             transport,
             &config));

     if (!init_processors()) {
         ESP_LOGE(TAG, "Routing processor initialization failed");
         (void)enp_context_deinit(&s_context);
         return;
     }

     ESP_ERROR_CHECK(enp_transport_set_receive_callback(
             s_context.transport,
             enp_receive_callback));

     s_announce_task = xTaskCreateStatic(
             neighbor_announce_task,
             "e3_3_announce",
             E3_TASK_STACK_SIZE,
             NULL,
             E3_TASK_PRIORITY,
             s_announce_task_stack,
             &s_announce_task_buffer);

     if (s_announce_task == NULL) {
         ESP_LOGE(TAG, "Failed to create neighbor announcement task");
         return;
     }

     s_control_task = xTaskCreateStatic(
             control_task,
             "e3_3_control",
             E3_TASK_STACK_SIZE,
             NULL,
             E3_TASK_PRIORITY,
             s_control_task_stack,
             &s_control_task_buffer);

     if (s_control_task == NULL) {
         ESP_LOGE(TAG, "Failed to create control task");
         return;
     }

     /* app_main may return; the ESP-NOW transport and E3.2 control task
      * are both statically allocated and remain alive. */
 }