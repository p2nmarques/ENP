/*
 * gateway.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef GATEWAY_H
 #define GATEWAY_H

 #include "esp_err.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 esp_err_t gateway_init(void);
 
 void gateway_print_stats(void);

 #ifdef __cplusplus
 }
 #endif

 #endif