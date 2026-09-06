/*
 * enp_rerr_recipient_selection.c
 *
 *  Created on: Sep 5, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.3.1 — IG-G.6
 * Dependent-recipient selection from authoritative routing evidence.
 *
 * ESP-IDF target: 6.0.2
 */

#include "enp_rerr_recipient_selection.h"

#include <string.h>

static bool destination_equal(enp_route_destination_t lhs,
                              enp_route_destination_t rhs)
{
    return lhs.network_id == rhs.network_id &&
           lhs.node_id == rhs.node_id;
}

bool enp_rerr_recipient_selection_init(
    enp_rerr_recipient_selection_t *selection,
    const enp_route_table_t *route_table,
    enp_address_t local_address)
{
    if (selection == NULL || route_table == NULL ||
        local_address.network == 0U || local_address.node == 0U) {
        return false;
    }

    memset(selection, 0, sizeof(*selection));
    selection->route_table = route_table;
    selection->local_address = local_address;
    return true;
}

bool enp_rerr_recipient_selection_next(
    void *context,
    const enp_rerr_generation_request_t *request,
    size_t index,
    enp_route_destination_t *recipient)
{
    const enp_rerr_recipient_selection_t *selection = context;

    if (selection == NULL || selection->route_table == NULL ||
        request == NULL || recipient == NULL) {
        return false;
    }

    size_t match = 0U;

    for (size_t i = 0U; i < selection->route_table->count; ++i) {
        const enp_route_entry_t *entry = &selection->route_table->entries[i];

        if (entry->state != ENP_ROUTE_STATE_ACTIVE) {
            continue;
        }

        const enp_route_destination_t local = {
            .network_id = (enp_route_network_id_t)selection->local_address.network,
            .node_id = (enp_route_node_id_t)selection->local_address.node,
        };

        if (!destination_equal(entry->next_hop, local)) {
            continue;
        }

        if (destination_equal(entry->destination, request->unreachable) ||
            destination_equal(entry->destination, local)) {
            continue;
        }

        if (match == index) {
            *recipient = entry->destination;
            return true;
        }

        ++match;
    }

    return false;
}
