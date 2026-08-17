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

 #include "core/data/enp_data_plane.h"

 #include <string.h>

 #include "core/protocol/enp_protocol.h"
 #include "core/protocol/payloads/enp_ack.h"

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

 
static void enp_data_plane_expire_ack_cache(
        enp_data_plane_t *plane,
        uint32_t now_ms)
{
    for (size_t index = 0U; index < ENP_DATA_PLANE_ACK_CACHE_SIZE; ++index)
    {
        enp_data_plane_ack_cache_entry_t *entry = &plane->ack_cache[index];
        if (entry->valid &&
            (uint32_t)(now_ms - entry->stored_at_ms) >=
                    ENP_DATA_PLANE_ACK_CACHE_TIMEOUT_MS)
        {
            entry->valid = false;
        }
    }
}

static size_t enp_data_plane_find_cached_ack(
        enp_data_plane_t *plane,
        const enp_address_t *data_origin,
        enp_sequence_t data_sequence,
        uint32_t now_ms)
{
    if ((plane == NULL) || (data_origin == NULL))
    {
        return ENP_DATA_PLANE_ACK_CACHE_SIZE;
    }

    enp_data_plane_expire_ack_cache(plane, now_ms);

    for (size_t index = 0U; index < ENP_DATA_PLANE_ACK_CACHE_SIZE; ++index)
    {
        const enp_data_plane_ack_cache_entry_t *entry =
                &plane->ack_cache[index];

        if (entry->valid &&
            entry->data_sequence == data_sequence &&
            enp_address_equal(&entry->data_origin, data_origin))
        {
            return index;
        }
    }

    return ENP_DATA_PLANE_ACK_CACHE_SIZE;
}

static size_t enp_data_plane_find_ack_slot(
        enp_data_plane_t *plane,
        uint32_t now_ms)
{
    size_t oldest_index = 0U;
    uint32_t oldest_age = 0U;

    enp_data_plane_expire_ack_cache(plane, now_ms);

    for (size_t index = 0U; index < ENP_DATA_PLANE_ACK_CACHE_SIZE; ++index)
    {
        if (!plane->ack_cache[index].valid)
        {
            return index;
        }

        const uint32_t age =
                (uint32_t)(now_ms - plane->ack_cache[index].stored_at_ms);

        if ((index == 0U) || (age > oldest_age))
        {
            oldest_index = index;
            oldest_age = age;
        }
    }

    return oldest_index;
}

static esp_err_t enp_data_plane_cache_ack(
        enp_data_plane_t *plane,
        const enp_packet_t *packet,
        uint32_t now_ms)
{
    const enp_header_t *header =
            enp_packet_header_const(packet);
    const enp_ack_payload_t *ack =
            (const enp_ack_payload_t *)enp_packet_payload_const(packet);

    if ((plane == NULL) || (packet == NULL) ||
        (header == NULL) || (ack == NULL) ||
        !enp_ack_payload_valid(ack))
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t index = enp_data_plane_find_cached_ack(
            plane,
            &header->destination,
            ack->data_packet_sequence,
            now_ms);

    if (index >= ENP_DATA_PLANE_ACK_CACHE_SIZE)
    {
        index = enp_data_plane_find_ack_slot(plane, now_ms);
    }

    enp_data_plane_ack_cache_entry_t *entry = &plane->ack_cache[index];
    entry->valid = true;
    entry->data_origin = header->destination;
    entry->data_sequence = ack->data_packet_sequence;
    entry->stored_at_ms = now_ms;
    entry->ack_packet = *packet;

    return ESP_OK;
}

static const enp_packet_t *enp_data_plane_cached_ack_for_data(
        enp_data_plane_t *plane,
        const enp_packet_t *data_packet,
        uint32_t now_ms)
{
    const enp_header_t *header =
            enp_packet_header_const(data_packet);

    if ((header == NULL) ||
        ((header->flags & ENP_FLAG_ACK_REQUIRED) == 0U))
    {
        return NULL;
    }

    const size_t index = enp_data_plane_find_cached_ack(
            plane,
            &header->source,
            header->sequence,
            now_ms);

    if (index >= ENP_DATA_PLANE_ACK_CACHE_SIZE)
    {
        return NULL;
    }

    return &plane->ack_cache[index].ack_packet;
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

     memset(plane->ack_cache, 0, sizeof(plane->ack_cache));

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
            if (header->type == (uint8_t)ENP_PACKET_APPLICATION)
            {
                const enp_packet_t *cached_ack =
                        enp_data_plane_cached_ack_for_data(
                                plane,
                                packet,
                                enp_context_time_ms(plane->context));

                if (cached_ack != NULL)
                {
                    return enp_routing_data_path_forward(
                            plane->routing_path,
                            cached_ack);
                }
            }

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

     if (header->type == (uint8_t)ENP_PACKET_ACK)
     {
         const esp_err_t cache_err =
                 enp_data_plane_cache_ack(
                         plane,
                         packet,
                         enp_context_time_ms(plane->context));

         if (cache_err != ESP_OK)
         {
             return cache_err;
         }
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





