/*
 * enp_network.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_network.h
  *
  * @brief ENP network runtime representation.
  */

 #ifndef ENP_NETWORK_H
 #define ENP_NETWORK_H

 #include "enp_node.h"
 #include "enp_types.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Network
  *---------------------------------------------------------*/

 /**
  * @brief ENP network runtime state.
  *
  * Represents the local ENP network identity and the local
  * node participating in that network.
  *
  * Routing information, neighbor tables and topology state
  * are intentionally not part of this structure. Those
  * responsibilities belong to higher-level ENP services.
  */
 typedef struct
 {
     /**
      * Logical ENP network identifier.
      */
     enp_network_id_t id;

     /**
      * Local node runtime state.
      */
     enp_node_t local;

 } enp_network_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_NETWORK_H */