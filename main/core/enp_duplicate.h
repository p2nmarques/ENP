/*
 * enp_transport.h
 *
 *  Created on: Aug 10, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_duplicate.h
 *
 * @brief ENP duplicate-packet cache.
 *
 * The duplicate cache records recently processed packets using
 * the originating logical source address and packet sequence
 * number. It is transport-independent.
 */

#ifndef ENP_DUPLICATE_H
#define ENP_DUPLICATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "core/enp_address.h"
#include "core/enp_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*----------------------------------------------------------
 * Configuration
 *---------------------------------------------------------*/

/**
 * @brief Maximum number of recently seen packets retained.
 */
#define ENP_DUPLICATE_CACHE_SIZE        32U

/**
 * @brief Lifetime of a duplicate-cache entry in milliseconds.
 */
#define ENP_DUPLICATE_CACHE_TIMEOUT_MS  10000U

/*----------------------------------------------------------
 * Duplicate Entry
 *---------------------------------------------------------*/

/**
 * @brief One recently observed packet identity.
 *
 * The transport address is deliberately not stored here.
 * Duplicate identity is based on the originating logical
 * ENP address and the packet sequence number.
 */
typedef struct
{
    enp_address_t source;
    enp_sequence_t sequence;
    uint32_t seen_at_ms;
    bool valid;

} enp_duplicate_entry_t;

/*----------------------------------------------------------
 * Duplicate Cache
 *---------------------------------------------------------*/

/**
 * @brief ENP duplicate cache.
 *
 * The cache uses only statically allocated storage. Its mutex
 * is also statically allocated so that the module does not
 * require heap allocation.
 */
typedef struct
{
    enp_duplicate_entry_t entries[
            ENP_DUPLICATE_CACHE_SIZE];

    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;

} enp_duplicate_cache_t;

/*----------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------*/

/**
 * @brief Initialize a duplicate cache.
 *
 * @param cache Duplicate cache.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for a NULL cache.
 * @return ESP_FAIL if the static mutex cannot be created.
 */
esp_err_t enp_duplicate_cache_init(
        enp_duplicate_cache_t *cache);

/**
 * @brief Clear all duplicate-cache entries.
 *
 * @param cache Duplicate cache.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for a NULL cache.
 * @return ESP_ERR_INVALID_STATE if the cache is not initialized.
 */
esp_err_t enp_duplicate_cache_clear(
        enp_duplicate_cache_t *cache);

/*----------------------------------------------------------
 * Duplicate Detection
 *---------------------------------------------------------*/

/**
 * @brief Check and record a packet identity.
 *
 * If the source/sequence pair is already present and its entry
 * has not expired, @p duplicate is set to true and the cache is
 * not changed.
 *
 * If the packet is new, an entry is recorded and @p duplicate
 * is set to false.
 *
 * Expired entries are discarded during the operation.
 * If the cache is full, the oldest valid entry is replaced.
 *
 * Time comparison is performed using unsigned 32-bit elapsed
 * time arithmetic, allowing the millisecond clock to wrap.
 *
 * @param cache Duplicate cache.
 * @param source Originating ENP logical address.
 * @param sequence Originating packet sequence number.
 * @param now_ms Current monotonic ENP time in milliseconds.
 * @param duplicate Output flag indicating whether the packet
 *                  was already seen within the cache lifetime.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 * @return ESP_ERR_INVALID_STATE if the cache is not initialized.
 */
esp_err_t enp_duplicate_check_and_record(
        enp_duplicate_cache_t *cache,
        const enp_address_t *source,
        enp_sequence_t sequence,
        uint32_t now_ms,
        bool *duplicate);

/*----------------------------------------------------------
 * Information
 *---------------------------------------------------------*/

/**
 * @brief Return the number of currently valid cache entries.
 *
 * @param cache Duplicate cache.
 *
 * @param now_ms Current monotonic ENP time in milliseconds.
 *
 * @return Number of non-expired entries.
 * @return Zero for invalid/uninitialized caches.
 */
size_t enp_duplicate_count(
        enp_duplicate_cache_t *cache,
        uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* ENP_DUPLICATE_H */
