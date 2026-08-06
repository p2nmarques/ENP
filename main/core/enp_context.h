/*
 * enp_context.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */
 
 /**
  * @file enp_context.h
  *
  * @brief ENP runtime context.
  */

 #ifndef ENP_CONTEXT_H
 #define ENP_CONTEXT_H

 #include "esp_err.h"

 #include "config/enp_config.h"

 #include "enp_network.h"
 #include "enp_transport.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Runtime Context
  *---------------------------------------------------------*/

 /**
  * @brief Runtime instance of an ENP protocol stack.
  *
  * The context owns the runtime state of one ENP instance.
  */
 typedef struct
 {
     /**
      * Local network.
      */
     enp_network_t network;

     /**
      * Active transport.
      */
     enp_transport_t transport;

 } enp_context_t;

 /*----------------------------------------------------------
  * Lifecycle
  *---------------------------------------------------------*/

 /**
  * @brief Initialize an ENP runtime context.
  *
  * @param context Runtime context.
  * @param config ENP configuration.
  * @param transport Transport implementation.
  *
  * @return ESP_OK on success.
  */
 esp_err_t enp_context_init(
         enp_context_t *context,
         const enp_config_t *config,
         const enp_transport_t *transport);

 /**
  * @brief Deinitialize an ENP runtime context.
  *
  * @param context Runtime context.
  *
  * @return ESP_OK on success.
  */
 esp_err_t enp_context_deinit(
         enp_context_t *context);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_CONTEXT_H */