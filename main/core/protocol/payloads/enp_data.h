/*
 * enp_data.h
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 */

 #ifndef ENP_DATA_H
 #define ENP_DATA_H

 /**
  * ENP v0.2 — DATA Plane Wire Definition
  *
  * E3.3.1
  *
  * Hardware-independent DATA payload definition.
  *
  * Design:
  *   - Source and destination remain in the ENP common header.
  *   - ENP packet sequence remains the network packet identity.
  *   - application_sequence identifies the application message.
  *   - No transport dependency.
  *   - No FreeRTOS dependency.
  *   - No dynamic allocation.
  *
  * ESP-IDF 5.5 compatible.
  */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 /* --------------------------------------------------------------------------
  * Protocol version
  * -------------------------------------------------------------------------- */

 #define ENP_DATA_PAYLOAD_VERSION        0x01U

 /* --------------------------------------------------------------------------
  * DATA subtype
  * -------------------------------------------------------------------------- */

 typedef enum
 {
     ENP_DATA_SUBTYPE_APPLICATION = 1

 } enp_data_subtype_t;

 /* --------------------------------------------------------------------------
  * DATA flags
  * -------------------------------------------------------------------------- */

 #define ENP_DATA_FLAG_NONE              0x0000U

 /*
  * Reserved for future fragmentation/reassembly.
  */
 #define ENP_DATA_FLAG_FRAGMENTED        0x0001U

 /*
  * Reserved for future QoS/priority handling.
  */
 #define ENP_DATA_FLAG_PRIORITY          0x0002U

 #define ENP_DATA_KNOWN_FLAGS \
     (ENP_DATA_FLAG_FRAGMENTED | ENP_DATA_FLAG_PRIORITY)

 /* --------------------------------------------------------------------------
  * DATA payload header
  *
  * Wire format:
  *
  * Offset  Size  Field
  * ------  ----  -------------------------
  *   0      1    payload_version
  *   1      1    subtype
  *   2      2    flags
  *   4      4    application_sequence
  *   8      2    payload_length
  *  10      2    reserved
  *
  * Total = 12 bytes
  * -------------------------------------------------------------------------- */

 typedef struct __attribute__((packed))
 {
     uint8_t  payload_version;
     uint8_t  subtype;

     uint16_t flags;

     uint32_t application_sequence;

     uint16_t payload_length;

     uint16_t reserved;

 } enp_data_header_t;

 #define ENP_DATA_HEADER_SIZE \
     ((size_t)sizeof(enp_data_header_t))

 /* --------------------------------------------------------------------------
  * API
  * -------------------------------------------------------------------------- */

 void enp_data_header_init(
     enp_data_header_t *header,
     enp_data_subtype_t subtype,
     uint16_t flags,
     uint32_t application_sequence,
     uint16_t payload_length);

 bool enp_data_header_valid(
     const enp_data_header_t *header);

 bool enp_data_payload_length_valid(
     const enp_data_header_t *header,
     size_t available_payload_length);

 #endif /* ENP_DATA_H */