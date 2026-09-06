/*
 * enp_rerr_recipient_selection.h
 *
 *  Created on: Sep 5, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-G.6
 * Dependent-recipient selection from authoritative routing evidence.
 *
 * ESP-IDF target: 6.0.2
 */
#ifndef ENP_RERR_RECIPIENT_SELECTION_H
#define ENP_RERR_RECIPIENT_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

#include "core/enp_address.h"
#include "core/routing/enp_rerr_generation.h"
#include "core/routing/enp_route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Selection context. No persistent precursor database is introduced.
 * The route table remains the sole routing-state source.
 */
typedef struct {
    const enp_route_table_t *route_table;
    enp_address_t local_address;
} enp_rerr_recipient_selection_t;

/*
 * Initialize a selector over an existing authoritative route table.
 */
bool enp_rerr_recipient_selection_init(
    enp_rerr_recipient_selection_t *selection,
    const enp_route_table_t *route_table,
    enp_address_t local_address);

/*
 * Enumerate dependent recipients using current routing evidence.
 *
 * A recipient is considered evidenced when an ACTIVE route has this node
 * as its next hop. The route destination is then the dependent logical node.
 * STALE/INVALID routes are ignored. The unreachable destination itself and
 * the local node are never returned.
 */
bool enp_rerr_recipient_selection_next(
    void *context,
    const enp_rerr_generation_request_t *request,
    size_t index,
    enp_route_destination_t *recipient);

#ifdef __cplusplus
}
#endif

#endif /* ENP_RERR_RECIPIENT_SELECTION_H */
