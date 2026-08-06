/*
 * enp_node.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_node.h
  *
  * @brief ENP network node.
  */

 #ifndef ENP_NODE_H
 #define ENP_NODE_H

 #include "enp_types.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Node
  *---------------------------------------------------------*/

 /**
  * @brief Represents an ENP network node.
  *
  * The node contains only its immutable identity and
  * communication state. Runtime information such as
  * discovery status, routing information, capabilities,
  * RSSI, or statistics are maintained by their respective
  * protocol services.
  */
 typedef struct
 {
     /**
      * Unique node identifier.
      */
     enp_node_id_t id;

     /**
      * Node role.
      */
     enp_role_t role;

     /**
      * Next outgoing packet sequence number.
      */
     enp_sequence_t sequence;

 } enp_node_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_NODE_H */
