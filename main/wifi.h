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

 /**
  * @brief Initialize WiFi Station.
  *
  * This function starts the WiFi station and attempts to connect
  * to the configured access point.
  *
  * WiFi credentials are taken from Kconfig.
  */
 esp_err_t wifi_init(void);

 /**
  * @brief Returns true if connected to the AP.
  */
 bool wifi_is_connected(void);

 /**
  * @brief Returns the current WiFi channel.
  *
  * Returns 0 until connected.
  */
 uint8_t wifi_get_channel(void);

 #ifdef __cplusplus
 }
 #endif

 #endif
