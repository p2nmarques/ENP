/*
 * enp_route_failure_coalescer.h
 *
 *  Created on: Aug 28, 2026
 *      Author: Pedro Marques
 * 
 * ENP v0.2 — RERR production integration
 * IG-D — bounded transient failure-event coalescer
 *
 * ESP-IDF 6.0.2 target.
 *
 * This component owns only transient handoff state between failure observation
 * and E5D admission. It does not own persistent route state.
 */

#ifndef ENP_ROUTE_FAILURE_COALESCER_H
#define ENP_ROUTE_FAILURE_COALESCER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/routing/enp_route_repair.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ENP_ROUTE_FAILURE_COALESCER_MAX_EVENTS
#define ENP_ROUTE_FAILURE_COALESCER_MAX_EVENTS ENP_ROUTE_REPAIR_MAX_PENDING
#endif

#ifndef ENP_ROUTE_FAILURE_COALESCER_TASK_STACK_SIZE
#define ENP_ROUTE_FAILURE_COALESCER_TASK_STACK_SIZE 2048U
#endif

#ifndef ENP_ROUTE_FAILURE_COALESCER_TASK_PRIORITY
#define ENP_ROUTE_FAILURE_COALESCER_TASK_PRIORITY 5U
#endif

typedef enum {
	ENP_ROUTE_FAILURE_EVENT_ABSENT = 0,
	ENP_ROUTE_FAILURE_EVENT_PENDING,
	ENP_ROUTE_FAILURE_EVENT_CLAIMED,
} enp_route_failure_event_state_t;

typedef struct {
	enp_route_destination_t destination;
	enp_route_destination_t failed_next_hop;
	enp_route_failure_event_state_t state;
} enp_route_failure_event_t;

typedef struct {
	StaticTask_t task_control;
	TaskHandle_t task;
	StackType_t task_stack[ENP_ROUTE_FAILURE_COALESCER_TASK_STACK_SIZE];

	bool initialized;
	bool task_running;

	enp_route_repair_t *repair;

	portMUX_TYPE lock;
	enp_route_failure_event_t events[ENP_ROUTE_FAILURE_COALESCER_MAX_EVENTS];
	size_t event_count;

	uint32_t observed_count;
	uint32_t duplicate_count;
	uint32_t capacity_count;
	uint32_t queue_failure_count;
	uint32_t invalid_count;
	uint32_t accepted_count;
	uint32_t duplicate_admission_count;
	uint32_t overflow_count;
} enp_route_failure_coalescer_t;

/*
 * Initializes the bounded transient coalescer and creates its dedicated static
 * handoff task. The caller retains ownership of E5D.
 */
bool enp_route_failure_coalescer_init(
	enp_route_failure_coalescer_t *coalescer,
	enp_route_repair_t *repair);

/*
 * Minimal failure-observation entry point.
 *
 * Coalescing identity is exactly:
 *     (destination, failed_next_hop)
 *
 * This function does not perform E5D admission synchronously.
 */
bool enp_route_failure_coalescer_observe(
	enp_route_failure_coalescer_t *coalescer,
	enp_route_destination_t destination,
	enp_route_destination_t failed_next_hop);

/*
 * E5D capacity-release notification hook.
 *
 * This is notification-only and must be registered with
 * enp_route_repair_set_capacity_available_callback(). It performs no
 * synchronous re-entry into E5D admission.
 */
void enp_route_failure_coalescer_notify_capacity_available(void *context);

size_t enp_route_failure_coalescer_event_count(
	const enp_route_failure_coalescer_t *coalescer);

#ifdef __cplusplus
}
#endif

#endif /* ENP_ROUTE_FAILURE_COALESCER_H */
