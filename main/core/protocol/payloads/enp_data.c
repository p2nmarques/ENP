/*
 * enp_data.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 *
 */
 
 #include "enp_data.h"

 #include <string.h>

 void enp_data_header_init(
     enp_data_header_t *header,
     enp_data_subtype_t subtype,
     uint16_t flags,
     uint32_t application_sequence,
     uint16_t payload_length)
 {
     if (header == NULL)
     {
         return;
     }

     memset(
         header,
         0,
         sizeof(*header));

     header->payload_version =
         ENP_DATA_PAYLOAD_VERSION;

     header->subtype =
         (uint8_t)subtype;

     header->flags =
         flags;

     header->application_sequence =
         application_sequence;

     header->payload_length =
         payload_length;

     header->reserved = 0U;
 }

 bool enp_data_header_valid(
     const enp_data_header_t *header)
 {
     if (header == NULL)
     {
         return false;
     }

     /*
      * Version validation.
      */
     if (header->payload_version !=
         ENP_DATA_PAYLOAD_VERSION)
     {
         return false;
     }

     /*
      * At E3.3.1 we only support application DATA.
      */
     if (header->subtype !=
         ENP_DATA_SUBTYPE_APPLICATION)
     {
         return false;
     }

     /*
      * Reject unknown flags.
      */
     if ((header->flags &
          (uint16_t)~ENP_DATA_KNOWN_FLAGS) != 0U)
     {
         return false;
     }

     /*
      * Reserved field must currently be zero.
      */
     if (header->reserved != 0U)
     {
         return false;
     }

     /*
      * Zero is reserved as an invalid application
      * sequence number.
      */
     if (header->application_sequence == 0U)
     {
         return false;
     }

     return true;
 }

 bool enp_data_payload_length_valid(
     const enp_data_header_t *header,
     size_t available_payload_length)
 {
     if (!enp_data_header_valid(header))
     {
         return false;
     }

     return
         (size_t)header->payload_length ==
         available_payload_length;
 }




