/**
 * @file enp_duplicate.c
 *
 * @brief ENP duplicate-packet cache implementation.
 */

#include "enp_duplicate.h"

#include <string.h>

/*----------------------------------------------------------
 * Internal Helpers
 *---------------------------------------------------------*/

static bool enp_duplicate_lock(enp_duplicate_cache_t *cache) {
	return (cache != NULL) && (cache->mutex != NULL) &&
		   (xSemaphoreTake(cache->mutex, portMAX_DELAY) == pdTRUE);
}

static void enp_duplicate_unlock(enp_duplicate_cache_t *cache) {
	if ((cache != NULL) && (cache->mutex != NULL)) {
		(void)xSemaphoreGive(cache->mutex);
	}
}

static bool enp_duplicate_expired(const enp_duplicate_entry_t *entry,
								  uint32_t now_ms) {
	if ((entry == NULL) || !entry->valid) {
		return true;
	}

	return ((uint32_t)(now_ms - entry->seen_at_ms) >=
			ENP_DUPLICATE_CACHE_TIMEOUT_MS);
}

static void enp_duplicate_expire_entries(enp_duplicate_cache_t *cache,
										 uint32_t now_ms) {
	for (size_t index = 0U; index < ENP_DUPLICATE_CACHE_SIZE; ++index) {
		if (enp_duplicate_expired(&cache->entries[index], now_ms)) {
			cache->entries[index].valid = false;
		}
	}
}

static size_t enp_duplicate_find(const enp_duplicate_cache_t *cache,
								 const enp_address_t *source,
								 enp_sequence_t sequence) {
	for (size_t index = 0U; index < ENP_DUPLICATE_CACHE_SIZE; ++index) {
		const enp_duplicate_entry_t *entry = &cache->entries[index];

		if (!entry->valid) {
			continue;
		}

		if ((entry->sequence == sequence) &&
			enp_address_equal(&entry->source, source)) {
			return index;
		}
	}

	return ENP_DUPLICATE_CACHE_SIZE;
}

static size_t enp_duplicate_find_free(const enp_duplicate_cache_t *cache) {
	for (size_t index = 0U; index < ENP_DUPLICATE_CACHE_SIZE; ++index) {
		if (!cache->entries[index].valid) {
			return index;
		}
	}

	return ENP_DUPLICATE_CACHE_SIZE;
}

static size_t enp_duplicate_find_oldest(const enp_duplicate_cache_t *cache,
										uint32_t now_ms) {
	size_t oldest_index = 0U;
	uint32_t oldest_age = 0U;

	for (size_t index = 0U; index < ENP_DUPLICATE_CACHE_SIZE; ++index) {
		const enp_duplicate_entry_t *entry = &cache->entries[index];

		const uint32_t age = (uint32_t)(now_ms - entry->seen_at_ms);

		if (index == 0U || age > oldest_age) {
			oldest_index = index;
			oldest_age = age;
		}
	}

	return oldest_index;
}

/*----------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------*/

esp_err_t enp_duplicate_cache_init(enp_duplicate_cache_t *cache) {
	if (cache == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	memset(cache, 0, sizeof(*cache));

	cache->mutex = xSemaphoreCreateMutexStatic(&cache->mutex_storage);

	if (cache->mutex == NULL) {
		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t enp_duplicate_cache_clear(enp_duplicate_cache_t *cache) {
	if (cache == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!enp_duplicate_lock(cache)) {
		return ESP_ERR_INVALID_STATE;
	}

	memset(cache->entries, 0, sizeof(cache->entries));

	enp_duplicate_unlock(cache);

	return ESP_OK;
}

/*----------------------------------------------------------
 * Duplicate Detection
 *---------------------------------------------------------*/

esp_err_t enp_duplicate_check_and_record(enp_duplicate_cache_t *cache,
										 const enp_address_t *source,
										 enp_sequence_t sequence,
										 uint32_t now_ms, bool *duplicate) {
	if ((cache == NULL) || (source == NULL) || (duplicate == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	*duplicate = false;

	if (!enp_duplicate_lock(cache)) {
		return ESP_ERR_INVALID_STATE;
	}

	/*
	 * Remove entries that are outside the duplicate window
	 * before checking the cache.
	 */
	enp_duplicate_expire_entries(cache, now_ms);

	const size_t existing_index = enp_duplicate_find(cache, source, sequence);

	if (existing_index < ENP_DUPLICATE_CACHE_SIZE) {
		*duplicate = true;

		enp_duplicate_unlock(cache);
		return ESP_OK;
	}

	size_t index = enp_duplicate_find_free(cache);

	if (index >= ENP_DUPLICATE_CACHE_SIZE) {
		index = enp_duplicate_find_oldest(cache, now_ms);
	}

	enp_duplicate_entry_t *entry = &cache->entries[index];

	entry->source = *source;
	entry->sequence = sequence;
	entry->seen_at_ms = now_ms;
	entry->valid = true;

	enp_duplicate_unlock(cache);

	return ESP_OK;
}

/*----------------------------------------------------------
 * Information
 *---------------------------------------------------------*/

size_t enp_duplicate_count(enp_duplicate_cache_t *cache, uint32_t now_ms) {
	if (cache == NULL) {
		return 0U;
	}

	if (!enp_duplicate_lock(cache)) {
		return 0U;
	}

	enp_duplicate_expire_entries(cache, now_ms);

	size_t count = 0U;

	for (size_t index = 0U; index < ENP_DUPLICATE_CACHE_SIZE; ++index) {
		if (cache->entries[index].valid) {
			++count;
		}
	}

	enp_duplicate_unlock(cache);

	return count;
}
