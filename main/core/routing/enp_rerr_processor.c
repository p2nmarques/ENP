/*
 * enp_rerr_processor.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_rerr_processor.h"

 #include <string.h>
 #include "esp_log.h"

 static bool destination_valid(enp_rerr_destination_t destination)
 {
     return destination.network_id != 0U &&
            destination.node_id != 0U;
 }

 /*
  * RFC-style 32-bit serial comparison.
  *
  * candidate >= reference means:
  *   candidate == reference, or candidate is newer than reference.
  *
  * The protocol assumes compared values are never separated by 2^31
  * or more increments.
  */
 static bool sequence_is_newer_or_equal(
     uint32_t candidate,
     uint32_t reference)
 {
     return (int32_t)(candidate - reference) >= 0;
 }

 static bool reason_valid(uint8_t reason)
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

 bool enp_rerr_processor_init(
     enp_rerr_processor_t *processor,
     const enp_rerr_processor_callbacks_t *callbacks)
 {
     if (processor == NULL ||
         callbacks == NULL ||
         callbacks->lookup_route == NULL ||
         callbacks->invalidate_route == NULL) {
         return false;
     }

     memset(processor, 0, sizeof(*processor));
     processor->callbacks = *callbacks;

     return true;
 }

 enp_rerr_result_t enp_rerr_processor_handle(
     enp_rerr_processor_t *processor,
     const enp_routing_rerr_t *rerr)
 {
		
     if (processor == NULL || rerr == NULL) {
         return ENP_RERR_RESULT_REJECT;
     }
	 
	 ESP_LOGI(
	 	    "RERRPROC",
	 	    "RERR validate: version=%u subtype=%u reason=%u "
	 	    "reserved0=%u reserved1=%lu",
	 	    (unsigned)rerr->payload_version,
	 	    (unsigned)rerr->subtype,
	 	    (unsigned)rerr->reason,
	 	    (unsigned)rerr->reserved_0,
	 	    (unsigned long)rerr->reserved_1);

	 if (rerr->subtype != ENP_ROUTING_SUBTYPE_RERR ||
	     rerr->payload_version != ENP_ROUTING_PAYLOAD_VERSION ||
	     rerr->reserved_0 != 0U ||
	     rerr->reserved_1 != 0U ||
	     !reason_valid(rerr->reason)) {
	     return ENP_RERR_RESULT_REJECT;
	 }

     enp_rerr_destination_t destination = {
         .network_id = rerr->unreachable_network_id,
         .node_id = rerr->unreachable_node_id
     };

     if (!destination_valid(destination)) {
         return ENP_RERR_RESULT_REJECT;
     }

     enp_rerr_route_info_t route_info = {0};

     if (!processor->callbacks.lookup_route(
             processor->callbacks.context,
             destination,
             &route_info)) {
         return ENP_RERR_RESULT_REJECT;
     }

     if (!route_info.installed) {
         return ENP_RERR_RESULT_IGNORED_NO_ROUTE;
     }

     if (!route_info.active) {
         return ENP_RERR_RESULT_IGNORED_INACTIVE;
     }

     /*
      * Unknown stored sequence (0) may be invalidated by any RERR.
      * A known stored sequence must not be invalidated by an unknown RERR.
      */
     if (route_info.destination_sequence != 0U &&
         rerr->destination_sequence == 0U) {
         return ENP_RERR_RESULT_IGNORED_STALE;
     }

     if (!sequence_is_newer_or_equal(
             rerr->destination_sequence,
             route_info.destination_sequence)) {
         return ENP_RERR_RESULT_IGNORED_STALE;
     }

     if (!processor->callbacks.invalidate_route(
             processor->callbacks.context,
             destination)) {
         return ENP_RERR_RESULT_REJECT;
     }

     return ENP_RERR_RESULT_INVALIDATED;
 }



