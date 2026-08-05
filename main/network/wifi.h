/*
 * wifi.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef WIFI_H
 #define WIFI_H

 #include <stdbool.h>
 #include <stdint.h>

 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 esp_err_t enp_wifi_init(void);

 bool enp_wifi_is_connected(void);

 uint8_t enp_wifi_get_channel(void);

 #ifdef __cplusplus
 }
 #endif

 #endif
