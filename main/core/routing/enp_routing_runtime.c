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

 #include "core/routing/enp_routing_runtime.h"

 #include <string.h>

 #include "core/routing/enp_route_failure_coalescer.h"
 #include "core/routing/enp_route_repair.h"
 #include "core/routing/enp_route_repair_adapter.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_routing_data_path.h"


 /*
  * Single authoritative production routing runtime state.
  *
  * These objects intentionally have static lifetime.
  *
  * In particular, s_repair contains the static FreeRTOS queue and task
  * backing storage used by E5D.
  */
 static enp_route_table_t s_routes;
 static enp_route_repair_t s_repair;
 static enp_route_failure_coalescer_t s_failure_coalescer;
 static enp_route_repair_adapter_t s_repair_adapter;
 static enp_routing_data_path_t s_data_path;

 static bool s_initialized;


 /*
  * E5C → IG-D notification boundary.
  *
  * This function executes in the E5C route-failure callback context.
  *
  * It performs no route discovery, route-table mutation, blocking operation,
  * or synchronous repair processing.
  */
 static void routing_runtime_route_failure(
     void *context,
     enp_route_destination_t destination,
     enp_route_destination_t failed_next_hop)
 {
     enp_route_failure_coalescer_t *coalescer =
         (enp_route_failure_coalescer_t *)context;

     if (coalescer == NULL) {
         return;
     }

     (void)enp_route_failure_coalescer_observe(
         coalescer,
         destination,
         failed_next_hop);
 }


 bool enp_routing_runtime_init(
     const enp_routing_runtime_config_t *config)
 {
     if (config == NULL) {
         return false;
     }

     /*
      * Reinitialization is not permitted.
      *
      * E5D owns a running FreeRTOS task after successful initialization.
      * Resetting its backing storage would therefore be unsafe.
      */
     if (s_initialized) {
         return false;
     }

     /*
      * Validate every external dependency before initialization begins.
      */
     if (config->transport == NULL ||
         config->select_next_hop == NULL ||
         config->resolve_transport == NULL ||
         config->now_ms == NULL) {
         return false;
     }

     /*
      * Start from known zeroed state before any component initialization.
      */
     memset(&s_routes, 0, sizeof(s_routes));
     memset(&s_repair, 0, sizeof(s_repair));
     memset(&s_failure_coalescer, 0, sizeof(s_failure_coalescer));
     memset(&s_repair_adapter, 0, sizeof(s_repair_adapter));
     memset(&s_data_path, 0, sizeof(s_data_path));


     /*
      * 1. R3-B — route table.
      */
     if (!enp_route_table_init(&s_routes)) {
         return false;
     }


     /*
      * 2. R4 — route-repair adapter.
      *
      * The adapter receives the E5D repair object as its consume context.
      * The repair coordinator itself is initialized immediately afterwards.
      *
      * This preserves the already accepted integration ordering.
      */
     if (!enp_route_repair_adapter_init(
             &s_repair_adapter,
             &s_repair,
             &s_routes,
             config->transport,
             config->local_address,
             config->select_next_hop,
             config->select_next_hop_context,
             config->resolve_transport,
             config->resolve_transport_context,
             config->now_ms,
             config->now_ms_context)) {
         return false;
     }


     /*
      * 3. E5D — route-repair coordinator.
      *
      * Repair consumption occurs asynchronously through the adapter.
      */
     if (!enp_route_repair_init(
             &s_repair,
             enp_route_repair_adapter_consume,
             &s_repair_adapter)) {
         return false;
     }


     /*
      * 4. IG-D — bounded failure-event coalescer.
      *
      * The coalescer is connected only through the accepted E5D boundary.
      */
     if (!enp_route_failure_coalescer_init(
             &s_failure_coalescer,
             &s_repair)) {
         return false;
     }


     /*
      * 5. E5C — routing data path.
      */
     if (!enp_routing_data_path_init(
             &s_data_path,
             &s_routes,
             config->transport,
             config->resolve_transport,
             config->resolve_transport_context)) {
         return false;
     }


     /*
      * 6. Production E5C → IG-D connection.
      *
      * E5C remains responsible for its own route-state transition.
      * After E5C has classified an ACTIVE → STALE route failure, it
      * notifies IG-D through this registered callback.
      */
     if (!enp_routing_data_path_set_route_failure_callback(
             &s_data_path,
             routing_runtime_route_failure,
             &s_failure_coalescer)) {
         return false;
     }


     s_initialized = true;

     return true;
 }


 bool enp_routing_runtime_is_initialized(void)
 {
     return s_initialized;
 }


 enp_route_table_t *enp_routing_runtime_route_table(void)
 {
     if (!s_initialized) {
         return NULL;
     }

     return &s_routes;
 }


 enp_routing_data_path_t *enp_routing_runtime_data_path(void)
 {
     if (!s_initialized) {
         return NULL;
     }

     return &s_data_path;
 }


 enp_route_repair_adapter_t *
 enp_routing_runtime_repair_adapter(void)
 {
     if (!s_initialized) {
         return NULL;
     }

     return &s_repair_adapter;
 }


