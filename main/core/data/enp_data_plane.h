/*
 * enp_data_plane.h
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 */
 
 /**
  * @file enp_data_plane.h
  *
  * @brief ENP receive data-plane and packet forwarding integration.
  *
  * The data plane owns receive-time duplicate suppression for DATA and
  * ACK packet domains and forwards non-local DATA/ACK packets through
  * the transport-independent routing data path.
  */

 #ifndef ENP_DATA_PLANE_H
 #define ENP_DATA_PLANE_H

 #include <stdbool.h>
 #include <stddef.h>

 #include "esp_err.h"

 #include "core/enp_context.h"
 #include "core/enp_duplicate.h"
 #include "core/enp_transport.h"
 #include "core/protocol/enp_packet.h"
 #include "core/routing/enp_routing_data_path.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /**
  * @brief Local packet processing callback.
  *
  * The data plane invokes this callback only after packet validation,
  * destination-locality checking and duplicate suppression.
  *
  * The callback is transport-independent. A dispatcher adapter can be
  * supplied by the integration owner without making the data plane depend
  * on the dispatcher implementation.
  */
 typedef esp_err_t (*enp_data_plane_local_process_fn)(
         void *context,
         const enp_packet_t *packet,
         const enp_transport_address_t *source);

 /**
  * @brief ENP receive data-plane runtime state.
  *
  * All storage is supplied by this object. No dynamic allocation is used.
  */
 typedef struct
 {
     enp_context_t *context;
     enp_routing_data_path_t *routing_path;

     enp_duplicate_cache_t data_duplicates;
     enp_duplicate_cache_t ack_duplicates;

     enp_data_plane_local_process_fn local_process;

 } enp_data_plane_t;

 /**
  * @brief Initialize the ENP receive data plane.
  *
  * @param plane Data-plane runtime object.
  * @param context ENP runtime context.
  * @param routing_path Routing data path used for forwarding.
  * @param local_process Optional callback for packets addressed to this node.
  *
  * @return ESP_OK on success.
  */
 esp_err_t enp_data_plane_init(
         enp_data_plane_t *plane,
         enp_context_t *context,
         enp_routing_data_path_t *routing_path,
         enp_data_plane_local_process_fn local_process);

 /**
  * @brief Deinitialize the ENP receive data plane.
  *
  * The data plane does not own the context or routing path.
  *
  * @return ESP_OK on success.
  */
 esp_err_t enp_data_plane_deinit(
         enp_data_plane_t *plane);

 /**
  * @brief Process one validated transport-received ENP frame.
  *
  * The function validates the complete packet, applies packet-domain
  * duplicate suppression, delivers local packets through the optional
  * callback, and forwards non-local DATA/ACK packets through the routing
  * data path.
  *
  * DATA and ACK packets intentionally use separate duplicate domains.
  * Duplicate identity remains source logical address + sequence number.
  *
  * @param plane ENP data-plane runtime object.
  * @param packet Complete ENP packet.
  * @param source Transport address from which the packet was received.
  *
  * @return ESP_OK when the packet was delivered or forwarded successfully.
  * @return ESP_ERR_NOT_FOUND when no local callback exists for a local packet.
  * @return ESP_ERR_NOT_SUPPORTED for non-local packet types not handled by
  *         this data plane.
  */
 esp_err_t enp_data_plane_process(
         enp_data_plane_t *plane,
         const enp_packet_t *packet,
         const enp_transport_address_t *source);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_DATA_PLANE_H */


#ifndef ENP_DATA_PLANE_H_
#define ENP_DATA_PLANE_H_





#endif /* ENP_DATA_PLANE_H_ */
