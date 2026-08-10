/*
 * enp.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp.h
  *
  * @brief ESP Network Protocol (ENP) Core API.
  *
  * This is the public entry point to the ENP library.
  *
  * Applications should normally include only this header.
  *
  * The ENP Core provides:
  *  - Configuration
  *  - Core types
  *  - Object model
  *  - Runtime context
  *  - Transport abstraction
  *  - Protocol definitions
  *  - Generic packet API
  *
  * The following modules are intentionally NOT exposed:
  *  - Link implementations (ESP-NOW, Wi-Fi, BLE, ...)
  *  - Dispatcher
  *  - Services
  *  - Internal utilities
  */

 #ifndef ENP_H
 #define ENP_H

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Library Version
  *---------------------------------------------------------*/

 #define ENP_VERSION_MAJOR        0U
 #define ENP_VERSION_MINOR        2U
 #define ENP_VERSION_PATCH        0U

 /*----------------------------------------------------------
  * Configuration
  *---------------------------------------------------------*/

 #include "config/enp_config.h"

 /*----------------------------------------------------------
  * Core Types
  *---------------------------------------------------------*/

 #include "enp_types.h"

 /*----------------------------------------------------------
  * Object Model
  *---------------------------------------------------------*/

 #include "enp_node.h"
 #include "enp_network.h"

 /*----------------------------------------------------------
  * Runtime
  *---------------------------------------------------------*/

 #include "enp_context.h"
 #include "enp_maintenance.h"
 #include "enp_transport.h"

 /*----------------------------------------------------------
  * Protocol
  *---------------------------------------------------------*/

 #include "protocol/enp_protocol.h"
 #include "protocol/enp_packet.h"

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_H */