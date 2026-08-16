/*
 * enp_data_plane.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_data_plane.c
  *
  * @brief ENP receive data-plane implementation.
  */

 #include "../data/enp_data_plane.h"

#include <string.h>

 #include "core/protocol/enp_protocol.h"

 static bool enp_data_plane_is_local(
         const enp_data_plane_t *plane,
         const enp_address_t *destination)
 {
     if ((plane == NULL) ||
         (plane->context == NULL) ||
         (destination == NULL))
     {
         return false;
     }

     return (destination->network == plane->context->network.id) &&
            (destination->node == plane->context->network.local.id);
 }

 static enp_duplicate_cache_t *enp_data_plane_duplicate_cache(
         enp_data_plane_t *plane,
         enp_packet_type_t type)
 {
     if (plane == NULL)
     {
         return NULL;
     }

     if (type == ENP_PACKET_APPLICATION)
     {
         return &plane->data_duplicates;
     }

     if (type == ENP_PACKET_ACK)
     {
         return &plane->ack_duplicates;
     }

     return NULL;
 }

 esp_err_t enp_data_plane_init(
         enp_data_plane_t *plane,
         enp_context_t *context,
         enp_routing_data_path_t *routing_path,
         enp_data_plane_local_process_fn local_process)
 {
     if ((plane == NULL) ||
         (context == NULL) ||
         (routing_path == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     memset(plane, 0, sizeof(*plane));

     esp_err_t err = enp_duplicate_cache_init(
             &plane->data_duplicates);
     if (err != ESP_OK)
     {
         return err;
     }

     err = enp_duplicate_cache_init(
             &plane->ack_duplicates);
     if (err != ESP_OK)
     {
         (void)enp_duplicate_cache_clear(&plane->data_duplicates);
         return err;
     }

     plane->context = context;
     plane->routing_path = routing_path;
     plane->local_process = local_process;

     return ESP_OK;
 }

 esp_err_t enp_data_plane_deinit(
         enp_data_plane_t *plane)
 {
     if (plane == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     esp_err_t data_err = enp_duplicate_cache_clear(
             &plane->data_duplicates);
     esp_err_t ack_err = enp_duplicate_cache_clear(
             &plane->ack_duplicates);

     if (data_err != ESP_OK)
     {
         return data_err;
     }

     if (ack_err != ESP_OK)
     {
         return ack_err;
     }

     plane->context = NULL;
     plane->routing_path = NULL;
     plane->local_process = NULL;

     return ESP_OK;
 }

 esp_err_t enp_data_plane_process(
         enp_data_plane_t *plane,
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     if ((plane == NULL) ||
         (plane->context == NULL) ||
         (plane->routing_path == NULL) ||
         (packet == NULL) ||
         (source == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (!enp_packet_verify(packet))
     {
         return ESP_ERR_INVALID_ARG;
     }

     const enp_header_t *header = enp_packet_header_const(packet);
     if (header == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     enp_duplicate_cache_t *duplicate_cache =
             enp_data_plane_duplicate_cache(
                     plane,
                     (enp_packet_type_t)header->type);

     if (duplicate_cache != NULL)
     {
         bool duplicate = false;

         const esp_err_t duplicate_err =
                 enp_duplicate_check_and_record(
                         duplicate_cache,
                         &header->source,
                         header->sequence,
                         enp_context_time_ms(plane->context),
                         &duplicate);

         if (duplicate_err != ESP_OK)
         {
             return duplicate_err;
         }

         if (duplicate)
         {
             return ESP_OK;
         }
     }

     if (enp_data_plane_is_local(
                 plane,
                 &header->destination))
     {
         if (plane->local_process == NULL)
         {
             return ESP_ERR_NOT_FOUND;
         }

         return plane->local_process(
                 plane->context,
                 packet,
                 source);
     }

     if ((header->type != (uint8_t)ENP_PACKET_APPLICATION) &&
         (header->type != (uint8_t)ENP_PACKET_ACK))
     {
         return ESP_ERR_NOT_SUPPORTED;
     }

     return enp_routing_data_path_forward(
             plane->routing_path,
             packet);
 }





