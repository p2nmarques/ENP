/*
 * enp_transport.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_transport.h
  *
  * @brief ENP link transport abstraction.
  */

 #ifndef ENP_TRANSPORT_H
 #define ENP_TRANSPORT_H

 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 /*----------------------------------------------------------
  * Callback Types
  *---------------------------------------------------------*/

  struct enp_transport;
  
 /**
  * @brief Receive callback.
  */

  typedef void (*enp_transport_receive_cb_t)(
          const struct enp_transport *transport,
          const uint8_t *address,
          const void *data,
          size_t length);

 /*----------------------------------------------------------
  * Transport Operations
  *---------------------------------------------------------*/

 typedef struct
 {
     esp_err_t (*init)(void);

     esp_err_t (*deinit)(void);

     esp_err_t (*send)(
             const uint8_t *address,
             const void *data,
             size_t length);

     esp_err_t (*set_receive_callback)(
             enp_transport_receive_cb_t callback);

 } enp_transport_ops_t;

 /*----------------------------------------------------------
  * Transport Descriptor
  *---------------------------------------------------------*/

 typedef struct
 {
     /**
      * Human-readable transport name.
      */
     const char *name;

     /**
      * Link address length in bytes.
      *
      * Example:
      * ESP-NOW : 6
      * Ethernet: 6
      * UART     : 0
      */
     uint8_t address_length;

     /**
      * Driver operations.
      */
     enp_transport_ops_t ops;

 } enp_transport_t;

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 esp_err_t enp_transport_init(
         const enp_transport_t *transport);

 esp_err_t enp_transport_deinit(
         const enp_transport_t *transport);

 esp_err_t enp_transport_send(
         const enp_transport_t *transport,
         const uint8_t *address,
         const void *data,
         size_t length);

 esp_err_t enp_transport_set_receive_callback(
         const enp_transport_t *transport,
         enp_transport_receive_cb_t callback);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_TRANSPORT_H */