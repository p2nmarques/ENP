/*
 * E1_2_duplicate_ttl_test_enp_routing_integration_main.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E1.2 duplicate RREQ suppression and TTL integration test.  *
 * Hardware-independent integration of:
 *   R4-B   RREQ Processor
 *   R3-B   Route Table
 *
 * No ESP-NOW, Wi-Fi, transport or FreeRTOS dependency.
 */

 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_log.h"

 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_rreq_processor.h"

 static const char *TAG = "E1DUPTTL";
 static int s_failures;

 #define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)
 #define FAIL(name) do { ESP_LOGE(TAG, "FAIL: %s", name); ++s_failures; } while (0)
 #define EXPECT_TRUE(condition, name) \
     do { if (condition) PASS(name); else FAIL(name); } while (0)

 typedef struct {
     enp_route_table_t table;

     bool duplicate;
     uint32_t duplicate_checks;
     uint32_t reverse_updates;
 } node_context_t;

 /*
  * Keep node state out of the main-task stack.  Each route table contains
  * ENP_MAX_ROUTES entries.
  */
 static node_context_t s_node_b;
 static node_context_t s_node_c;

 static enp_rreq_node_t node(uint16_t id)
 {
     return (enp_rreq_node_t){
         .network_id = 1U,
         .node_id = id
     };
 }

 static enp_route_destination_t route_destination(enp_rreq_node_t n)
 {
     return (enp_route_destination_t){
         .network_id = n.network_id,
         .node_id = n.node_id
     };
 }

 static bool metric_from_hops(
     uint32_t hops,
     enp_route_metric_t *metric)
 {
     if (metric == NULL || hops > UINT16_MAX) {
         return false;
     }

     if (!enp_route_metric_init(
             metric,
             ENP_ROUTE_METRIC_HOP_COUNT)) {
         return false;
     }

     metric->value = (uint16_t)hops;
     metric->valid = true;

     return true;
 }

 static bool install_reverse_route(
     node_context_t *ctx,
     enp_rreq_node_t originator,
     enp_rreq_node_t next_hop,
     uint8_t hop_count,
     enp_route_sequence_t sequence,
     uint32_t lifetime_ms)
 {
     enp_route_entry_t entry = {0};

     entry.destination = route_destination(originator);
     entry.next_hop = route_destination(next_hop);
     entry.route_sequence = sequence;
     entry.expires_at_ms = lifetime_ms;
     entry.state = ENP_ROUTE_STATE_ACTIVE;

     if (!metric_from_hops(hop_count, &entry.metric)) {
         return false;
     }

     return enp_route_table_insert(&ctx->table, &entry);
 }

 static bool mock_is_duplicate(
     void *context,
     enp_rreq_node_t originator,
     enp_route_request_id_t request_id)
 {
     node_context_t *ctx = context;

     (void)originator;
     (void)request_id;

     ++ctx->duplicate_checks;

     return ctx->duplicate;
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

     return install_reverse_route(
         ctx,
         originator,
         next_hop,
         hop_count,
         destination_sequence,
         lifetime_ms);
 }

 static void reset_node(node_context_t *ctx)
 {
     memset(ctx, 0, sizeof(*ctx));
     (void)enp_route_table_init(&ctx->table);
 }

 static bool init_processor(
     enp_rreq_processor_t *processor,
     node_context_t *ctx,
     uint16_t local_node_id)
 {
     enp_rreq_processor_callbacks_t callbacks = {
         .context = ctx,
         .is_duplicate = mock_is_duplicate,
         .learn_reverse_route = mock_learn_reverse_route
     };

     return enp_rreq_processor_init(
         processor,
         node(local_node_id),
         &callbacks);
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

 static void test_duplicate_rreq(void)
 {
     enp_rreq_processor_t processor;
     enp_routing_rreq_t input;
     enp_routing_rreq_t output;

     reset_node(&s_node_b);

     EXPECT_TRUE(
         init_processor(&processor, &s_node_b, 2U),
         "B RREQ processor initialized");

     input = make_rreq(
         3U,       /* destination C */
         0xD001U,  /* request ID */
         100U,     /* destination sequence */
         0U,
         8U);

     s_node_b.duplicate = false;

     enp_rreq_result_t first_result =
         enp_rreq_processor_handle(
             &processor,
             node(1U),       /* originator A */
             node(1U),       /* immediate sender A */
             &input,
             &output);

     EXPECT_TRUE(
         first_result == ENP_RREQ_RESULT_FORWARD,
         "first RREQ is accepted and forwarded");

     EXPECT_TRUE(
         s_node_b.duplicate_checks == 1U,
         "first RREQ performs one duplicate check");

     EXPECT_TRUE(
         s_node_b.reverse_updates == 1U,
         "first RREQ learns reverse route");

     EXPECT_TRUE(
         output.hop_count == 1U,
         "first forwarded RREQ increments hop count");

     EXPECT_TRUE(
         output.ttl == 7U,
         "first forwarded RREQ decrements TTL");

     const enp_route_entry_t *reverse_route =
         enp_route_table_lookup_const(
             &s_node_b.table,
             route_destination(node(1U)));

     EXPECT_TRUE(
         reverse_route != NULL,
         "B has reverse route to A after first RREQ");

     if (reverse_route != NULL) {
         EXPECT_TRUE(
             reverse_route->next_hop.node_id == 1U,
             "B reverse route to A uses A as next hop");
     }

     /*
      * The same originator/request-id is now marked duplicate.
      * The processor must drop it before reverse-route learning.
      */
     s_node_b.duplicate = true;

     enp_rreq_result_t duplicate_result =
         enp_rreq_processor_handle(
             &processor,
             node(1U),
             node(1U),
             &input,
             &output);

     EXPECT_TRUE(
         duplicate_result == ENP_RREQ_RESULT_DROP_DUPLICATE,
         "duplicate RREQ is dropped");

     EXPECT_TRUE(
         s_node_b.duplicate_checks == 2U,
         "duplicate RREQ performs duplicate check");

     EXPECT_TRUE(
         s_node_b.reverse_updates == 1U,
         "duplicate RREQ does not learn reverse route again");

     EXPECT_TRUE(
         enp_route_table_active_count(&s_node_b.table) == 1U,
         "duplicate RREQ does not create another active route");
 }

 static void test_ttl_expiration_boundary(void)
 {
     enp_rreq_processor_t processor;
     enp_routing_rreq_t input;
     enp_routing_rreq_t output;

     reset_node(&s_node_c);

     EXPECT_TRUE(
         init_processor(&processor, &s_node_c, 3U),
         "C RREQ processor initialized");

     /*
      * TTL=1 is the forwarding boundary.  The RREQ is still processed
      * sufficiently to learn the reverse route, but must not propagate.
      */
     input = make_rreq(
         99U,      /* not C, therefore forwarding would normally occur */
         0xD002U,
         100U,
         4U,
         1U);

     s_node_c.duplicate = false;

     enp_rreq_result_t result =
         enp_rreq_processor_handle(
             &processor,
             node(1U),       /* A */
             node(2U),       /* B */
             &input,
             &output);

     EXPECT_TRUE(
         result == ENP_RREQ_RESULT_DROP_TTL,
         "TTL=1 RREQ is dropped at forwarding boundary");

     EXPECT_TRUE(
         s_node_c.reverse_updates == 1U,
         "TTL=1 RREQ still learns reverse route");

     EXPECT_TRUE(
         s_node_c.duplicate_checks == 1U,
         "TTL=1 RREQ performs duplicate check");

     const enp_route_entry_t *reverse_route =
         enp_route_table_lookup_const(
             &s_node_c.table,
             route_destination(node(1U)));

     EXPECT_TRUE(
         reverse_route != NULL,
         "C has reverse route to A after TTL drop");

     if (reverse_route != NULL) {
         EXPECT_TRUE(
             reverse_route->next_hop.node_id == 2U,
             "C reverse route to A uses B as next hop");

         EXPECT_TRUE(
             reverse_route->metric.valid &&
             reverse_route->metric.value == 5U,
             "TTL drop reverse route records incremented hop count");
     }

     /*
      * The processor API always provides a forward-output buffer.
      * For DROP_TTL the integration layer must not transmit it.
      * The output is therefore deliberately not treated as a
      * transmission request.
      */
     EXPECT_TRUE(
         result != ENP_RREQ_RESULT_FORWARD,
         "TTL=1 RREQ is not forwarded");
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "ENP v0.2 E1.2 duplicate RREQ + TTL");
     ESP_LOGI(TAG, "======================================");

     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "Duplicate RREQ integration tests");
     test_duplicate_rreq();

     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "TTL boundary integration tests");
     test_ttl_expiration_boundary();

     ESP_LOGI(TAG, "======================================");

     if (s_failures == 0) {
         ESP_LOGI(
             TAG,
             "ALL E1.2 DUPLICATE + TTL TESTS PASSED");
     } else {
         ESP_LOGE(
             TAG,
             "%d E1.2 TEST(S) FAILED",
             s_failures);
     }

     ESP_LOGI(TAG, "======================================");
 }




