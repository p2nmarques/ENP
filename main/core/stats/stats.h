/*
 * stats.h
 *
 *  Created on: Aug 4, 2026
 *      Author: Pedro Marques
 */

#ifndef STATS_H
#define STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t tx_packets;
	uint32_t rx_packets;

	uint32_t tx_sensor;
	uint32_t rx_sensor;

	uint32_t tx_ack;
	uint32_t rx_ack;

	uint32_t crc_errors;
	uint32_t send_errors;

	uint32_t unknown_packets;

} node_stats_t;

/*------------------------------------------------------------------
 * Lifecycle
 *-----------------------------------------------------------------*/

void enp_stats_init(node_stats_t *stats);

void enp_stats_reset(node_stats_t *stats);

/*------------------------------------------------------------------
 * Increment helpers
 *-----------------------------------------------------------------*/

void enp_stats_inc_tx(node_stats_t *stats);

void enp_stats_inc_rx(node_stats_t *stats);

void enp_stats_inc_tx_sensor(node_stats_t *stats);

void enp_stats_inc_rx_sensor(node_stats_t *stats);

void enp_stats_inc_tx_ack(node_stats_t *stats);

void enp_stats_inc_rx_ack(node_stats_t *stats);

void enp_stats_inc_crc_error(node_stats_t *stats);

void enp_stats_inc_send_error(node_stats_t *stats);

void enp_stats_inc_unknown(node_stats_t *stats);

/*------------------------------------------------------------------
 * Display
 *-----------------------------------------------------------------*/

void enp_stats_print(const char *tag, const node_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif