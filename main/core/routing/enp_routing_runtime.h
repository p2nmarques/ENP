/*
 * enp_routing_runtime.h
 *
 *  Created on: Aug 28, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-F
 *
 * Dedicated production routing runtime owner.
 *
 * This module is the single production owner of:
 *
 *   R3-B  route table
 *   R4    route-repair adapter
 *   E5D   route-repair coordinator
 *   IG-D  route-failure coalescer
 *   E5C   routing data path
 *
 * ESP-IDF target: 6.0.2
 */

 #ifndef ENP_ROUTING_RUNTIME_H
 #define ENP_ROUTING_RUNTIME_H

 #include <stdbool.h>

 #include "core/enp_address.h"
 #include "core/enp_transport.h"

 #include "core/routing/enp_route_repair_adapter.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_routing_data_path.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 typedef struct {
     enp_transport_t *transport;

     enp_address_t local_address;

     enp_route_repair_select_next_hop_fn select_next_hop;
     void *select_next_hop_context;

     enp_routing_resolve_transport_fn resolve_transport;
     void *resolve_transport_context;

     enp_route_repair_now_ms_fn now_ms;
     void *now_ms_context;
 } enp_routing_runtime_config_t;

 /*
  * Initialize the single production routing runtime.
  *
  * Initialization ownership/order:
  *
  *   R3-B route table
  *       ↓
  *   R4 route-repair adapter
  *       ↓
  *   E5D route-repair coordinator
  *       ↓
  *   IG-D failure-event coalescer
  *       ↓
  *   E5C routing data path
  *       ↓
  *   E5C → IG-D failure callback registration
  *
  * The runtime owns all backing storage internally and therefore provides
  * static lifetime for the E5D repair task and all associated routing state.
  */
 bool enp_routing_runtime_init(
     const enp_routing_runtime_config_t *config);

 /*
  * Returns whether the production routing runtime has completed
  * initialization successfully.
  */
 bool enp_routing_runtime_is_initialized(void);

 /*
  * Accessors expose existing routing components without transferring
  * ownership.
  *
  * They return NULL until initialization succeeds.
  */
 enp_route_table_t *enp_routing_runtime_route_table(void);

 enp_routing_data_path_t *enp_routing_runtime_data_path(void);

 enp_route_repair_adapter_t *enp_routing_runtime_repair_adapter(void);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_ROUTING_RUNTIME_H */