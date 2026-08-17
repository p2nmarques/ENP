/*
 * enp_receive_path.h
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7 Phase 4 / P4-E4B — ENP production receive-path integration.
 */

 #ifndef ENP_RECEIVE_PATH_H
 #define ENP_RECEIVE_PATH_H

 #include <stdbool.h>
 #include <stddef.h>

 #include "esp_err.h"

 #include "core/enp_context.h"
 #include "core/enp_transport.h"
 #include "core/data/enp_data_plane.h"
 #include "core/routing/enp_routing_data_path.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 typedef struct
 {
     enp_context_t *context;
     enp_routing_data_path_t *routing_path;
     enp_data_plane_t data_plane;
     bool initialized;
 } enp_receive_path_t;

 /**
  * @brief Initialize the ENP production receive path.
  *
  * DATA and ACK packets are passed to the ENP data plane. All other
  * packet types are passed to the normal dispatcher path.
  */
 esp_err_t enp_receive_path_init(
         enp_receive_path_t *path,
         enp_context_t *context,
         enp_routing_data_path_t *routing_path);

 /**
  * @brief Deinitialize the ENP production receive path.
  */
 esp_err_t enp_receive_path_deinit(
         enp_receive_path_t *path);

 /**
  * @brief Process one raw transport-received ENP frame.
  */
 esp_err_t enp_receive_path_process(
         enp_receive_path_t *path,
         const enp_transport_address_t *source,
         const void *data,
         size_t length);

 /**
  * @brief Transport callback adapter for the production receive path.
  *
  * The callback is suitable for enp_transport_set_receive_callback().
  * The receive path object is supplied through enp_receive_path_bind().
  */
 void enp_receive_path_transport_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length);

 /**
  * @brief Bind a receive-path instance to the transport callback adapter.
  */
 esp_err_t enp_receive_path_bind(
         enp_receive_path_t *path);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_RECEIVE_PATH_H */

