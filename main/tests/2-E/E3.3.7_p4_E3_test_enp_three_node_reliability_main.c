/*
 * E3.3.7_p4_E3_test_enp_three_node_reliability_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / E3 — three-node end-to-end reliability integration.
 *
 * Topology:
 *
 *     A (Node 1) ---- B (Node 2) ---- C (Node 3)
 *       Gateway          Relay            Sensor
 *
 * DATA:
 *     A -> B -> C
 * ACK:
 *     C -> B -> A
 *
 * The first ACK is deliberately dropped at B. A's generic reliability
 * transaction must time out and retransmit the same DATA identity. B must
 * suppress the duplicate DATA and forward the cached ACK. C must deliver the
 * application DATA exactly once. A must complete the same reliability
 * transaction as DELIVERED.
 *
 * Route discovery is intentionally not part of E3C. The route table is
 * populated with the already validated A->B->C topology so this test isolates
 * Reliability -> Routing -> multi-hop forwarding -> ACK recovery.
 *
 * Target: ESP-IDF 6.0.2
 */

  #include <stdbool.h>
  #include <stddef.h>
  #include <stdint.h>
  #include <inttypes.h>
  #include <string.h>

  #include "esp_err.h"
  #include "esp_log.h"
  #include "esp_netif.h"
  #include "esp_wifi.h"
  #include "nvs_flash.h"

  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/queue.h"

  #include "config/enp_config.h"
  #include "core/enp_context.h"
  #include "core/enp_maintenance.h"
  #include "core/data/enp_data_plane.h"
  #include "core/enp_transport.h"
  #include "core/dispatcher/enp_dispatcher.h"
  #include "core/protocol/enp_packet.h"
  #include "core/protocol/payloads/enp_ack.h"
  #include "core/protocol/payloads/enp_data.h"
  #include "core/reliability/enp_reliability.h"
  #include "core/reliability/enp_reliability_service.h"
  #include "core/routing/enp_route_table.h"
  #include "core/routing/enp_routing_data_path.h"
  #include "core/network/enp_neighbor.h"
  #include "link/enp_transport_espnow.h"
  #include "link/enp_transport_wifi.h"

  static const char *TAG = "E3_3_7_P4_E3";

  #define E3_NETWORK_ID          ((enp_network_id_t)1U)
  #define E3_NODE_A             ((enp_node_id_t)1U)
  #define E3_NODE_B             ((enp_node_id_t)2U)
  #define E3_NODE_C             ((enp_node_id_t)3U)

  #define E3_DATA_SEQUENCE      ((enp_sequence_t)0xE3C1U)
  #define E3_ACK_SEQUENCE       ((enp_sequence_t)0xE3C2U)
  #define E3_APP_SEQUENCE       ((uint32_t)0x000000C1U)
  #define E3_DATA_TTL           ((uint8_t)8U)
  #define E3_APP_PAYLOAD_LEN    4U
  static const uint8_t E3_APP_PAYLOAD[E3_APP_PAYLOAD_LEN] = {
      0xE3U, 0xC3U, 0x00U, 0x01U
  };
  #define E3_TEST_TIMEOUT_MS    6000U
  #define E3_POLL_MS            20U

  /* Known ESP-NOW STA addresses from the validated E3 topology. */
  #define E3_MAC_A_0 0x94U
  #define E3_MAC_A_1 0xE6U
  #define E3_MAC_A_2 0x86U
  #define E3_MAC_A_3 0x0DU
  #define E3_MAC_A_4 0x11U
  #define E3_MAC_A_5 0x8CU

  #define E3_MAC_B_0 0x78U
  #define E3_MAC_B_1 0x21U
  #define E3_MAC_B_2 0x84U
  #define E3_MAC_B_3 0xE6U
  #define E3_MAC_B_4 0x19U
  #define E3_MAC_B_5 0x84U

  #define E3_MAC_C_0 0x94U
  #define E3_MAC_C_1 0xE6U
  #define E3_MAC_C_2 0x86U
  #define E3_MAC_C_3 0x0EU
  #define E3_MAC_C_4 0x2BU
  #define E3_MAC_C_5 0x60U

  static enp_context_t s_context;
  static enp_route_table_t s_routes;
  static enp_routing_data_path_t s_routing_path;
  static enp_data_plane_t s_data_plane;
 static enp_transport_t *s_underlying_transport;
 static enp_transport_t s_observed_transport;

  static enp_packet_t s_tx_data;
  static enp_packet_t s_local_ack;
  typedef struct {
      enp_packet_t packet;
      enp_transport_address_t source;
  } e3c_rx_item_t;

  #define E3C_RX_QUEUE_LENGTH 4U
  static StaticQueue_t s_rx_queue_buffer;
  static uint8_t s_rx_queue_storage[
          E3C_RX_QUEUE_LENGTH * sizeof(e3c_rx_item_t)];
  static QueueHandle_t s_rx_queue;
  static e3c_rx_item_t s_rx_item;

  static enp_reliability_handle_t s_reliability_handle =
          ENP_RELIABILITY_INVALID_HANDLE;
  static bool s_reliability_delivered;
  static bool s_reliability_failed;
  static uint32_t s_reliability_submit_count;

  static uint32_t s_a_ack_drop_count;
  static uint32_t s_a_ack_dispatch_count;
  static uint32_t s_c_delivery_count;
  static uint32_t s_c_ack_sent_count;

 /* B-side data-plane instrumentation. These counters are test observability only. */
 static uint32_t s_b_data_received_count;
 static uint32_t s_b_data_forwarded_count;
 static uint32_t s_b_ack_received_count;
 static uint32_t s_b_ack_forwarded_count;
 static uint32_t s_b_duplicate_data_count;
 static uint32_t s_b_cached_ack_recovery_count;
 static bool s_b_expect_cached_ack_forward;

  static enp_node_id_t s_node_id;
  static uint32_t s_ack_sequence = E3_ACK_SEQUENCE;

  static enp_address_t make_address(enp_node_id_t node_id)
  {
      return (enp_address_t){
          .network = E3_NETWORK_ID,
          .node = node_id,
      };
  }

  static enp_transport_address_t make_transport_address(
          uint8_t b0,
          uint8_t b1,
          uint8_t b2,
          uint8_t b3,
          uint8_t b4,
          uint8_t b5)
  {
      return (enp_transport_address_t){
          .value = {b0, b1, b2, b3, b4, b5},
          .length = 6U,
      };
  }

  static enp_transport_address_t mac_for_node(enp_node_id_t node_id)
  {
      switch (node_id)
      {
          case E3_NODE_A:
              return make_transport_address(
                      E3_MAC_A_0, E3_MAC_A_1, E3_MAC_A_2,
                      E3_MAC_A_3, E3_MAC_A_4, E3_MAC_A_5);
          case E3_NODE_B:
              return make_transport_address(
                      E3_MAC_B_0, E3_MAC_B_1, E3_MAC_B_2,
                      E3_MAC_B_3, E3_MAC_B_4, E3_MAC_B_5);
          case E3_NODE_C:
              return make_transport_address(
                      E3_MAC_C_0, E3_MAC_C_1, E3_MAC_C_2,
                      E3_MAC_C_3, E3_MAC_C_4, E3_MAC_C_5);
          default:
              return (enp_transport_address_t){0};
      }
  }

  static enp_config_t make_config(enp_node_id_t node_id)
  {
      enp_role_t role = ENP_ROLE_SENSOR;

      if (node_id == E3_NODE_A)
      {
          role = ENP_ROLE_GATEWAY;
      }
      else if (node_id == E3_NODE_B)
      {
          role = ENP_ROLE_RELAY;
      }

      return (enp_config_t){
          .network_id = E3_NETWORK_ID,
          .node_id = node_id,
          .role = role,
      };
  }

  static bool make_hop_metric(uint16_t hops, enp_route_metric_t *metric)
  {
      if (metric == NULL || !enp_route_metric_init(
              metric, ENP_ROUTE_METRIC_HOP_COUNT))
      {
          return false;
      }

      metric->value = hops;
      metric->valid = true;
      return true;
  }

  static bool install_route(
          enp_node_id_t destination,
          enp_node_id_t next_hop,
          uint16_t hops)
  {
      enp_route_entry_t entry = {0};

      entry.destination = (enp_route_destination_t){
          .network_id = E3_NETWORK_ID,
          .node_id = destination,
      };
      entry.next_hop = (enp_route_destination_t){
          .network_id = E3_NETWORK_ID,
          .node_id = next_hop,
      };
      entry.route_sequence = 1U;
      entry.expires_at_ms = UINT32_MAX;
      entry.state = ENP_ROUTE_STATE_ACTIVE;

      if (!make_hop_metric(hops, &entry.metric))
      {
          return false;
      }

      const enp_route_entry_t *existing =
              enp_route_table_lookup_const(&s_routes, entry.destination);

      if (existing != NULL)
      {
          return enp_route_table_update(&s_routes, &entry);
      }

      return enp_route_table_insert(&s_routes, &entry);
  }

  static bool install_local_topology(enp_node_id_t local_node)
  {
      if (!enp_route_table_init(&s_routes))
      {
          return false;
      }

      switch (local_node)
      {
          case E3_NODE_A:
              /* A -> C through B. */
              return install_route(E3_NODE_C, E3_NODE_B, 2U);

          case E3_NODE_B:
              /* B -> C and B -> A are both one-hop. */
              return install_route(E3_NODE_C, E3_NODE_C, 1U) &&
                     install_route(E3_NODE_A, E3_NODE_A, 1U);

          case E3_NODE_C:
              /* C -> A through B. */
              return install_route(E3_NODE_A, E3_NODE_B, 2U);

          default:
              return false;
      }
  }

  static bool install_local_neighbors(enp_node_id_t local_node)
  {
      const uint32_t now_ms = enp_context_time_ms(&s_context);

      if (local_node == E3_NODE_A)
      {
          const enp_address_t b = make_address(E3_NODE_B);
          const enp_transport_address_t mac = mac_for_node(E3_NODE_B);
          return enp_neighbor_update(
                         &s_context.neighbors,
                         &b,
                         &mac,
                         ENP_ROLE_RELAY,
                         0U,
                         1U,
                         0,
                         now_ms) == ESP_OK;
      }

      if (local_node == E3_NODE_B)
      {
          const enp_address_t a = make_address(E3_NODE_A);
          const enp_address_t c = make_address(E3_NODE_C);
          const enp_transport_address_t mac_a = mac_for_node(E3_NODE_A);
          const enp_transport_address_t mac_c = mac_for_node(E3_NODE_C);

          return enp_neighbor_update(
                         &s_context.neighbors,
                         &a,
                         &mac_a,
                         ENP_ROLE_GATEWAY,
                         0U,
                         1U,
                         0,
                         now_ms) == ESP_OK &&
                 enp_neighbor_update(
                         &s_context.neighbors,
                         &c,
                         &mac_c,
                         ENP_ROLE_SENSOR,
                         0U,
                         1U,
                         0,
                         now_ms) == ESP_OK;
      }

      if (local_node == E3_NODE_C)
      {
          const enp_address_t b = make_address(E3_NODE_B);
          const enp_transport_address_t mac = mac_for_node(E3_NODE_B);
          return enp_neighbor_update(
                         &s_context.neighbors,
                         &b,
                         &mac,
                         ENP_ROLE_RELAY,
                         0U,
                         1U,
                         0,
                         now_ms) == ESP_OK;
      }

      return false;
  }

  static esp_err_t observed_transport_send(
         const enp_transport_address_t *destination,
         const void *data,
         size_t length)
 {
     if (s_underlying_transport == NULL ||
         s_underlying_transport->send == NULL ||
         destination == NULL || data == NULL || length == 0U)
     {
         return ESP_ERR_INVALID_ARG;
     }

     enp_packet_t packet;
     memset(&packet, 0, sizeof(packet));
     if (length > sizeof(packet))
     {
         return ESP_ERR_INVALID_SIZE;
     }

     memcpy(enp_packet_data(&packet), data, length);

     if (s_node_id == E3_NODE_B && enp_packet_verify(&packet))
     {
         const enp_header_t *header = enp_packet_header_const(&packet);
         if (header != NULL)
         {
             if (header->type == (uint8_t)ENP_PACKET_APPLICATION)
             {
                 ++s_b_data_forwarded_count;
             }
             else if (header->type == (uint8_t)ENP_PACKET_ACK)
             {
                 ++s_b_ack_forwarded_count;
                 if (s_b_expect_cached_ack_forward)
                 {
                     ++s_b_cached_ack_recovery_count;
                     s_b_expect_cached_ack_forward = false;
                     ESP_LOGI(TAG,
                              "PASS: B forwarded cached ACK after duplicate DATA");
                 }
             }
         }
     }

     return s_underlying_transport->send(destination, data, length);
 }

 static bool resolve_transport(
          void *context,
          enp_route_destination_t next_hop,
          enp_transport_address_t *transport_address)
  {
      if (context == NULL || transport_address == NULL)
      {
          return false;
      }

      enp_context_t *ctx = (enp_context_t *)context;
      const enp_address_t logical = {
          .network = next_hop.network_id,
          .node = next_hop.node_id,
      };

      return enp_neighbor_get_transport_address(
                     &ctx->neighbors,
                     &logical,
                     transport_address) == ESP_OK;
  }

  static esp_err_t submit_to_routing(
          const enp_packet_t *packet,
          void *user_context)
  {
      const esp_err_t err = enp_routing_data_path_submit(
              (enp_routing_data_path_t *)user_context,
              packet);

      if (err == ESP_OK)
      {
          ++s_reliability_submit_count;
      }

      return err;
  }

  static void reliability_result(
          enp_reliability_handle_t handle,
          enp_reliability_result_t result,
          void *user_context)
  {
      (void)handle;
      (void)user_context;

      if (result == ENP_RELIABILITY_RESULT_DELIVERED)
      {
          s_reliability_delivered = true;
      }
      else if (result == ENP_RELIABILITY_RESULT_FAILED)
      {
          s_reliability_failed = true;
      }
  }

  static bool make_reliable_data(void)
  {
      const enp_address_t source = make_address(E3_NODE_A);

      enp_packet_init(
              &s_tx_data,
              ENP_PACKET_APPLICATION,
              &source);

      enp_header_t *header = enp_packet_header(&s_tx_data);
      if (header == NULL)
      {
          return false;
      }

      header->destination = make_address(E3_NODE_C);
      header->flags = ENP_FLAG_ACK_REQUIRED;
      header->ttl = E3_DATA_TTL;
      header->sequence = E3_DATA_SEQUENCE;

      enp_data_header_t *data_header =
              (enp_data_header_t *)enp_packet_payload(&s_tx_data);

      if (data_header == NULL)
      {
          return false;
      }

      enp_data_header_init(
              data_header,
              ENP_DATA_SUBTYPE_APPLICATION,
              ENP_DATA_FLAG_NONE,
              E3_APP_SEQUENCE,
              E3_APP_PAYLOAD_LEN);

      memcpy(
              ((uint8_t *)data_header) + ENP_DATA_HEADER_SIZE,
              E3_APP_PAYLOAD,
              sizeof(E3_APP_PAYLOAD));

      return enp_packet_seal(
                     &s_tx_data,
                     (uint16_t)(ENP_DATA_HEADER_SIZE +
                                sizeof(E3_APP_PAYLOAD))) == ESP_OK;
  }

  static bool make_ack_from_data(const enp_packet_t *data_packet)
  {
      if (data_packet == NULL)
      {
          return false;
      }

      const enp_header_t *data_header =
              enp_packet_header_const(data_packet);

      const enp_data_header_t *payload =
              (const enp_data_header_t *)enp_packet_payload_const(data_packet);

      if (data_header == NULL || payload == NULL)
      {
          return false;
      }

      const enp_address_t source = make_address(E3_NODE_C);

      enp_packet_init(
              &s_local_ack,
              ENP_PACKET_ACK,
              &source);

      enp_header_t *ack_header = enp_packet_header(&s_local_ack);
      if (ack_header == NULL)
      {
          return false;
      }

      ack_header->destination = data_header->source;
      ack_header->flags = ENP_FLAG_NONE;
      ack_header->ttl = E3_DATA_TTL;
      ack_header->sequence = s_ack_sequence++;

      enp_ack_payload_t *ack_payload =
              (enp_ack_payload_t *)enp_packet_payload(&s_local_ack);

      if (ack_payload == NULL)
      {
          return false;
      }

      enp_ack_payload_init(
              ack_payload,
              data_header->sequence,
              payload->application_sequence);

      return enp_packet_seal(
                     &s_local_ack,
                     ENP_ACK_WIRE_SIZE) == ESP_OK;
  }

  static bool packet_is_expected_data(const enp_packet_t *packet)
  {
      if (packet == NULL || !enp_packet_verify(packet))
      {
          return false;
      }

      const enp_header_t *header = enp_packet_header_const(packet);
      const enp_address_t expected_source = make_address(E3_NODE_A);
      const enp_address_t expected_destination = make_address(E3_NODE_C);

      if (header == NULL ||
          header->type != (uint8_t)ENP_PACKET_APPLICATION ||
          !enp_address_equal(&header->source, &expected_source) ||
          !enp_address_equal(&header->destination, &expected_destination) ||
          header->sequence != E3_DATA_SEQUENCE ||
          (header->flags & ENP_FLAG_ACK_REQUIRED) == 0U ||
          header->ttl == 0U ||
          header->payload_length < ENP_DATA_HEADER_SIZE)
      {
          return false;
      }

      const enp_data_header_t *data_header =
              (const enp_data_header_t *)enp_packet_payload_const(packet);

      if (data_header == NULL ||
          !enp_data_header_valid(data_header) ||
          data_header->application_sequence != E3_APP_SEQUENCE ||
          !enp_data_payload_length_valid(
                  data_header,
                  (size_t)header->payload_length - ENP_DATA_HEADER_SIZE))
      {
          return false;
      }

      const uint8_t *application_payload =
              ((const uint8_t *)data_header) + ENP_DATA_HEADER_SIZE;

      return memcmp(
                     application_payload,
                     E3_APP_PAYLOAD,
                     sizeof(E3_APP_PAYLOAD)) == 0;
  }

  static bool packet_is_expected_ack(const enp_packet_t *packet)
  {
      if (packet == NULL || !enp_packet_verify(packet))
      {
          return false;
      }

      const enp_header_t *header = enp_packet_header_const(packet);
      const enp_address_t expected_source = make_address(E3_NODE_C);
      const enp_address_t expected_destination = make_address(E3_NODE_A);

      if (header == NULL ||
          header->type != (uint8_t)ENP_PACKET_ACK ||
          !enp_address_equal(&header->source, &expected_source) ||
          !enp_address_equal(&header->destination, &expected_destination) ||
          header->payload_length != ENP_ACK_WIRE_SIZE)
      {
          return false;
      }

      const enp_ack_payload_t *ack =
              (const enp_ack_payload_t *)enp_packet_payload_const(packet);

      return enp_ack_payload_valid(ack) &&
             ack->data_packet_sequence == E3_DATA_SEQUENCE &&
             ack->application_sequence == E3_APP_SEQUENCE;
  }

  static void transport_receive_callback(
          const enp_transport_address_t *source,
          const void *data,
          size_t length)
  {
      if (s_rx_queue == NULL || source == NULL || data == NULL ||
          length == 0U || length > sizeof(enp_packet_t))
      {
          return;
      }

      if (s_node_id == E3_NODE_A &&
          s_a_ack_drop_count == 0U &&
          length <= sizeof(enp_packet_t))
      {
          enp_packet_t probe;
          memset(&probe, 0, sizeof(probe));
          memcpy(enp_packet_data(&probe), data, length);

          if (enp_packet_verify(&probe))
          {
              const enp_header_t *probe_header =
                      enp_packet_header_const(&probe);

              if ((probe_header != NULL) &&
                  probe_header->type == (uint8_t)ENP_PACKET_ACK)
              {
                  ++s_a_ack_drop_count;
                  ESP_LOGI(
                          TAG,
                          "TEST: A intentionally drops first ACK before data-plane processing");
                  return;
              }
          }
      }

      e3c_rx_item_t item;
      memset(&item, 0, sizeof(item));
      memcpy(enp_packet_data(&item.packet), data, length);
      item.source = *source;

      if (xQueueSend(s_rx_queue, &item, 0U) != pdTRUE)
      {
          ESP_LOGW(TAG, "RX queue full; dropping frame");
      }
  }

  static bool receive_next_packet(void)
  {
      if (s_rx_queue == NULL ||
          xQueueReceive(s_rx_queue, &s_rx_item, 0U) != pdTRUE)
      {
          return false;
      }

      return true;
  }

  static esp_err_t local_process(
          void *context,
          const enp_packet_t *packet,
          const enp_transport_address_t *source)
  {
      (void)context;

      if ((packet == NULL) || (source == NULL))
      {
          return ESP_ERR_INVALID_ARG;
      }

      const enp_header_t *header = enp_packet_header_const(packet);
      if (header == NULL)
      {
          return ESP_ERR_INVALID_ARG;
      }

      if (s_node_id == E3_NODE_A &&
          header->type == (uint8_t)ENP_PACKET_ACK)
      {
          if (!packet_is_expected_ack(packet))
          {
              ESP_LOGE(TAG, "FAIL: A received invalid/unexpected ACK");
              s_reliability_failed = true;
              return ESP_ERR_INVALID_ARG;
          }

          if (enp_dispatcher_dispatch(packet, source) != ESP_OK)
          {
              ESP_LOGE(TAG, "FAIL: A dispatcher rejected ACK");
              s_reliability_failed = true;
              return ESP_FAIL;
          }

          ++s_a_ack_dispatch_count;
          ESP_LOGI(TAG, "PASS: A ACK reached reliability through dispatcher");
          return ESP_OK;
      }

      if (s_node_id == E3_NODE_C &&
          header->type == (uint8_t)ENP_PACKET_APPLICATION)
      {
          if (!packet_is_expected_data(packet))
          {
              ESP_LOGE(TAG, "FAIL: C DATA validation");
              return ESP_ERR_INVALID_ARG;
          }

          ++s_c_delivery_count;
          ESP_LOGI(TAG, "PASS: C delivered application DATA exactly once");

          if (!make_ack_from_data(packet))
          {
              ESP_LOGE(TAG, "FAIL: C ACK construction");
              return ESP_FAIL;
          }

          if (enp_routing_data_path_submit(
                      &s_routing_path,
                      &s_local_ack) != ESP_OK)
          {
              ESP_LOGE(TAG, "FAIL: C submitted ACK through routing path");
              return ESP_FAIL;
          }

          ++s_c_ack_sent_count;
          ESP_LOGI(TAG, "PASS: C generated and submitted ACK");
          return ESP_OK;
      }

      return ESP_ERR_NOT_SUPPORTED;
  }

  static void process_next_packet(void)
  {
      if (!receive_next_packet())
      {
          return;
      }

      if (!enp_packet_verify(&s_rx_item.packet))
      {
          ESP_LOGE(TAG,
                   "FAIL: received invalid ENP packet (len=%u)",
                   (unsigned)enp_packet_length(&s_rx_item.packet));
          return;
      }

      const enp_header_t *header =
              enp_packet_header_const(&s_rx_item.packet);

      if (header == NULL)
      {
          ESP_LOGE(TAG, "FAIL: received ENP packet has no header");
          return;
      }

      /*
       * The reusable data plane deliberately owns DATA and ACK only.
       * Discovery and routing-control packets are handled by their
       * respective services and must not be passed to the data plane.
       */
      if ((header->type != (uint8_t)ENP_PACKET_APPLICATION) &&
          (header->type != (uint8_t)ENP_PACKET_ACK))
      {
          ESP_LOGI(TAG,
                   "PASS: ignored non-DATA/ACK packet type=%u",
                   (unsigned)header->type);
          return;
      }

      if (s_node_id == E3_NODE_B)
     {
         if (header->type == (uint8_t)ENP_PACKET_APPLICATION)
         {
             ++s_b_data_received_count;
             if (s_b_data_received_count == 2U)
             {
                 ++s_b_duplicate_data_count;
                 s_b_expect_cached_ack_forward = true;
                 ESP_LOGI(TAG,
                          "PASS: B observed duplicate DATA; expecting cached ACK recovery");
             }
         }
         else if (header->type == (uint8_t)ENP_PACKET_ACK)
         {
             ++s_b_ack_received_count;
         }
     }

     const esp_err_t err = enp_data_plane_process(
              &s_data_plane,
              &s_rx_item.packet,
              &s_rx_item.source);

      if (err != ESP_OK)
      {
          ESP_LOGE(TAG,
                   "FAIL: ENP data plane rejected type=%u err=%s (%d)",
                   (unsigned)header->type,
                   esp_err_to_name(err),
                   (int)err);
          return;
      }

      if (s_node_id == E3_NODE_B)
      {
          if (header->type == (uint8_t)ENP_PACKET_APPLICATION)
          {
              ESP_LOGI(TAG,
                       "PASS: B data plane processed DATA for forwarding");
          }
          else if (header->type == (uint8_t)ENP_PACKET_ACK)
          {
              ESP_LOGI(TAG,
                       "PASS: B data plane processed ACK for forwarding/cache");
          }
      }
  }

  static bool common_setup(enp_node_id_t node_id)
  {
      nvs_flash_init();

      if (esp_netif_init() != ESP_OK ||
          esp_event_loop_create_default() != ESP_OK ||
          enp_wifi_init() != ESP_OK)
      {
          return false;
      }

      while (!enp_wifi_is_connected())
      {
          vTaskDelay(pdMS_TO_TICKS(100));
      }

      ESP_LOGI(TAG,
               "Wi-Fi connected, channel=%u node=%u",
               (unsigned)enp_wifi_get_channel(),
               (unsigned)node_id);

      enp_transport_t *transport = enp_transport_espnow_get();
      if (transport == NULL)
      {
          return false;
      }

      s_underlying_transport = transport;
      memset(&s_observed_transport, 0, sizeof(s_observed_transport));
      s_observed_transport.send = observed_transport_send;
      s_observed_transport.set_receive_callback = transport->set_receive_callback;

      const enp_config_t config = make_config(node_id);

      if (enp_context_init(&s_context, transport, &config) != ESP_OK)
      {
          return false;
      }

      if (!install_local_neighbors(node_id) ||
          !install_local_topology(node_id))
      {
          return false;
      }

      if (!enp_routing_data_path_init(
                  &s_routing_path,
                  &s_routes,
                  &s_observed_transport,
                  resolve_transport,
                  &s_context))
      {
          return false;
      }

      if (enp_data_plane_init(
                  &s_data_plane,
                  &s_context,
                  &s_routing_path,
                  local_process) != ESP_OK)
      {
          return false;
      }

      s_rx_queue = xQueueCreateStatic(
              E3C_RX_QUEUE_LENGTH,
              sizeof(e3c_rx_item_t),
              s_rx_queue_storage,
              &s_rx_queue_buffer);
      if (s_rx_queue == NULL)
      {
          return false;
      }

      if (enp_transport_set_receive_callback(
                  transport,
                  transport_receive_callback) != ESP_OK)
      {
          return false;
      }

      return true;
  }

  static bool sender_setup(void)
  {
      if (!enp_reliability_init() ||
          !enp_reliability_set_submit_callback(
                  submit_to_routing,
                  &s_routing_path) ||
          !enp_reliability_set_result_callback(
                  reliability_result,
                  NULL) ||
          !enp_reliability_start())
      {
          return false;
      }

      if (enp_dispatcher_init(&s_context) != ESP_OK ||
          enp_dispatcher_register(
                  enp_reliability_service_get()) != ESP_OK)
      {
          return false;
      }

      if (enp_maintenance_init(&s_context) != ESP_OK)
      {
          return false;
      }

      if (!make_reliable_data())
      {
          return false;
      }

      ESP_LOGI(TAG,
               "PASS: A reliability transaction prepared seq=0x%04" PRIX32,
               (uint32_t)E3_DATA_SEQUENCE);
      return true;
  }

  static void sender_run(void)
  {
      const uint32_t start = enp_context_time_ms(&s_context);
      uint32_t now_ms = start;

      if (!enp_reliability_send(
                  &s_tx_data,
                  now_ms,
                  &s_reliability_handle))
      {
          ESP_LOGE(TAG, "FAIL: A initial reliable DATA submission");
          return;
      }

      ESP_LOGI(TAG, "PASS: A submitted reliable DATA through routing");

      while ((enp_context_time_ms(&s_context) - start) < E3_TEST_TIMEOUT_MS)
      {
          now_ms = enp_context_time_ms(&s_context);

          process_next_packet();

          if (s_reliability_delivered || s_reliability_failed)
          {
              break;
          }

          vTaskDelay(pdMS_TO_TICKS(E3_POLL_MS));
      }

      if (s_reliability_failed)
      {
          ESP_LOGE(TAG, "FAIL: A reliability transaction FAILED");
          return;
      }

      if (!s_reliability_delivered)
      {
          ESP_LOGE(TAG, "FAIL: A did not receive correlated ACK");
          return;
      }

      if (s_reliability_submit_count != 2U)
      {
          ESP_LOGE(TAG,
                   "FAIL: A expected exactly 2 routing submissions (initial + 1 retry), got %lu",
                   (unsigned long)s_reliability_submit_count);
          return;
      }

      ESP_LOGI(TAG, "PASS: A reliability transaction DELIVERED");
      ESP_LOGI(TAG, "PASS: A observed exactly one retransmission through routing");
      ESP_LOGI(TAG, "PASS: A received recovered ACK through B");
      ESP_LOGI(TAG, "E3.3.7 Phase 4 / E3 A ROLE PASS");
  }

  static void relay_run(void)
  {
      const uint32_t start = enp_context_time_ms(&s_context);

      while ((enp_context_time_ms(&s_context) - start) < E3_TEST_TIMEOUT_MS)
      {
          process_next_packet();

          if (s_b_data_received_count >= 2U &&
              s_b_ack_received_count >= 1U &&
              s_b_cached_ack_recovery_count >= 1U)
          {
              break;
          }

          vTaskDelay(pdMS_TO_TICKS(E3_POLL_MS));
      }

      if (s_node_id != E3_NODE_B)
      {
          return;
      }

      bool pass = true;

      if (s_b_data_received_count != 2U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 2 DATA receptions (original + duplicate), got %lu",
                   (unsigned long)s_b_data_received_count);
          pass = false;
      }

      if (s_b_data_forwarded_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 1 DATA forwarding, got %lu",
                   (unsigned long)s_b_data_forwarded_count);
          pass = false;
      }

      if (s_b_ack_received_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 1 ACK reception, got %lu",
                   (unsigned long)s_b_ack_received_count);
          pass = false;
      }

      if (s_b_ack_forwarded_count != 2U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 2 ACK forwardings (original + cached), got %lu",
                   (unsigned long)s_b_ack_forwarded_count);
          pass = false;
      }

      if (s_b_duplicate_data_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 1 duplicate DATA suppression, got %lu",
                   (unsigned long)s_b_duplicate_data_count);
          pass = false;
      }

      if (s_b_cached_ack_recovery_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: B expected 1 cached-ACK recovery/forwarding, got %lu",
                   (unsigned long)s_b_cached_ack_recovery_count);
          pass = false;
      }

      if (pass)
      {
          ESP_LOGI(TAG, "PASS: B DATA received exactly twice");
          ESP_LOGI(TAG, "PASS: B DATA forwarded exactly once");
          ESP_LOGI(TAG, "PASS: B ACK received exactly once");
          ESP_LOGI(TAG, "PASS: B ACK forwarded exactly twice");
          ESP_LOGI(TAG, "PASS: B suppressed duplicate DATA exactly once");
          ESP_LOGI(TAG, "PASS: B recovered and forwarded cached ACK exactly once");
          ESP_LOGI(TAG, "E3.3.7 Phase 4 / E3 B ROLE PASS");
      }
      else
      {
          ESP_LOGE(TAG, "E3.3.7 Phase 4 / E3 B ROLE FAIL");
      }
  }

  static void sensor_run(void)
  {
      const uint32_t start = enp_context_time_ms(&s_context);

      while ((enp_context_time_ms(&s_context) - start) < E3_TEST_TIMEOUT_MS)
      {
          process_next_packet();

          if (s_c_delivery_count >= 1U && s_c_ack_sent_count >= 1U)
          {
              break;
          }

          vTaskDelay(pdMS_TO_TICKS(E3_POLL_MS));
      }

      if (s_c_delivery_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: C expected exactly one application delivery, got %lu",
                   (unsigned long)s_c_delivery_count);
          return;
      }

      if (s_c_ack_sent_count != 1U)
      {
          ESP_LOGE(TAG,
                   "FAIL: C expected exactly one ACK transmission, got %lu",
                   (unsigned long)s_c_ack_sent_count);
          return;
      }

      ESP_LOGI(TAG, "PASS: C delivered DATA exactly once");
      ESP_LOGI(TAG, "PASS: C generated exactly one ACK");
      ESP_LOGI(TAG, "E3.3.7 Phase 4 / E3 C ROLE PASS");
  }

  void app_main(void)
  {
      const enp_node_id_t node_id =
              (enp_node_id_t)CONFIG_ENP_E3_NODE_ID;
      s_node_id = node_id;

      ESP_LOGI(TAG, "======================================");
      ESP_LOGI(TAG, "E3.3.7 PHASE 4 / E3 E3C CONSOLIDATION");
      ESP_LOGI(TAG, "A -> B -> C DATA / C -> B -> A ACK");
      ESP_LOGI(TAG, "First ACK dropped before A data-plane processing; retry must recover cached ACK at B");
      ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
      ESP_LOGI(TAG, "Node=%u", (unsigned)node_id);
      ESP_LOGI(TAG, "======================================");

      if (!common_setup(node_id))
      {
          ESP_LOGE(TAG, "P4/E3 setup FAIL");
          return;
      }

      if (node_id == E3_NODE_A)
      {
          if (!sender_setup())
          {
              ESP_LOGE(TAG, "P4/E3 A setup FAIL");
              return;
          }
          sender_run();
      }
      else if (node_id == E3_NODE_B)
      {
          relay_run();
      }
      else if (node_id == E3_NODE_C)
      {
          sensor_run();
      }
      else
      {
          ESP_LOGE(TAG, "Unsupported E3C node ID=%u", (unsigned)node_id);
      }

      if (node_id == E3_NODE_A)
      {
          (void)enp_maintenance_deinit();
          vTaskDelay(pdMS_TO_TICKS(100U));
          (void)enp_dispatcher_deinit();
          (void)enp_reliability_deinit();
      }

      (void)enp_data_plane_deinit(&s_data_plane);
      (void)enp_context_deinit(&s_context);
  }





