/*
 * enp_neighbor.h
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_neighbor.h
  *
  * @brief ENP neighbor table.
  *
  * The neighbor subsystem maintains information about nodes
  * directly reachable by the local node.
  *
  * This module is transport-independent except for the
  * transport address associated with each neighbor.
  */

  #ifndef ENP_NEIGHBOR_H
  #define ENP_NEIGHBOR_H

  #include <stdbool.h>
  #include <stddef.h>
  #include <stdint.h>

  #include "esp_err.h"

  #include "freertos/FreeRTOS.h"
  #include "freertos/semphr.h"

  #include "core/enp_address.h"
  #include "config/enp_defaults.h"
  #include "core/enp_transport.h"
  #include "core/enp_types.h"

  #ifdef __cplusplus
  extern "C"
  {
  #endif

  /*----------------------------------------------------------
   * Neighbor State
   *---------------------------------------------------------*/

  typedef enum
  {
      ENP_NEIGHBOR_STATE_EMPTY = 0,

      ENP_NEIGHBOR_STATE_ACTIVE,

      ENP_NEIGHBOR_STATE_STALE

  } enp_neighbor_state_t;

  /*----------------------------------------------------------
   * Neighbor
   *---------------------------------------------------------*/

  typedef struct
  {
      /**
       * Logical ENP address.
       */
      enp_address_t address;

      /**
       * Physical transport address.
       */
      enp_transport_address_t transport_address;

      /**
       * Advertised node role.
       */
      enp_role_t role;

      /**
       * Advertised node capabilities.
       */
      uint16_t capabilities;

      /**
       * Last sequence number received.
       */
      enp_sequence_t last_sequence;

      /**
       * Last received RSSI.
       *
       * Zero means unavailable.
       */
      int8_t rssi;

      /**
       * Last observation time in milliseconds.
       */
      uint32_t last_seen_ms;

      /**
       * Current neighbor state.
       */
      enp_neighbor_state_t state;

  } enp_neighbor_t;

  /*----------------------------------------------------------
   * Neighbor Table
   *---------------------------------------------------------*/

  typedef struct
  {
      enp_neighbor_t entries[ENP_MAX_NEIGHBORS];

      size_t count;

      /* Protects concurrent table mutation from the RX worker
      * and the periodic maintenance task. */
     SemaphoreHandle_t mutex;
     StaticSemaphore_t mutex_storage;

  } enp_neighbor_table_t;

  /*----------------------------------------------------------
   * Lifecycle
   *---------------------------------------------------------*/

  esp_err_t enp_neighbor_table_init(
          enp_neighbor_table_t *table);

  esp_err_t enp_neighbor_table_clear(
          enp_neighbor_table_t *table);

  /*----------------------------------------------------------
   * Update
   *---------------------------------------------------------*/

  esp_err_t enp_neighbor_update(
          enp_neighbor_table_t *table,
          const enp_address_t *address,
          const enp_transport_address_t *transport_address,
          enp_role_t role,
          uint16_t capabilities,
          enp_sequence_t sequence,
          int8_t rssi,
          uint32_t now_ms);

  /*----------------------------------------------------------
   * Lookup
   *---------------------------------------------------------*/

  enp_neighbor_t *enp_neighbor_find(
          enp_neighbor_table_t *table,
          const enp_address_t *address);

  const enp_neighbor_t *enp_neighbor_find_const(
          const enp_neighbor_table_t *table,
          const enp_address_t *address);

  /*----------------------------------------------------------
   * Removal
   *---------------------------------------------------------*/

  esp_err_t enp_neighbor_remove(
          enp_neighbor_table_t *table,
          const enp_address_t *address);

  /*----------------------------------------------------------
   * Expiration
   *---------------------------------------------------------*/

  size_t enp_neighbor_expire(
          enp_neighbor_table_t *table,
          uint32_t now_ms,
          uint32_t timeout_ms);

  /*----------------------------------------------------------
   * Information
   *---------------------------------------------------------*/

  size_t enp_neighbor_count(
          const enp_neighbor_table_t *table);

  #ifdef __cplusplus
  }
  #endif

  #endif /* ENP_NEIGHBOR_H */