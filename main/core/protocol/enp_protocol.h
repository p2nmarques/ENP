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
  */

 #ifndef ENP_PROTOCOL_H
 #define ENP_PROTOCOL_H

 #include <stdint.h>

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Protocol Version
  *---------------------------------------------------------*/

 /**
  * @brief ENP wire protocol version.
  *
  * This value identifies the ENP protocol version carried
  * in every frame header.
  */
 #define ENP_PROTOCOL_VERSION      ((uint8_t)1U)

 /*----------------------------------------------------------
  * Protocol Constants
  *---------------------------------------------------------*/

 /**
  * @brief ENP protocol magic.
  */
 #define ENP_PROTOCOL_MAGIC        ((uint32_t)0x454E5001UL)

 /**
  * @brief Maximum packet time-to-live.
  */
 #define ENP_MAX_TTL               ((uint8_t)16U)

 /**
  * @brief Default packet time-to-live.
  */
 #define ENP_DEFAULT_TTL           ENP_MAX_TTL

 /*----------------------------------------------------------
  * Packet Types
  *---------------------------------------------------------*/

 /**
  * @brief ENP packet types.
  */
 typedef enum
 {
     ENP_PACKET_INVALID = 0,

     ENP_PACKET_DISCOVERY,

     ENP_PACKET_HEARTBEAT,

     ENP_PACKET_SENSOR,

     ENP_PACKET_ACK,

     ENP_PACKET_ROUTE,

     ENP_PACKET_APPLICATION

 } enp_packet_type_t;

 /*----------------------------------------------------------
  * Packet Flags
  *---------------------------------------------------------*/

 /**
  * @brief Packet contains no flags.
  */
 #define ENP_FLAG_NONE             ((uint8_t)0x00U)

 /**
  * @brief Packet requires an acknowledgement.
  */
 #define ENP_FLAG_ACK_REQUIRED     ((uint8_t)(1U << 0))

 /**
  * @brief Packet is an acknowledgement.
  */
 #define ENP_FLAG_ACK              ((uint8_t)(1U << 1))

 /**
  * @brief Packet is a broadcast.
  */
 #define ENP_FLAG_BROADCAST        ((uint8_t)(1U << 2))

 /**
  * @brief Packet payload is encrypted.
  */
 #define ENP_FLAG_ENCRYPTED        ((uint8_t)(1U << 3))

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_PROTOCOL_H */