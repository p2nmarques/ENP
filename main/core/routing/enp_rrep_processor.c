/*
 * enp_rrep_processor.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_rrep_processor.h"

 #include <string.h>

 static bool node_equal(enp_rrep_node_t a, enp_rrep_node_t b)
 {
     return a.network_id == b.network_id &&
            a.node_id == b.node_id;
 }

 static bool node_valid(enp_rrep_node_t node)
 {
     return !(node.network_id == 0U && node.node_id == 0U);
 }

 /*
  * RFC-style unsigned serial-number comparison.
  *
  * Returns true when candidate is newer than or equal to reference,
  * assuming differences remain below 2^31.
  */
 static bool sequence_is_newer_or_equal(
     uint32_t candidate,
     uint32_t reference)
 {
     return (int32_t)(candidate - reference) >= 0;
 }

 bool enp_rrep_processor_init(
     enp_rrep_processor_t *processor,
     enp_rrep_node_t local_node,
     enp_rrep_node_t discovery_originator,
     const enp_rrep_processor_callbacks_t *callbacks)
 {
     if (processor == NULL ||
         !node_valid(local_node) ||
         !node_valid(discovery_originator) ||
         callbacks == NULL ||
         callbacks->update_route == NULL ||
         callbacks->lookup_next_hop == NULL ||
         callbacks->discovery_complete == NULL) {
         return false;
     }

     memset(processor, 0, sizeof(*processor));
     processor->local_node = local_node;
     processor->discovery_originator = discovery_originator;
     processor->callbacks = *callbacks;

     return true;
 }

 enp_rrep_result_t enp_rrep_processor_handle(
     enp_rrep_processor_t *processor,
     enp_rrep_node_t previous_hop,
     const enp_routing_rrep_t *rrep,
     enp_routing_rrep_t *forward_rrep)
 {
     if (processor == NULL ||
         rrep == NULL ||
         forward_rrep == NULL ||
         !node_valid(previous_hop)) {
         return ENP_RREP_RESULT_REJECT;
     }

     if (rrep->payload_version != ENP_ROUTING_PAYLOAD_VERSION ||
         rrep->subtype != ENP_ROUTING_SUBTYPE_RREP) {
         return ENP_RREP_RESULT_REJECT;
     }

     if (rrep->destination_network_id == 0U &&
         rrep->destination_node_id == 0U) {
         return ENP_RREP_RESULT_REJECT;
     }

     if (rrep->hop_count == UINT8_MAX) {
         return ENP_RREP_RESULT_REJECT;
     }

     if (rrep->metric == UINT32_MAX) {
         return ENP_RREP_RESULT_REJECT;
     }

     enp_rrep_node_t destination = {
         .network_id = rrep->destination_network_id,
         .node_id = rrep->destination_node_id
     };

     /*
      * The RREP source is the route destination. Therefore the immediate
      * sender is the next hop toward that destination.
      *
      * This node learns/refreshes its forward route to the destination
      * before deciding whether the RREP itself must be forwarded.
      */
     uint8_t route_hop_count = rrep->hop_count;

     if (!processor->callbacks.update_route(
             processor->callbacks.context,
             destination,
             previous_hop,
             rrep->destination_sequence,
             route_hop_count,
             rrep->route_lifetime_ms,
             rrep->metric)) {
         return ENP_RREP_RESULT_REJECT;
     }

     /*
      * If the RREP has reached the originator of the outstanding discovery,
      * the discovery can be completed.
      */
     if (node_equal(
             processor->local_node,
             processor->discovery_originator)) {
         if (!processor->callbacks.discovery_complete(
                 processor->callbacks.context,
                 destination,
                 rrep->destination_sequence)) {
             return ENP_RREP_RESULT_REJECT;
         }

         return ENP_RREP_RESULT_COMPLETE;
     }

     /*
      * The RREP is travelling back toward the RREQ originator.
      * TTL is not part of the RREP payload in the current ENP v0.2 wire
      * definition, so forwarding is bounded by hop-count overflow only.
      *
      * The next-hop lookup is for the RREQ originator.
      */
     enp_rrep_node_t next_hop;

     if (!processor->callbacks.lookup_next_hop(
             processor->callbacks.context,
             processor->discovery_originator,
             &next_hop)) {
         return ENP_RREP_RESULT_DROP_NO_ROUTE;
     }

     if (!node_valid(next_hop)) {
         return ENP_RREP_RESULT_DROP_NO_ROUTE;
     }

     *forward_rrep = *rrep;
     ++forward_rrep->hop_count;

     /*
      * A forward route to the RREP destination was learned above. The
      * transport/dispatcher will use next_hop to deliver the forwarded
      * packet. The next hop itself is intentionally not stored in the
      * payload.
      */
     (void)next_hop;

     return ENP_RREP_RESULT_FORWARD;
 }



