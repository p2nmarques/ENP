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

 #include "enp_types.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Address
  *---------------------------------------------------------*/

 /**
  * @brief ENP logical address.
  *
  * An ENP address uniquely identifies a node within
  * an ENP network.
  */
 typedef struct
 {
     /**
      * Network identifier.
      */
     enp_network_id_t network;

     /**
      * Node identifier.
      */
     enp_node_id_t node;

 } enp_address_t;

 /*----------------------------------------------------------
  * Constants
  *---------------------------------------------------------*/

  #define ENP_NODE_BROADCAST      ((enp_node_id_t)0U)
  #define ENP_NETWORK_ANY         ((enp_network_id_t)0U)

 /*----------------------------------------------------------
  * Helpers
  *---------------------------------------------------------*/

 /**
  * @brief Compare two ENP addresses.
  */
 bool enp_address_equal(
         const enp_address_t *left,
         const enp_address_t *right);

 /**
  * @brief Returns true if the address is a broadcast address.
  */
 bool enp_address_is_broadcast(
         const enp_address_t *address);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_ADDRESS_H */