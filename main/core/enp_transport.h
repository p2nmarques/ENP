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
  *
  * The transport layer provides the interface between the
  * ENP protocol and the underlying communication technology.
  *
  * The transport is intentionally protocol-agnostic. It
  * operates on raw byte buffers and transport-specific
  * addresses.
  */

 #ifndef ENP_TRANSPORT_H
 #define ENP_TRANSPORT_H

 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #include "config/enp_config.h"

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
  * The interpretation of this address is defined by the
  * active transport implementation.
  *
  * For ESP-NOW this will normally contain a 6-byte MAC
  * address.
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
  * The callback is invoked when raw data is received from
  * the underlying transport.
  *
  * The supplied data is only valid for the duration of the
  * callback unless the transport implementation explicitly
  * documents otherwise.
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
  *
  * A transport implementation provides these operations to
  * the ENP Core.
  */
 typedef struct
 {
     /**
      * Initialize the transport.
      *
      * The configuration is read-only and remains owned by
      * the caller.
      */
     esp_err_t (*init)(
             const enp_config_t *config);

     /**
      * Deinitialize the transport.
      */
     esp_err_t (*deinit)(void);

     /**
      * Send raw data to a transport address.
      */
     esp_err_t (*send)(
             const enp_transport_address_t *destination,
             const void *data,
             size_t length);

     /**
      * Register the transport receive callback.
      */
     esp_err_t (*set_receive_callback)(
             enp_transport_receive_callback_t callback);

 } enp_transport_t;

 /*----------------------------------------------------------
  * Transport Lifecycle
  *---------------------------------------------------------*/

 /**
  * @brief Initialize a transport.
  *
  * @param transport Transport interface.
  * @param config ENP configuration.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for invalid arguments.
  * @return Transport-specific error otherwise.
  */
 esp_err_t enp_transport_init(
         enp_transport_t *transport,
         const enp_config_t *config);

 /**
  * @brief Deinitialize a transport.
  *
  * @param transport Transport interface.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for invalid arguments.
  * @return Transport-specific error otherwise.
  */
 esp_err_t enp_transport_deinit(
         enp_transport_t *transport);

 /*----------------------------------------------------------
  * Transport Data
  *---------------------------------------------------------*/

 /**
  * @brief Send raw data through a transport.
  *
  * @param transport Transport interface.
  * @param destination Destination transport address.
  * @param data Data to send.
  * @param length Number of bytes to send.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for invalid arguments.
  * @return Transport-specific error otherwise.
  */
 esp_err_t enp_transport_send(
         enp_transport_t *transport,
         const enp_transport_address_t *destination,
         const void *data,
         size_t length);

 /**
  * @brief Register a transport receive callback.
  *
  * @param transport Transport interface.
  * @param callback Receive callback.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for invalid arguments.
  * @return Transport-specific error otherwise.
  */
 esp_err_t enp_transport_set_receive_callback(
         enp_transport_t *transport,
         enp_transport_receive_callback_t callback);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_TRANSPORT_H */