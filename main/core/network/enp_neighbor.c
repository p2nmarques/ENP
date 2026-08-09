/*
 * enp_neighborn.c
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_neighbor.c
  *
  * @brief ENP neighbor table implementation.
  */

 #include "../network/enp_neighbor.h"

#include <string.h>

 /*----------------------------------------------------------
  * Private Helpers
  *---------------------------------------------------------*/

 static size_t enp_neighbor_find_index(
         const enp_neighbor_table_t *table,
         const enp_address_t *address)
 {
     if ((table == NULL) ||
         (address == NULL))
     {
         return ENP_MAX_NEIGHBORS;
     }

     for (size_t index = 0U;
          index < ENP_MAX_NEIGHBORS;
          ++index)
     {
         const enp_neighbor_t *neighbor =
                 &table->entries[index];

         if (neighbor->state == ENP_NEIGHBOR_STATE_EMPTY)
         {
             continue;
         }

         if (enp_address_equal(
                     &neighbor->address,
                     address))
         {
             return index;
         }
     }

     return ENP_MAX_NEIGHBORS;
 }

 static size_t enp_neighbor_find_free_index(
         const enp_neighbor_table_t *table)
 {
     if (table == NULL)
     {
         return ENP_MAX_NEIGHBORS;
     }

     for (size_t index = 0U;
          index < ENP_MAX_NEIGHBORS;
          ++index)
     {
         if (table->entries[index].state ==
             ENP_NEIGHBOR_STATE_EMPTY)
         {
             return index;
         }
     }

     return ENP_MAX_NEIGHBORS;
 }

 /*----------------------------------------------------------
  * Lifecycle
  *---------------------------------------------------------*/

 esp_err_t enp_neighbor_table_init(
         enp_neighbor_table_t *table)
 {
     if (table == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     memset(
             table,
             0,
             sizeof(*table));

     return ESP_OK;
 }

 esp_err_t enp_neighbor_table_clear(
         enp_neighbor_table_t *table)
 {
     if (table == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     memset(
             table,
             0,
             sizeof(*table));

     return ESP_OK;
 }

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
          uint32_t now_ms)
  {
      if ((table == NULL) ||
          (address == NULL) ||
          (transport_address == NULL))
      {
          return ESP_ERR_INVALID_ARG;
      }

      if (enp_address_is_broadcast(address))
      {
          return ESP_ERR_INVALID_ARG;
      }

      size_t index =
              enp_neighbor_find_index(
                      table,
                      address);

      if (index < ENP_MAX_NEIGHBORS)
      {
          enp_neighbor_t *neighbor =
                  &table->entries[index];

          neighbor->transport_address =
                  *transport_address;

          neighbor->role =
                  role;

          neighbor->capabilities =
                  capabilities;

          neighbor->last_sequence =
                  sequence;

          neighbor->rssi =
                  rssi;

          neighbor->last_seen_ms =
                  now_ms;

          neighbor->state =
                  ENP_NEIGHBOR_STATE_ACTIVE;

          return ESP_OK;
      }

      index =
              enp_neighbor_find_free_index(
                      table);

      if (index >= ENP_MAX_NEIGHBORS)
      {
          return ESP_ERR_NO_MEM;
      }

      enp_neighbor_t *neighbor =
              &table->entries[index];

      memset(
              neighbor,
              0,
              sizeof(*neighbor));

      neighbor->address =
              *address;

      neighbor->transport_address =
              *transport_address;

      neighbor->role =
              role;

      neighbor->capabilities =
              capabilities;

      neighbor->last_sequence =
              sequence;

      neighbor->rssi =
              rssi;

      neighbor->last_seen_ms =
              now_ms;

      neighbor->state =
              ENP_NEIGHBOR_STATE_ACTIVE;

      table->count++;

      return ESP_OK;
  }

 /*----------------------------------------------------------
  * Lookup
  *---------------------------------------------------------*/

 enp_neighbor_t *enp_neighbor_find(
         enp_neighbor_table_t *table,
         const enp_address_t *address)
 {
     if ((table == NULL) ||
         (address == NULL))
     {
         return NULL;
     }

     const size_t index =
             enp_neighbor_find_index(
                     table,
                     address);

     if (index >= ENP_MAX_NEIGHBORS)
     {
         return NULL;
     }

     return &table->entries[index];
 }

 const enp_neighbor_t *enp_neighbor_find_const(
         const enp_neighbor_table_t *table,
         const enp_address_t *address)
 {
     if ((table == NULL) ||
         (address == NULL))
     {
         return NULL;
     }

     const size_t index =
             enp_neighbor_find_index(
                     table,
                     address);

     if (index >= ENP_MAX_NEIGHBORS)
     {
         return NULL;
     }

     return &table->entries[index];
 }

 /*----------------------------------------------------------
  * Removal
  *---------------------------------------------------------*/

 esp_err_t enp_neighbor_remove(
         enp_neighbor_table_t *table,
         const enp_address_t *address)
 {
     if ((table == NULL) ||
         (address == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     const size_t index =
             enp_neighbor_find_index(
                     table,
                     address);

     if (index >= ENP_MAX_NEIGHBORS)
     {
         return ESP_ERR_NOT_FOUND;
     }

     memset(
             &table->entries[index],
             0,
             sizeof(table->entries[index]));

     if (table->count > 0U)
     {
         table->count--;
     }

     return ESP_OK;
 }

 /*----------------------------------------------------------
  * Expiration
  *---------------------------------------------------------*/

 size_t enp_neighbor_expire(
         enp_neighbor_table_t *table,
         uint32_t now_ms,
         uint32_t timeout_ms)
 {
     if (table == NULL)
     {
         return 0U;
     }

     size_t expired = 0U;

     for (size_t index = 0U;
          index < ENP_MAX_NEIGHBORS;
          ++index)
     {
         enp_neighbor_t *neighbor =
                 &table->entries[index];

         if (neighbor->state !=
             ENP_NEIGHBOR_STATE_ACTIVE)
         {
             continue;
         }

         /*
          * Unsigned subtraction intentionally handles
          * uint32_t millisecond counter wraparound.
          */
         const uint32_t elapsed =
                 now_ms -
                 neighbor->last_seen_ms;

         if (elapsed >= timeout_ms)
         {
             neighbor->state =
                     ENP_NEIGHBOR_STATE_STALE;

             expired++;
         }
     }

     return expired;
 }

 /*----------------------------------------------------------
  * Information
  *---------------------------------------------------------*/

 size_t enp_neighbor_count(
         const enp_neighbor_table_t *table)
 {
     if (table == NULL)
     {
         return 0U;
     }

     return table->count;
 }


