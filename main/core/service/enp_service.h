/*
 * enp_service.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_service.h
  *
  * @brief ENP service interface.
  *
  * A service provides application/protocol functionality for
  * one ENP packet type.
  *
  * Services are transport-independent.
  */

 #ifndef ENP_SERVICE_H
 #define ENP_SERVICE_H

 #include "esp_err.h"

 #include "core/enp_context.h"
 #include "core/protocol/enp_packet.h"
 #include "core/enp_transport.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Service Callbacks
  *---------------------------------------------------------*/

 /**
  * @brief Service initialization callback.
  *
  * @param context ENP runtime context.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for invalid arguments.
  * @return Service-specific error otherwise.
  */
 typedef esp_err_t (*enp_service_init_fn)(
         enp_context_t *context);

 /**
  * @brief Service packet processing callback.
  *
  * The dispatcher has already validated the complete ENP
  * packet before invoking this callback.
  *
  * @param context ENP runtime context.
  * @param packet Validated ENP packet.
  * @param source Transport address from which the packet
  *        was received.
  *
  * @return ESP_OK on success.
  * @return Service-specific error otherwise.
  */
 typedef esp_err_t (*enp_service_process_fn)(
         enp_context_t *context,
         const enp_packet_t *packet,
         const enp_transport_address_t *source);

 /*----------------------------------------------------------
  * Service Descriptor
  *---------------------------------------------------------*/

 /**
  * @brief ENP service descriptor.
  *
  * Each service handles one ENP packet type.
  */
 typedef struct
 {
     /**
      * Human-readable service name.
      */
     const char *name;

     /**
      * ENP packet type handled by this service.
      */
     enp_packet_type_t packet_type;

     /**
      * Optional service initialization callback.
      */
     enp_service_init_fn init;

     /**
      * Packet processing callback.
      *
      * Must not be NULL.
      */
     enp_service_process_fn process;

 } enp_service_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_SERVICE_H */