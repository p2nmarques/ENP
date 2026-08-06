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

 #include "/config/enp_config.h"
#include "./protocol/enp_protocol.h"
 #include "enp_transport.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 /**
  * @brief Runtime instance of ENP.
  *
  * Every ENP application owns exactly one context.
  *
  * The context owns:
  *   - Runtime network state
  *   - Local node
  *   - Active link transport
  *
  * The context does NOT own:
  *   - Dispatcher
  *   - Services
  *   - Tasks
  *   - Timers
  */
 typedef struct
 {
     /**
      * Runtime network state.
      */
     enp_network_t network;

     /**
      * Active link implementation.
      */
     const enp_transport_t *transport;

 } enp_context_t;

 /**
  * @brief Create and initialize an ENP instance.
  *
  * This function initializes:
  *  - Runtime context
  *  - Selected link transport
  *  - Local node
  *  - Network state
  *
  * @param context Runtime context.
  * @param transport Link implementation.
  * @param config ENP configuration.
  *
  * @return
  *      - ESP_OK
  *      - ESP_ERR_INVALID_ARG
  *      - Transport specific errors
  */
 esp_err_t enp_context_init(
         enp_context_t *context,
         const enp_transport_t *transport,
         const enp_config_t *config);

 /**
  * @brief Shutdown an ENP instance.
  *
  * Releases any resources owned by the active transport.
  *
  * @param context Runtime context.
  *
  * @return
  *      - ESP_OK
  *      - ESP_ERR_INVALID_ARG
  */
 esp_err_t enp_context_deinit(
         enp_context_t *context);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_CONTEXT_H */