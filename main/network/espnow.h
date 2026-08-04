/*
 * espnow.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef ESPNOW_H
 #define ESPNOW_H

 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 typedef void (*espnow_receive_callback_t)(
         const uint8_t *mac,
         const void *data,
         size_t len);
		 
 void espnow_register_receive_callback(
          espnow_receive_callback_t callback);

 esp_err_t espnow_init(void);

 esp_err_t espnow_send(
         const uint8_t *mac,
         const void *data,
         size_t len);

 esp_err_t espnow_add_peer(
         const uint8_t *mac);
		 
 #ifdef __cplusplus
 }
 #endif

 #endif
