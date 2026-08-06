/*
 * enp_network.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_network.h
  *
  * @brief ENP network.
  */

 #ifndef ENP_NETWORK_H
 #define ENP_NETWORK_H

 #include "enp_node.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Network
  *---------------------------------------------------------*/

 /**
  * @brief Represents the local ENP network context.
  */
 typedef struct
 {
     /**
      * Logical network identifier.
      
     enp_network_id_t id;*/

     /**
      * Local ENP node.
      */
     enp_node_t node;

 } enp_network_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_NETWORK_H */
