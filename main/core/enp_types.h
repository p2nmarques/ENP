/*
 * enp_types.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_types.h
  *
  * @brief Fundamental ENP type definitions.
  *
  * This file defines the primitive types used throughout
  * the ENP Core and Protocol.
  */

 #ifndef ENP_TYPES_H
 #define ENP_TYPES_H

 #include <stdint.h>

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Primitive Types
  *---------------------------------------------------------*/

 /**
  * @brief ENP node identifier.
  */
 typedef uint32_t enp_node_id_t;

 /**
  * @brief ENP network identifier.
  */
 typedef uint16_t enp_network_id_t;

 /**
  * @brief ENP packet sequence number.
  */
 typedef uint32_t enp_sequence_t;

 /**
  * @brief ENP capability bitmap.
  */
 typedef uint32_t enp_capability_t;

 /*----------------------------------------------------------
  * Common Enumerations
  *---------------------------------------------------------*/

 /**
  * @brief ENP node role.
  */
 typedef enum
 {
     ENP_ROLE_UNKNOWN = 0,

     ENP_ROLE_GATEWAY,

     ENP_ROLE_SENSOR,

     ENP_ROLE_RELAY,

     ENP_ROLE_ROOT,

     ENP_ROLE_MONITOR

 } enp_role_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_TYPES_H */