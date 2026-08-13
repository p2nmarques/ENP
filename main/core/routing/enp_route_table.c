#include "enp_route_table.h"

#include <string.h>

static bool destination_equal(
    enp_route_destination_t lhs,
    enp_route_destination_t rhs)
{
    return lhs.network_id == rhs.network_id &&
           lhs.node_id == rhs.node_id;
}

static bool metric_is_valid(const enp_route_metric_t *metric)
{
    return metric != NULL &&
           metric->valid &&
           metric->type == ENP_ROUTE_METRIC_HOP_COUNT;
}

static bool entry_is_valid(const enp_route_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    if (entry->destination.network_id == 0U &&
        entry->destination.node_id == 0U) {
        return false;
    }

    if (!metric_is_valid(&entry->metric)) {
        return false;
    }

    if (entry->state != ENP_ROUTE_STATE_ACTIVE &&
        entry->state != ENP_ROUTE_STATE_STALE) {
        return false;
    }

    return true;
}

static int find_index(
    const enp_route_table_t *table,
    enp_route_destination_t destination)
{
    for (size_t i = 0U; i < table->count; ++i) {
        if (destination_equal(
                table->entries[i].destination,
                destination)) {
            return (int)i;
        }
    }

    return -1;
}

bool enp_route_table_init(enp_route_table_t *table)
{
    if (table == NULL) {
        return false;
    }

    memset(table, 0, sizeof(*table));

    for (size_t i = 0U; i < ENP_MAX_ROUTES; ++i) {
        table->entries[i].state = ENP_ROUTE_STATE_INVALID;
    }

    return true;
}

enp_route_entry_t *enp_route_table_lookup(
    enp_route_table_t *table,
    enp_route_destination_t destination)
{
    if (table == NULL) {
        return NULL;
    }

    int index = find_index(table, destination);

    if (index < 0) {
        return NULL;
    }

    if (table->entries[index].state != ENP_ROUTE_STATE_ACTIVE) {
        return NULL;
    }

    return &table->entries[index];
}

const enp_route_entry_t *enp_route_table_lookup_const(
    const enp_route_table_t *table,
    enp_route_destination_t destination)
{
    if (table == NULL) {
        return NULL;
    }

    int index = find_index(table, destination);

    if (index < 0) {
        return NULL;
    }

    if (table->entries[index].state != ENP_ROUTE_STATE_ACTIVE) {
        return NULL;
    }

    return &table->entries[index];
}

bool enp_route_table_insert(
    enp_route_table_t *table,
    const enp_route_entry_t *entry)
{
    if (table == NULL || !entry_is_valid(entry)) {
        return false;
    }

    if (find_index(table, entry->destination) >= 0) {
        return false;
    }

    if (table->count >= ENP_MAX_ROUTES) {
        return false;
    }

    table->entries[table->count] = *entry;
    ++table->count;

    return true;
}

bool enp_route_table_update(
    enp_route_table_t *table,
    const enp_route_entry_t *entry)
{
    if (table == NULL || !entry_is_valid(entry)) {
        return false;
    }

    int index = find_index(table, entry->destination);

    if (index < 0) {
        return false;
    }

    table->entries[index] = *entry;

    return true;
}

bool enp_route_table_invalidate(
    enp_route_table_t *table,
    enp_route_destination_t destination)
{
    if (table == NULL) {
        return false;
    }

    int index = find_index(table, destination);

    if (index < 0) {
        return false;
    }

    table->entries[index].state = ENP_ROUTE_STATE_STALE;

    return true;
}

bool enp_route_table_remove(
    enp_route_table_t *table,
    enp_route_destination_t destination)
{
    if (table == NULL) {
        return false;
    }

    int index = find_index(table, destination);

    if (index < 0) {
        return false;
    }

    size_t last = table->count - 1U;

    if ((size_t)index != last) {
        table->entries[index] = table->entries[last];
    }

    memset(&table->entries[last], 0, sizeof(table->entries[last]));
    table->entries[last].state = ENP_ROUTE_STATE_INVALID;

    --table->count;

    return true;
}

size_t enp_route_table_expire(
    enp_route_table_t *table,
    uint32_t now_ms)
{
    if (table == NULL) {
        return 0U;
    }

    size_t expired = 0U;

    for (size_t i = 0U; i < table->count; ++i) {
        enp_route_entry_t *entry = &table->entries[i];

        if (entry->state != ENP_ROUTE_STATE_ACTIVE) {
            continue;
        }

        /*
         * Unsigned subtraction gives correct elapsed-time semantics across
         * a 32-bit millisecond clock wrap, provided route lifetimes remain
         * below 2^31 ms.
         */
        if ((int32_t)(now_ms - entry->expires_at_ms) >= 0) {
            entry->state = ENP_ROUTE_STATE_STALE;
            ++expired;
        }
    }

    return expired;
}

size_t enp_route_table_count(
    const enp_route_table_t *table)
{
    return table != NULL ? table->count : 0U;
}

size_t enp_route_table_active_count(
    const enp_route_table_t *table)
{
    if (table == NULL) {
        return 0U;
    }

    size_t active = 0U;

    for (size_t i = 0U; i < table->count; ++i) {
        if (table->entries[i].state == ENP_ROUTE_STATE_ACTIVE) {
            ++active;
        }
    }

    return active;
}
