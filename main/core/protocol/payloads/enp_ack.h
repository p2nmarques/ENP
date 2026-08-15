/*
 * enp_ack.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 *
 *
 * ENP v0.2 — DATA acknowledgement payload.
 *
 * E3.3.3 integration wire format.
 * Transport-independent and ESP-IDF 5.5 compatible.
 *
 * The ENP packet header carries:
 *   source      = ACK generator (C)
 *   destination = original DATA originator (A)
 *   sequence    = ACK packet sequence
 *   ttl         = forwarding lifetime
 *
 * The ACK payload carries the DATA identity being acknowledged.
 */

 #ifndef ENP_ACK_H
 #define ENP_ACK_H

  #include <stdbool.h>
 #include <stdint.h>

 #define ENP_ACK_PAYLOAD_VERSION  1U
 #define ENP_ACK_SUBTYPE_DATA     1U
 #define ENP_ACK_STATUS_OK        0U
 #define ENP_ACK_WIRE_SIZE        12U

 typedef struct __attribute__((packed)) {
     uint8_t  payload_version;
     uint8_t  subtype;
     uint8_t  status;
     uint8_t  reserved;
     uint32_t data_packet_sequence;
     uint32_t application_sequence;
 } enp_ack_payload_t;

 typedef char enp_ack_wire_size_must_be_12[
         (sizeof(enp_ack_payload_t) == ENP_ACK_WIRE_SIZE) ? 1 : -1];

 static inline void enp_ack_payload_init(
         enp_ack_payload_t *ack,
         uint32_t data_packet_sequence,
         uint32_t application_sequence)
 {
     if (ack == NULL) {
         return;
     }

     ack->payload_version = ENP_ACK_PAYLOAD_VERSION;
     ack->subtype = ENP_ACK_SUBTYPE_DATA;
     ack->status = ENP_ACK_STATUS_OK;
     ack->reserved = 0U;
     ack->data_packet_sequence = data_packet_sequence;
     ack->application_sequence = application_sequence;
 }

 static inline bool enp_ack_payload_valid(
         const enp_ack_payload_t *ack)
 {
     return ack != NULL &&
            ack->payload_version == ENP_ACK_PAYLOAD_VERSION &&
            ack->subtype == ENP_ACK_SUBTYPE_DATA &&
            ack->status == ENP_ACK_STATUS_OK &&
            ack->reserved == 0U &&
            ack->data_packet_sequence != 0U &&
            ack->application_sequence != 0U;
 }

 #endif /* ENP_ACK_H */