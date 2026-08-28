/*
 * enp_route_failure_coalescer.c
 *
 *  Created on: Aug 28, 2026
 *      Author: Pedro Marques
 * 
 * IG-D — bounded transient failure-event coalescer
 * ESP-IDF 6.0.2 target.
 */

#include "enp_route_failure_coalescer.h"

#include <string.h>

static bool destination_equal(enp_route_destination_t lhs,
							  enp_route_destination_t rhs) {
	return lhs.network_id == rhs.network_id && lhs.node_id == rhs.node_id;
}

static bool event_equal(const enp_route_failure_event_t *event,
						enp_route_destination_t destination,
						enp_route_destination_t failed_next_hop) {
	return event != NULL &&
		destination_equal(event->destination, destination) &&
		destination_equal(event->failed_next_hop, failed_next_hop);
}

static int find_event_locked(const enp_route_failure_coalescer_t *coalescer,
							 enp_route_destination_t destination,
							 enp_route_destination_t failed_next_hop) {
	for (size_t i = 0U; i < coalescer->event_count; ++i) {
		if (event_equal(&coalescer->events[i], destination,
						failed_next_hop)) {
			return (int)i;
		}
	}
	return -1;
}

static void remove_event_locked(enp_route_failure_coalescer_t *coalescer,
								size_t index) {
	if (index + 1U < coalescer->event_count) {
		memmove(&coalescer->events[index],
				&coalescer->events[index + 1U],
				(coalescer->event_count - index - 1U) *
					sizeof(coalescer->events[0]));
	}
	--coalescer->event_count;
}

static bool claim_event(enp_route_failure_coalescer_t *coalescer,
						enp_route_failure_event_t *event) {
	bool claimed = false;

	portENTER_CRITICAL(&coalescer->lock);
	for (size_t i = 0U; i < coalescer->event_count; ++i) {
		if (coalescer->events[i].state ==
			ENP_ROUTE_FAILURE_EVENT_PENDING) {
			coalescer->events[i].state =
				ENP_ROUTE_FAILURE_EVENT_CLAIMED;
			*event = coalescer->events[i];
			claimed = true;
			break;
		}
	}
	portEXIT_CRITICAL(&coalescer->lock);

	return claimed;
}

static void dispose_claim(enp_route_failure_coalescer_t *coalescer,
						  const enp_route_failure_event_t *event,
						  enp_route_repair_request_result_t result) {
	portENTER_CRITICAL(&coalescer->lock);

	int index = find_event_locked(coalescer, event->destination,
								  event->failed_next_hop);
	if (index >= 0 &&
		coalescer->events[(size_t)index].state ==
			ENP_ROUTE_FAILURE_EVENT_CLAIMED) {
		switch (result) {
		case ENP_ROUTE_REPAIR_REQUEST_ACCEPTED:
			++coalescer->accepted_count;
			remove_event_locked(coalescer, (size_t)index);
			break;

		case ENP_ROUTE_REPAIR_REQUEST_DUPLICATE:
			++coalescer->duplicate_admission_count;
			remove_event_locked(coalescer, (size_t)index);
			break;

		case ENP_ROUTE_REPAIR_REQUEST_CAPACITY:
			++coalescer->capacity_count;
			coalescer->events[(size_t)index].state =
				ENP_ROUTE_FAILURE_EVENT_PENDING;
			break;

		case ENP_ROUTE_REPAIR_REQUEST_QUEUE_FAILURE:
			++coalescer->queue_failure_count;
			coalescer->events[(size_t)index].state =
				ENP_ROUTE_FAILURE_EVENT_PENDING;
			break;

		case ENP_ROUTE_REPAIR_REQUEST_INVALID:
		default:
			++coalescer->invalid_count;
			remove_event_locked(coalescer, (size_t)index);
			break;
		}
	}

	portEXIT_CRITICAL(&coalescer->lock);
}

