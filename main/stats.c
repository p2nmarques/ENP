/*
 * stats.c
 *
 *  Created on: Aug 4, 2026
 *      Author: Pedro Marques
 */

 #include "stats.h"

 #include <inttypes.h>
 #include <string.h>

 #include "esp_log.h"

 void stats_init(node_stats_t *stats)
 {
     memset(stats, 0, sizeof(*stats));
 }

 void stats_reset(node_stats_t *stats)
 {
     memset(stats, 0, sizeof(*stats));
 }

 void stats_inc_tx(node_stats_t *stats)
 {
     stats->tx_packets++;
 }

 void stats_inc_rx(node_stats_t *stats)
 {
     stats->rx_packets++;
 }

 void stats_inc_tx_sensor(node_stats_t *stats)
 {
     stats->tx_sensor++;
 }

 void stats_inc_rx_sensor(node_stats_t *stats)
 {
     stats->rx_sensor++;
 }

 void stats_inc_tx_ack(node_stats_t *stats)
 {
     stats->tx_ack++;
 }

 void stats_inc_rx_ack(node_stats_t *stats)
 {
     stats->rx_ack++;
 }

 void stats_inc_crc_error(node_stats_t *stats)
 {
     stats->crc_errors++;
 }

 void stats_inc_send_error(node_stats_t *stats)
 {
     stats->send_errors++;
 }

 void stats_inc_unknown(node_stats_t *stats)
 {
     stats->unknown_packets++;
 }

 void stats_print(
         const char *tag,
         const node_stats_t *stats)
 {
     ESP_LOGI(tag, "========================================");
     ESP_LOGI(tag, "Node Statistics");
     ESP_LOGI(tag, "========================================");

     ESP_LOGI(tag, "TX Packets      : %" PRIu32, stats->tx_packets);
     ESP_LOGI(tag, "RX Packets      : %" PRIu32, stats->rx_packets);

     ESP_LOGI(tag, "TX Sensor       : %" PRIu32, stats->tx_sensor);
     ESP_LOGI(tag, "RX Sensor       : %" PRIu32, stats->rx_sensor);

     ESP_LOGI(tag, "TX ACK          : %" PRIu32, stats->tx_ack);
     ESP_LOGI(tag, "RX ACK          : %" PRIu32, stats->rx_ack);

     ESP_LOGI(tag, "CRC Errors      : %" PRIu32, stats->crc_errors);
     ESP_LOGI(tag, "Send Errors     : %" PRIu32, stats->send_errors);
     ESP_LOGI(tag, "Unknown Packets : %" PRIu32, stats->unknown_packets);

     ESP_LOGI(tag, "========================================");
 }