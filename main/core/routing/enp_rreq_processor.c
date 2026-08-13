/*
 * enp_rreq_processor.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "core/routing/enp_rreq_processor.h"

 #include <string.h>

 static bool node_valid(enp_rreq_node_t node)
 {
     return node.network_id != 0U &&
            node.node_id != 0U;
 }

 static bool rreq_valid(const enp_routing_rreq_t *rreq)
 {
     if (rreq == NULL) return false;
     if (rreq->payload_version != ENP_ROUTING_PAYLOAD_VERSION) return false;
     if (rreq->subtype != ENP_ROUTING_SUBTYPE_RREQ) return false;
     if (rreq->destination_network_id == 0U || rreq->destination_node_id == 0U) return false;
     if (rreq->route_request_id == 0U) return false;
     if (rreq->ttl == 0U) return false;
     if (rreq->hop_count == UINT8_MAX) return false;
     return true;
 }

 bool enp_rreq_processor_init(
     enp_rreq_processor_t *processor,
     enp_rreq_node_t local_node,
     const enp_rreq_processor_callbacks_t *callbacks)
 {
     if (processor == NULL || !node_valid(local_node) || callbacks == NULL ||
         callbacks->is_duplicate == NULL || callbacks->learn_reverse_route == NULL) {
         return false;
     }

     memset(processor, 0, sizeof(*processor));
     processor->local_node = local_node;
     processor->callbacks = *callbacks;
     return true;
 }

 enp_rreq_result_t enp_rreq_processor_handle(
     enp_rreq_processor_t *processor,
     enp_rreq_node_t originator,
     enp_rreq_node_t immediate_sender,
     const enp_routing_rreq_t *rreq,
     enp_routing_rreq_t *forward_rreq)
 {
     if (processor == NULL || rreq == NULL || forward_rreq == NULL ||
         !node_valid(processor->local_node) ||
         !node_valid(originator) || !node_valid(immediate_sender) ||
         !rreq_valid(rreq)) {
         return ENP_RREQ_RESULT_REJECT;
     }

     if (processor->callbacks.is_duplicate(
             processor->callbacks.context,
             originator,
             rreq->route_request_id)) {
         return ENP_RREQ_RESULT_DROP_DUPLICATE;
     }

     const uint8_t new_hop_count = (uint8_t)(rreq->hop_count + 1U);

     if (!processor->callbacks.learn_reverse_route(
             processor->callbacks.context,
             originator,
             immediate_sender,
             new_hop_count,
             rreq->destination_sequence,
             rreq->route_lifetime_ms)) {
         return ENP_RREQ_RESULT_REJECT;
     }

     if (rreq->destination_network_id == processor->local_node.network_id &&
         rreq->destination_node_id == processor->local_node.node_id) {
         *forward_rreq = *rreq;
         return ENP_RREQ_RESULT_REPLY;
     }

     if (rreq->ttl <= 1U) {
         return ENP_RREQ_RESULT_DROP_TTL;
     }

     *forward_rreq = *rreq;
     forward_rreq->hop_count = new_hop_count;
     forward_rreq->ttl = (uint8_t)(rreq->ttl - 1U);

     return ENP_RREQ_RESULT_FORWARD;
 }