static void coalescer_task(void *arg) {
	enp_route_failure_coalescer_t *coalescer =
		(enp_route_failure_coalescer_t *)arg;

	if (coalescer == NULL) {
		vTaskDelete(NULL);
		return;
	}

	coalescer->task_running = true;

	for (;;) {
		/*
		 * A notification is only a wake hint. On wake, inspect bounded
		 * authoritative coalescer state.
		 */
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		for (;;) {
			enp_route_failure_event_t event;
			if (!claim_event(coalescer, &event)) {
				break;
			}

			/*
			 * Critical invariant: E5D admission is outside the
			 * coalescer lock. CLAIMED remains visible to duplicate
			 * detection while this call executes.
			 */
			enp_route_repair_request_result_t result =
				enp_route_repair_request_ex(
					coalescer->repair,
					event.destination,
					event.failed_next_hop);

			dispose_claim(coalescer, &event, result);
		}
	}
}

bool enp_route_failure_coalescer_init(
	enp_route_failure_coalescer_t *coalescer,
	enp_route_repair_t *repair) {
	if (coalescer == NULL || repair == NULL || !repair->initialized) {
		return false;
	}

	memset(coalescer, 0, sizeof(*coalescer));
	coalescer->repair = repair;
	coalescer->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

	TaskHandle_t task = xTaskCreateStatic(
		coalescer_task,
		"enp_rerr_handoff",
		ENP_ROUTE_FAILURE_COALESCER_TASK_STACK_SIZE,
		coalescer,
		ENP_ROUTE_FAILURE_COALESCER_TASK_PRIORITY,
		coalescer->task_stack,
		&coalescer->task_control);

	if (task == NULL) {
		return false;
	}

	coalescer->task = task;
	coalescer->initialized = true;

	if (!enp_route_repair_set_capacity_available_callback(
			repair,
			enp_route_failure_coalescer_notify_capacity_available,
			coalescer)) {
		coalescer->initialized = false;
		return false;
	}

	return true;
}

bool enp_route_failure_coalescer_observe(
	enp_route_failure_coalescer_t *coalescer,
	enp_route_destination_t destination,
	enp_route_destination_t failed_next_hop) {
	if (coalescer == NULL || !coalescer->initialized ||
		destination.network_id == 0U || destination.node_id == 0U ||
		failed_next_hop.network_id == 0U || failed_next_hop.node_id == 0U) {
		return false;
	}

	bool accepted = false;
	TaskHandle_t task;

	portENTER_CRITICAL(&coalescer->lock);
	++coalescer->observed_count;

	if (find_event_locked(coalescer, destination,
						  failed_next_hop) >= 0) {
		++coalescer->duplicate_count;
		accepted = true;
	} else if (coalescer->event_count >=
			   ENP_ROUTE_FAILURE_COALESCER_MAX_EVENTS) {
		++coalescer->overflow_count;
		accepted = false;
	} else {
		coalescer->events[coalescer->event_count++] =
			(enp_route_failure_event_t){
				.destination = destination,
				.failed_next_hop = failed_next_hop,
				.state = ENP_ROUTE_FAILURE_EVENT_PENDING,
			};
		accepted = true;
	}

	task = coalescer->task;
	portEXIT_CRITICAL(&coalescer->lock);

	/*
	 * Wake after state publication. The task notification is intentionally
	 * only a hint; state is re-inspected by the worker.
	 */
	if (accepted && task != NULL) {
		xTaskNotifyGive(task);
	}

	return accepted;
}

void enp_route_failure_coalescer_notify_capacity_available(void *context) {
	enp_route_failure_coalescer_t *coalescer =
		(enp_route_failure_coalescer_t *)context;

	if (coalescer == NULL || !coalescer->initialized) {
		return;
	}

	if (coalescer->task != NULL) {
		xTaskNotifyGive(coalescer->task);
	}
}

size_t enp_route_failure_coalescer_event_count(
	const enp_route_failure_coalescer_t *coalescer) {
	if (coalescer == NULL) {
		return 0U;
	}

	size_t count;
	portENTER_CRITICAL((portMUX_TYPE *)&coalescer->lock);
	count = coalescer->event_count;
	portEXIT_CRITICAL((portMUX_TYPE *)&coalescer->lock);
	return count;
}
