/*
 * enp_network.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_protocol.h
  *
  * @brief ENP protocol definitions.
  *
  * This file defines the ENP wire protocol.
  * Any changes to this file may affect protocol compatibility.
  */

 #ifndef ENP_PROTOCOL_H
 #define ENP_PROTOCOL_H

 #include <stdint.h>

 #ifdef __cplusplus
 extern "C" {
 #endif

 /*----------------------------------------------------------
  * Protocol Version
  *---------------------------------------------------------*/

 #define ENP_PROTOCOL_VERSION_MAJOR      0U
 #define ENP_PROTOCOL_VERSION_MINOR      2U
 
 #define ENP_PROTOCOL_VERSION \
     ((ENP_PROTOCOL_VERSION_MAJOR << 8) | ENP_PROTOCOL_VERSION_MINOR)

 /*----------------------------------------------------------
  * Packet Identification
  *---------------------------------------------------------*/

 #define ENP_PACKET_MAGIC                0x454E5001UL

 /*----------------------------------------------------------
  * Packet Types
  *---------------------------------------------------------*/

 /*
  * Packet type allocation
  *
  * 0x00 Reserved
  *
  * 0x01 - 0x0F Discovery
  * 0x10 - 0x1F Data
  * 0x20 - 0x2F Reliability
  * 0x30 - 0x3F Routing
  * 0x40 - 0x4F Management
  * 0xF0 - 0xFF Reserved
  */

 typedef enum
 {
     /* Invalid */

     ENP_PACKET_INVALID = 0x00,

     /* Discovery */

     ENP_PACKET_DISCOVERY           = 0x01,
     ENP_PACKET_DISCOVERY_RESPONSE  = 0x02,

     /* Data */

     ENP_PACKET_SENSOR              = 0x10,

     /* Reliability */

     ENP_PACKET_ACK                 = 0x20,

     /* Routing */

     ENP_PACKET_ROUTE_REQUEST       = 0x30,
     ENP_PACKET_ROUTE_RESPONSE      = 0x31,

     /* Management */

     ENP_PACKET_HEARTBEAT           = 0x40,
     ENP_PACKET_MANAGEMENT          = 0x41

 } enp_packet_type_t;

 /*----------------------------------------------------------
  * Packet Flags
  *---------------------------------------------------------*/

 #define ENP_FLAG_NONE               0x00

 #define ENP_FLAG_ACK_REQUIRED       (1U << 0)

 #define ENP_FLAG_BROADCAST          (1U << 1)

 #define ENP_FLAG_ENCRYPTED          (1U << 2)

 #define ENP_FLAG_FRAGMENTED         (1U << 3)

 /*----------------------------------------------------------
  * Reserved Values
  *---------------------------------------------------------*/

 #define ENP_NODE_BROADCAST          0U

 #define ENP_NETWORK_ANY             0U

 #define ENP_DEFAULT_TTL             1U

 /*----------------------------------------------------------
  * Packet Limits
  *---------------------------------------------------------*/

 /*
  * Maximum packet size supported by ENP.
  *
  * This should not exceed the payload size supported
  * by the active transport.
  */

 #define ENP_MAX_PACKET_SIZE         250U

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_PROTOCOL_H */
