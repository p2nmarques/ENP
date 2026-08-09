/*
 * enp_address.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_address.h
  *
  * @brief ENP logical network address.
  */

 #ifndef ENP_ADDRESS_H
 #define ENP_ADDRESS_H

 #include <stdbool.h>
 #include <stdint.h>

 #include "enp_types.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Address
  *---------------------------------------------------------*/

 /**
  * @brief ENP logical node address.
  *
  * An ENP address uniquely identifies a node within an
  * ENP network.
  *
  * Wire representation:
  *
  *     +----------------+----------------+
  *     | Network ID     | Node ID        |
  *     | 2 bytes        | 4 bytes        |
  *     +----------------+----------------+
  *
  * Total size: 6 bytes.
  *
  * Multi-byte integer fields use little-endian byte order.
  */
 typedef struct __attribute__((packed))
 {
     /**
      * Logical network identifier.
      */
     enp_network_id_t network;

     /**
      * Logical node identifier.
      */
     enp_node_id_t node;

 } enp_address_t;

 /*----------------------------------------------------------
  * Address Constants
  *---------------------------------------------------------*/

 /**
  * @brief Broadcast node identifier.
  *
  * A node identifier of zero represents broadcast.
  */
 #define ENP_NODE_BROADCAST \
     ((enp_node_id_t)0U)

 /**
  * @brief Any network identifier.
  *
  * A network identifier of zero represents any network
  * where this value is applicable.
  */
 #define ENP_NETWORK_ANY \
     ((enp_network_id_t)0U)

 /*----------------------------------------------------------
  * Address API
  *---------------------------------------------------------*/

 /**
  * @brief Compare two ENP addresses.
  *
  * @param left First address.
  * @param right Second address.
  *
  * @return true if both addresses are equal.
  * @return false otherwise.
  */
 bool enp_address_equal(
         const enp_address_t *left,
         const enp_address_t *right);

 /**
  * @brief Check whether an ENP address is a broadcast address.
  *
  * @param address Address to check.
  *
  * @return true if the node identifier represents broadcast.
  * @return false otherwise.
  */
 bool enp_address_is_broadcast(
         const enp_address_t *address);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_ADDRESS_H */