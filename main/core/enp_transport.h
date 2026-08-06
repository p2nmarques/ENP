/*
 * enp_transport.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_transport.h
  *
  * @brief ENP transport abstraction.
  */

 #ifndef ENP_TRANSPORT_H
 #define ENP_TRANSPORT_H

 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #include "/config/enp_config.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Transport Address
  *---------------------------------------------------------*/

 /**
  * @brief Transport-specific address.
  *
  * The interpretation of this address depends on the
  * active transport implementation.
  */
 typedef struct
 {
     uint8_t value[16];

     uint8_t length;

 } enp_transport_address_t;

 /*----------------------------------------------------------
  * Receive Callback
  *---------------------------------------------------------*/

 /**
  * @brief Transport receive callback.
  *
  * @param source Source transport address.
  * @param data Received data.
  * @param length Number of received bytes.
  */
 typedef void (*enp_transport_receive_callback_t)(
         const enp_transport_address_t *source,
         const void *data,
         size_t length);

 /*----------------------------------------------------------
  * Transport Interface
  *---------------------------------------------------------*/

 /**
  * @brief ENP transport interface.
  */
 typedef struct
 {
     /**
      * Initialize transport.
      */
     esp_err_t (*init)(
             const enp_config_t *config);

     /**
      * Deinitialize transport.
      */
     esp_err_t (*deinit)(void);

     /**
      * Send raw bytes.
      */
     esp_err_t (*send)(
             const enp_transport_address_t *destination,
             const void *data,
             size_t length);

     /**
      * Register receive callback.
      */
     esp_err_t (*set_receive_callback)(
             enp_transport_receive_callback_t callback);

 } enp_transport_t;

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 /**
  * @brief Initialize transport.
  */
 esp_err_t enp_transport_init(
         enp_transport_t *transport,
         const enp_config_t *config);

 /**
  * @brief Deinitialize transport.
  */
 esp_err_t enp_transport_deinit(
         enp_transport_t *transport);

 /**
  * @brief Send raw bytes.
  */
 esp_err_t enp_transport_send(
         enp_transport_t *transport,
         const enp_transport_address_t *destination,
         const void *data,
         size_t length);

 /**
  * @brief Register receive callback.
  */
 esp_err_t enp_transport_set_receive_callback(
         enp_transport_t *transport,
         enp_transport_receive_callback_t callback);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_TRANSPORT_H */