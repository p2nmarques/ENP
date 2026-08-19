/*
 * enp_route_repair.h
 *
 * ENP v0.2 — Phase 4 / P4-E5D
 * Route Repair Coordinator — Step 1
 *
 *   Created on: Aug 19, 2026
 *      Author: Pedro Marques
 *
 * This component is intentionally limited to the E5D repair-request boundary.
 * R4 discovery integration, route installation, failed-next-hop exclusion,
 * and reliability integration are added only in later E5D steps.
 */

#ifndef ENP_ROUTE_REPAIR_H
#define ENP_ROUTE_REPAIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "core/routing/enp_route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENP_ROUTE_REPAIR_MAX_PENDING
#define ENP_ROUTE_REPAIR_MAX_PENDING 8U
#endif

#ifndef ENP_ROUTE_REPAIR_QUEUE_LENGTH
#define ENP_ROUTE_REPAIR_QUEUE_LENGTH ENP_ROUTE_REPAIR_MAX_PENDING
#endif

#ifndef ENP_ROUTE_REPAIR_TASK_STACK_SIZE
#define ENP_ROUTE_REPAIR_TASK_STACK_SIZE 2048U
#endif

#ifndef ENP_ROUTE_REPAIR_TASK_PRIORITY
#define ENP_ROUTE_REPAIR_TASK_PRIORITY 5U
#endif

typedef struct {
	enp_route_destination_t destination;
	enp_route_destination_t failed_next_hop;
} enp_route_repair_request_t;

/*
 * Invoked from the dedicated E5D repair task, never from the caller of
 * enp_route_repair_request().
 *
 * This is a Step-1 observation boundary. Later E5D steps will replace the
 * observation callback with the R4 repair coordinator.
 */
typedef void (*enp_route_repair_consume_fn)(
	const enp_route_repair_request_t *request, void *context);

/*
 * Lifetime: the caller must provide storage that remains valid for the entire
 * lifetime of the E5D repair task. In production this should normally be
 * static/global storage; automatic (stack) storage is not permitted.
 */
typedef struct {
	StaticQueue_t queue_control;
	QueueHandle_t queue;
	uint8_t queue_storage[ENP_ROUTE_REPAIR_QUEUE_LENGTH *
						  sizeof(enp_route_repair_request_t)];

	StaticTask_t task_control;
	StackType_t task_stack[ENP_ROUTE_REPAIR_TASK_STACK_SIZE];

	bool initialized;
	bool task_running;

	/*
	 * A bounded destination set used for duplicate suppression while a repair
	 * request is pending or being consumed. No dynamic allocation is used.
	 */
	enp_route_repair_request_t pending[ENP_ROUTE_REPAIR_MAX_PENDING];
	size_t pending_count;

	enp_route_repair_consume_fn consume;
	void *consume_context;

	uint32_t request_count;
	uint32_t suppressed_count;
	uint32_t consumed_count;
} enp_route_repair_t;

bool enp_route_repair_init(enp_route_repair_t *repair,
						   enp_route_repair_consume_fn consume,
						   void *consume_context);

bool enp_route_repair_request(enp_route_repair_t *repair,
							  enp_route_destination_t destination,
							  enp_route_destination_t failed_next_hop);

bool enp_route_repair_is_pending(const enp_route_repair_t *repair,
								 enp_route_destination_t destination);

size_t enp_route_repair_pending_count(const enp_route_repair_t *repair);

uint32_t enp_route_repair_request_count(const enp_route_repair_t *repair);
uint32_t enp_route_repair_suppressed_count(const enp_route_repair_t *repair);
uint32_t enp_route_repair_consumed_count(const enp_route_repair_t *repair);

#ifdef __cplusplus
}
#endif

#endif /* ENP_ROUTE_REPAIR_H */
