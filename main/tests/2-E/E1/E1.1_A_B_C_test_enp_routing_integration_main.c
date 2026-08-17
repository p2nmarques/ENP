 /*
 * E1_A_B_C_test_enp_routing_integration_main.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E1.1 A-B-C basic routing discovery integration test.
 *
 * Hardware-independent integration of:
 *   R4-A.1 Route Discovery
 *   R4-B   RREQ Processor
 *   R4-C   RREP Processor
 *   R3-B   Route Table
 *   R3-A   Hop-count Metric
 *
 * No ESP-NOW, Wi-Fi, transport or FreeRTOS dependency.
 */

 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_log.h"

 #include "core/routing/enp_route_discovery.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_rrep_processor.h"
 #include "core/routing/enp_rreq_processor.h"

 static const char *TAG = "E1ABC";
 static int s_failures;

 #define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)
 #define FAIL(name) do { ESP_LOGE(TAG, "FAIL: %s", name); ++s_failures; } while (0)
 #define EXPECT_TRUE(condition, name) \
     do { if (condition) PASS(name); else FAIL(name); } while (0)

 typedef struct {
     enp_route_table_t table;
     uint32_t duplicate_checks;
     uint32_t reverse_updates;
     uint32_t route_updates;
     uint32_t next_hop_lookups;
     uint32_t discovery_completions;
     enp_rreq_node_t last_originator;
     enp_rreq_node_t last_next_hop;
 } node_context_t;

 static enp_rreq_node_t node(uint16_t id)
 {
     return (enp_rreq_node_t){ .network_id = 1U, .node_id = id };
 }

 static enp_route_destination_t destination_from_rreq(enp_rreq_node_t n)
 {
     return (enp_route_destination_t){
         .network_id = n.network_id,
         .node_id = n.node_id
     };
 }

 static enp_rrep_node_t rrep_node(uint16_t id)
 {
     return (enp_rrep_node_t){ .network_id = 1U, .node_id = id };
 }

 static bool metric_from_hops(uint32_t hops, enp_route_metric_t *metric)
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

 static bool install_route(
     enp_route_table_t *table,
     enp_route_destination_t destination,
     enp_route_destination_t next_hop,
     uint32_t sequence,
     uint8_t hop_count,
     uint32_t lifetime_ms)
 {
     enp_route_entry_t entry = {0};

     entry.destination = destination;
     entry.next_hop = next_hop;
     entry.route_sequence = sequence;
     entry.expires_at_ms = lifetime_ms;
     entry.state = ENP_ROUTE_STATE_ACTIVE;

     if (!metric_from_hops(hop_count, &entry.metric)) {
         return false;
     }

     enp_route_entry_t *existing =
         enp_route_table_lookup(table, destination);

     if (existing != NULL) {
         return enp_route_table_update(table, &entry);
     }

     return enp_route_table_insert(table, &entry);
 }

 /* --------------------------------------------------------------------------
  * RREQ callbacks
  * -------------------------------------------------------------------------- */

 static bool mock_is_duplicate(
     void *context,
     enp_rreq_node_t originator,
     enp_route_request_id_t request_id)
 {
     node_context_t *ctx = context;
     (void)originator;
     (void)request_id;

     ++ctx->duplicate_checks;

     /*
      * E1.1 uses a single request, so the first delivery is always new.
      * The explicit duplicate test below uses a small test-local flag by
      * installing a second callback context when needed.
      */
     return false;
 }

 static bool mock_learn_reverse_route(
     void *context,
     enp_rreq_node_t originator,
     enp_rreq_node_t next_hop,
     uint8_t hop_count,
     enp_route_sequence_t destination_sequence,
     uint32_t lifetime_ms)
 {
     node_context_t *ctx = context;

     ++ctx->reverse_updates;
     ctx->last_originator = originator;
     ctx->last_next_hop = next_hop;

     return install_route(
         &ctx->table,
         destination_from_rreq(originator),
         destination_from_rreq(next_hop),
         destination_sequence,
         hop_count,
         lifetime_ms);
 }

 /* --------------------------------------------------------------------------
  * RREP callbacks
  * -------------------------------------------------------------------------- */

 static bool mock_update_route(
     void *context,
     enp_rrep_node_t destination,
     enp_rrep_node_t next_hop,
     enp_route_sequence_t destination_sequence,
     uint8_t hop_count,
     uint32_t lifetime_ms,
     uint32_t metric)
 {
     node_context_t *ctx = context;

     ++ctx->route_updates;

     return install_route(
         &ctx->table,
         (enp_route_destination_t){
             .network_id = destination.network_id,
             .node_id = destination.node_id
         },
         (enp_route_destination_t){
             .network_id = next_hop.network_id,
             .node_id = next_hop.node_id
         },
         destination_sequence,
         (uint8_t)metric,
         lifetime_ms);
 }

 static bool mock_lookup_next_hop(
     void *context,
     enp_rrep_node_t destination,
     enp_rrep_node_t *next_hop)
 {
     node_context_t *ctx = context;

     ++ctx->next_hop_lookups;

     if (next_hop == NULL) {
         return false;
     }

     const enp_route_entry_t *entry =
         enp_route_table_lookup_const(
             &ctx->table,
             (enp_route_destination_t){
                 .network_id = destination.network_id,
                 .node_id = destination.node_id
             });

     if (entry == NULL) {
         return false;
     }

     *next_hop = rrep_node(entry->next_hop.node_id);
     return true;
 }

 static bool mock_discovery_complete(
     void *context,
     enp_rrep_node_t destination,
     enp_route_sequence_t destination_sequence)
 {
     node_context_t *ctx = context;
     (void)destination;
     (void)destination_sequence;

     ++ctx->discovery_completions;
     return true;
 }

 /* --------------------------------------------------------------------------
  * Helpers
  * -------------------------------------------------------------------------- */

 static bool init_node(node_context_t *ctx)
 {
     memset(ctx, 0, sizeof(*ctx));
     return enp_route_table_init(&ctx->table);
 }

 static enp_routing_rreq_t make_rreq(
     uint16_t destination_node,
     uint32_t request_id,
     uint32_t destination_sequence,
     uint8_t hop_count,
     uint8_t ttl)
 {
     return (enp_routing_rreq_t){
         .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
         .subtype = ENP_ROUTING_SUBTYPE_RREQ,
         .destination_network_id = 1U,
         .destination_node_id = destination_node,
         .route_request_id = request_id,
         .destination_sequence = destination_sequence,
         .hop_count = hop_count,
         .ttl = ttl,
         .route_lifetime_ms = 5000U
     };
 }

 static enp_routing_rrep_t make_rrep(
     uint16_t destination_node,
     uint32_t destination_sequence,
     uint8_t hop_count)
 {
     return (enp_routing_rrep_t){
         .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
         .subtype = ENP_ROUTING_SUBTYPE_RREP,
         .destination_network_id = 1U,
         .destination_node_id = destination_node,
         .destination_sequence = destination_sequence,
         .hop_count = hop_count,
         .reserved_0 = 0U,
         .route_lifetime_ms = 5000U,
         .reserved_1 = 0U
     };
 }

 static const enp_route_entry_t *lookup_route(
     node_context_t *ctx,
     uint16_t destination_node)
 {
     return enp_route_table_lookup_const(
         &ctx->table,
         (enp_route_destination_t){1U, destination_node});
 }

 /* --------------------------------------------------------------------------
  * E1.1
  * -------------------------------------------------------------------------- */

 /*
  * ENP_MAX_ROUTES is 64 and each node_context_t contains a complete
  * route table. Three node_context_t objects are therefore too large
  * for the default ESP-IDF main task stack (3584 bytes).
  *
  * Keep the integration-test node state in static storage. This is also
  * closer to the intended embedded deployment model.
  */
 static node_context_t s_node_a;
 static node_context_t s_node_b;
 static node_context_t s_node_c;

 static void test_basic_a_b_c_discovery(void)
 {
     node_context_t *a = &s_node_a;
     node_context_t *b = &s_node_b;
     node_context_t *c = &s_node_c;

     EXPECT_TRUE(init_node(a), "node A route table initialized");
     EXPECT_TRUE(init_node(b), "node B route table initialized");
     EXPECT_TRUE(init_node(c), "node C route table initialized");

     enp_route_discovery_t discovery_a;
     EXPECT_TRUE(
         enp_route_discovery_init(&discovery_a),
         "A discovery initialized");

     /*
      * A requests a route to C.
      */
     EXPECT_TRUE(
         enp_route_discovery_start(
             &discovery_a,
             (enp_discovery_destination_t){1U, 3U},
             0xA001U,
             100U,
             8U,
             0U),
         "A starts discovery for C");

     /*
      * A -> B
      */
     enp_rreq_processor_t rreq_b;
     enp_rreq_processor_callbacks_t rreq_b_callbacks = {
         .context = b,
         .is_duplicate = mock_is_duplicate,
         .learn_reverse_route = mock_learn_reverse_route
     };

     EXPECT_TRUE(
         enp_rreq_processor_init(
             &rreq_b,
             node(2U),
             &rreq_b_callbacks),
         "B RREQ processor initialized");

     enp_routing_rreq_t rreq_a =
         make_rreq(3U, 0xA001U, 100U, 0U, 8U);
     enp_routing_rreq_t rreq_b_out;

     EXPECT_TRUE(
         enp_rreq_processor_handle(
             &rreq_b,
             node(1U),
             node(1U),
             &rreq_a,
             &rreq_b_out) == ENP_RREQ_RESULT_FORWARD,
         "B forwards A's RREQ");

     EXPECT_TRUE(
         rreq_b_out.hop_count == 1U,
         "B increments RREQ hop count");

     EXPECT_TRUE(
         rreq_b_out.ttl == 7U,
         "B decrements RREQ TTL");

     /*
      * B must now have a reverse route to A via A.
      */
     const enp_route_entry_t *b_to_a = lookup_route(b, 1U);

     EXPECT_TRUE(
         b_to_a != NULL,
         "B learned reverse route to A");

     EXPECT_TRUE(
         b_to_a != NULL && b_to_a->next_hop.node_id == 1U,
         "B reverse route to A uses A as next hop");

     EXPECT_TRUE(
         b->reverse_updates == 1U,
         "B learned one reverse route");

     /*
      * B -> C
      */
     enp_rreq_processor_t rreq_c;
     enp_rreq_processor_callbacks_t rreq_c_callbacks = {
         .context = c,
         .is_duplicate = mock_is_duplicate,
         .learn_reverse_route = mock_learn_reverse_route
     };

     EXPECT_TRUE(
         enp_rreq_processor_init(
             &rreq_c,
             node(3U),
             &rreq_c_callbacks),
         "C RREQ processor initialized");

     enp_routing_rreq_t rreq_c_out;

     EXPECT_TRUE(
         enp_rreq_processor_handle(
             &rreq_c,
             node(1U),
             node(2U),
             &rreq_b_out,
             &rreq_c_out) == ENP_RREQ_RESULT_REPLY,
         "C receives RREQ and requests RREP");

     /*
      * C must have a reverse route to A via B.
      */
     const enp_route_entry_t *c_to_a = lookup_route(c, 1U);

     EXPECT_TRUE(
         c_to_a != NULL,
         "C learned reverse route to A");

     EXPECT_TRUE(
         c_to_a != NULL && c_to_a->next_hop.node_id == 2U,
         "C reverse route to A uses B as next hop");

     EXPECT_TRUE(
         c->reverse_updates == 1U,
         "C learned one reverse route");

     /*
      * C generates RREP for destination C.
      *
      * The current ENP v0.2 RREP wire format has no route_request_id.
      * R4-A.1 correlates completion using destination + sequence.
      */
     enp_routing_rrep_t rrep_c =
         make_rrep(3U, 100U, 0U);

     /*
      * C -> B
      */
     enp_rrep_processor_t rrep_b;
     enp_rrep_processor_callbacks_t rrep_b_callbacks = {
         .context = b,
         .update_route = mock_update_route,
         .lookup_next_hop = mock_lookup_next_hop,
         .discovery_complete = mock_discovery_complete
     };

     EXPECT_TRUE(
         enp_rrep_processor_init(
             &rrep_b,
             rrep_node(2U),
             rrep_node(1U),
             &rrep_b_callbacks),
         "B RREP processor initialized");

     enp_routing_rrep_t rrep_b_out;

     EXPECT_TRUE(
         enp_rrep_processor_handle(
             &rrep_b,
             rrep_node(3U),
             &rrep_c,
             &rrep_b_out) == ENP_RREP_RESULT_FORWARD,
         "B forwards RREP toward A");

     EXPECT_TRUE(
         rrep_b_out.hop_count == 1U,
         "B increments RREP hop count");

     /*
      * B must now have a forward route to C via C.
      */
     const enp_route_entry_t *b_to_c = lookup_route(b, 3U);

     EXPECT_TRUE(
         b_to_c != NULL,
         "B learned forward route to C");

     EXPECT_TRUE(
         b_to_c != NULL && b_to_c->next_hop.node_id == 3U,
         "B forward route to C uses C as next hop");

     EXPECT_TRUE(
         b->next_hop_lookups == 1U,
         "B performs one lookup toward A");

     /*
      * B -> A
      */
     enp_rrep_processor_t rrep_a;
     enp_rrep_processor_callbacks_t rrep_a_callbacks = {
         .context = a,
         .update_route = mock_update_route,
         .lookup_next_hop = mock_lookup_next_hop,
         .discovery_complete = mock_discovery_complete
     };

     EXPECT_TRUE(
         enp_rrep_processor_init(
             &rrep_a,
             rrep_node(1U),
             rrep_node(1U),
             &rrep_a_callbacks),
         "A RREP processor initialized");

     enp_routing_rrep_t rrep_a_out;

     EXPECT_TRUE(
         enp_rrep_processor_handle(
             &rrep_a,
             rrep_node(2U),
             &rrep_b_out,
             &rrep_a_out) == ENP_RREP_RESULT_COMPLETE,
         "A receives RREP and completes discovery");

     EXPECT_TRUE(
         a->discovery_completions == 1U,
         "A discovery completion callback invoked once");

     EXPECT_TRUE(
         enp_route_discovery_on_rrep(
             &discovery_a,
             (enp_discovery_destination_t){1U, 3U},
             100U),
         "A discovery state accepts completed RREP");

     EXPECT_TRUE(
         enp_route_discovery_state(&discovery_a) ==
             ENP_DISCOVERY_STATE_COMPLETE,
         "A discovery enters COMPLETE");

     /*
      * A must now have the usable route A -> B -> C.
      */
     const enp_route_entry_t *a_to_c = lookup_route(a, 3U);

     EXPECT_TRUE(
         a_to_c != NULL,
         "A learned forward route to C");

     EXPECT_TRUE(
         a_to_c != NULL && a_to_c->next_hop.node_id == 2U,
         "A forward route to C uses B as next hop");

     /*
      * Final topology proof.
      */
     EXPECT_TRUE(
         enp_route_table_active_count(&a->table) == 1U,
         "A has one active route");

     EXPECT_TRUE(
         enp_route_table_active_count(&b->table) == 2U,
         "B has reverse and forward routes");

     EXPECT_TRUE(
         enp_route_table_active_count(&c->table) == 1U,
         "C has one reverse route");

     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "A-B-C route establishment:");
     ESP_LOGI(TAG, "  A -> C : next hop B");
     ESP_LOGI(TAG, "  B -> C : next hop C");
     ESP_LOGI(TAG, "  B -> A : next hop A");
     ESP_LOGI(TAG, "  C -> A : next hop B");
 }

 /*
  * E1.1 is intentionally limited to the first successful path.
  *
  * Duplicate suppression, TTL termination, route expiration and RERR are
  * separate E1/E2 acceptance items and should not be mixed into this first
  * end-to-end baseline.
  */
 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "ENP v0.2 E1.1 A-B-C basic discovery");
     ESP_LOGI(TAG, "======================================");

     test_basic_a_b_c_discovery();

     ESP_LOGI(TAG, "======================================");

     if (s_failures == 0) {
         ESP_LOGI(TAG, "ALL E1.1 A-B-C DISCOVERY TESTS PASSED");
     } else {
         ESP_LOGE(
             TAG,
             "%d E1.1 A-B-C DISCOVERY TEST(S) FAILED",
             s_failures);
     }

     ESP_LOGI(TAG, "======================================");
 }