/*
 * enp_route_repair.c
 *
 *  Created on: Aug 19, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — Phase 4 / P4-E5D
 * Route Repair Coordinator — Step 1
 */

#include "enp_route_repair.h"

#include <string.h>

static bool destination_equal(enp_route_destination_t lhs,
							  enp_route_destination_t rhs) {
	return lhs.network_id == rhs.network_id && lhs.node_id == rhs.node_id;
}

static int find_pending(const enp_route_repair_t *repair,
						enp_route_destination_t destination) {
	if (repair == NULL) {
		return -1;
	}

	for (size_t i = 0U; i < repair->pending_count; ++i) {
		if (destination_equal(repair->pending[i].destination, destination)) {
			return (int)i;
		}
	}

	return -1;
}

static void remove_pending(enp_route_repair_t *repair,
						   enp_route_destination_t destination) {
	int index = find_pending(repair, destination);
	if (index < 0) {
		return;
	}

	size_t i = (size_t)index;
	if (i + 1U < repair->pending_count) {
		memmove(&repair->pending[i], &repair->pending[i + 1U],
				(repair->pending_count - i - 1U) * sizeof(repair->pending[0]));
	}

	--repair->pending_count;
}

static void repair_task(void *arg) {
	enp_route_repair_t *repair = (enp_route_repair_t *)arg;
	enp_route_repair_request_t request;

	if (repair == NULL) {
		vTaskDelete(NULL);
		return;
	}

	repair->task_running = true;

	for (;;) {
		if (xQueueReceive(repair->queue, &request, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		/*
		 * The request remains in the bounded pending set while the consume
		 * callback executes. This preserves duplicate suppression across the
		 * pending -> task-consumption boundary.
		 */
		if (repair->consume != NULL) {
			repair->consume(&request, repair->consume_context);
		}

		remove_pending(repair, request.destination);
		++repair->consumed_count;

		/*
		 * A pending admission slot has been authoritatively released.
		 * This is notification-only: the external handoff owner decides
		 * whether any retained failure event should be retried.
		 */
		if (repair->capacity_available != NULL) {
			repair->capacity_available(repair->capacity_available_context);
		}
	}
}

bool enp_route_repair_init(enp_route_repair_t *repair,
						   enp_route_repair_consume_fn consume,
						   void *consume_context) {
	if (repair == NULL) {
		return false;
	}

	memset(repair, 0, sizeof(*repair));

	repair->consume = consume;
	repair->consume_context = consume_context;

	QueueHandle_t queue = xQueueCreateStatic(
		ENP_ROUTE_REPAIR_QUEUE_LENGTH, sizeof(enp_route_repair_request_t),
		repair->queue_storage, &repair->queue_control);

	if (queue == NULL) {
		return false;
	}

	/*
	 * Publish the queue handle before creating the task.
	 * xTaskCreateStatic() may make the new task runnable immediately; the task
	 * therefore must never be able to enter xQueueReceive() while
	 * repair->queue is still NULL.
	 */
	repair->queue = queue;

	TaskHandle_t task = xTaskCreateStatic(
		repair_task, "enp_route_repair", ENP_ROUTE_REPAIR_TASK_STACK_SIZE,
		repair, ENP_ROUTE_REPAIR_TASK_PRIORITY, repair->task_stack,
		&repair->task_control);

	if (task == NULL) {
		return false;
	}

	repair->initialized = true;
	return true;
}

bool enp_route_repair_set_capacity_available_callback(
	enp_route_repair_t *repair,
	enp_route_repair_capacity_available_fn callback,
	void *context) {
	if (repair == NULL || !repair->initialized) {
		return false;
	}

	repair->capacity_available = callback;
	repair->capacity_available_context = context;
	return true;
}

enp_route_repair_request_result_t
enp_route_repair_request_ex(enp_route_repair_t *repair,
							enp_route_destination_t destination,
							enp_route_destination_t failed_next_hop) {
	if (repair == NULL || !repair->initialized ||
		destination.network_id == 0U || destination.node_id == 0U ||
		failed_next_hop.network_id == 0U || failed_next_hop.node_id == 0U) {
		return ENP_ROUTE_REPAIR_REQUEST_INVALID;
	}

	++repair->request_count;

	if (find_pending(repair, destination) >= 0) {
		++repair->suppressed_count;
		return ENP_ROUTE_REPAIR_REQUEST_DUPLICATE;
	}

	if (repair->pending_count >= ENP_ROUTE_REPAIR_MAX_PENDING) {
		return ENP_ROUTE_REPAIR_REQUEST_CAPACITY;
	}

	enp_route_repair_request_t request = {
		.destination = destination,
		.failed_next_hop = failed_next_hop,
	};

	/* Reserve before publishing because the worker may run immediately. */
	repair->pending[repair->pending_count++] = request;

	if (xQueueSend(repair->queue, &request, 0) != pdTRUE) {
		remove_pending(repair, destination);
		return ENP_ROUTE_REPAIR_REQUEST_QUEUE_FAILURE;
	}

	return ENP_ROUTE_REPAIR_REQUEST_ACCEPTED;
}

bool enp_route_repair_request(enp_route_repair_t *repair,
							  enp_route_destination_t destination,
							  enp_route_destination_t failed_next_hop) {
	return enp_route_repair_request_ex(repair, destination, failed_next_hop) ==
		ENP_ROUTE_REPAIR_REQUEST_ACCEPTED;
}

bool enp_route_repair_is_pending(const enp_route_repair_t *repair,
								 enp_route_destination_t destination) {
	return find_pending(repair, destination) >= 0;
}

size_t enp_route_repair_pending_count(const enp_route_repair_t *repair) {
	return repair != NULL ? repair->pending_count : 0U;
}

uint32_t enp_route_repair_request_count(const enp_route_repair_t *repair) {
	return repair != NULL ? repair->request_count : 0U;
}

uint32_t enp_route_repair_suppressed_count(const enp_route_repair_t *repair) {
	return repair != NULL ? repair->suppressed_count : 0U;
}

uint32_t enp_route_repair_consumed_count(const enp_route_repair_t *repair) {
	return repair != NULL ? repair->consumed_count : 0U;
}
