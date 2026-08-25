/*
 * E3.3.7 Phase 4 / P4-E5E Gate 2
 * Reliability REPAIR_PENDING controlled regression
 *
 * E3.3.7_p4_E5E_gate2_reliability_controlled_main.c
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 *
 * Purpose:
 *   Validate the modified Reliability core in isolation before E5E
 *   integration. No ESP-NOW, routing, E5D, or hardware is involved.
 *
 * Baseline:
 *   P4-E5D Step-3 frozen source.
 * Change under test:
 *   Reliability REPAIR_PENDING state and repair control API.
 * Target: ESP-IDF 6.0.2
 */

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

#include "core/protocol/payloads/enp_ack.h"
#include "core/protocol/payloads/enp_data.h"
#include "core/reliability/enp_reliability.h"

static const char *TAG = "E3_3_7_P4_E5E_G2";

#define TEST_NETWORK_ID 1U
#define TEST_ORIGIN_NODE 1U
#define TEST_DEST_NODE 3U
#define TEST_DATA_SEQUENCE 0x7201U
#define TEST_APP_SEQUENCE 0x0052U
#define TEST_ACK_SEQUENCE 0x9201U

#define REPAIR_A ((enp_reliability_repair_id_t)0xE5E20001U)
#define REPAIR_B ((enp_reliability_repair_id_t)0xE5E20002U)

static unsigned s_submit_count;
static unsigned s_result_count;
static enp_reliability_result_t s_last_result;
static enp_reliability_handle_t s_last_result_handle;
static enp_packet_t s_last_submitted;

static esp_err_t test_submit(const enp_packet_t *packet, void *context)
{
    (void)context;

    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ++s_submit_count;
    s_last_submitted = *packet;
    return ESP_OK;
}

static void test_result(enp_reliability_handle_t handle,
                        enp_reliability_result_t result,
                        void *context)
{
    (void)context;

    ++s_result_count;
    s_last_result_handle = handle;
    s_last_result = result;
}

static bool make_data(enp_packet_t *packet)
{
    const enp_address_t origin = {
        .network = TEST_NETWORK_ID,
        .node = TEST_ORIGIN_NODE,
    };
    const enp_address_t destination = {
        .network = TEST_NETWORK_ID,
        .node = TEST_DEST_NODE,
    };
    static const uint8_t payload[] = "E5E-GATE2";

    enp_packet_init(packet, ENP_PACKET_APPLICATION, &origin);

    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) {
        return false;
    }

    header->destination = destination;
    header->flags = ENP_FLAG_ACK_REQUIRED;
    header->sequence = TEST_DATA_SEQUENCE;

    enp_data_header_t *data_header =
        (enp_data_header_t *)enp_packet_payload(packet);

    enp_data_header_init(data_header,
                         ENP_DATA_SUBTYPE_APPLICATION,
                         ENP_DATA_FLAG_NONE,
                         TEST_APP_SEQUENCE,
                         (uint16_t)sizeof(payload));

    memcpy((uint8_t *)data_header + ENP_DATA_HEADER_SIZE,
           payload,
           sizeof(payload));

    return enp_packet_seal(packet,
                           (uint16_t)(ENP_DATA_HEADER_SIZE + sizeof(payload)))
           == ESP_OK;
}

static bool make_ack(enp_packet_t *packet)
{
    const enp_address_t origin = {
        .network = TEST_NETWORK_ID,
        .node = TEST_DEST_NODE,
    };
    const enp_address_t destination = {
        .network = TEST_NETWORK_ID,
        .node = TEST_ORIGIN_NODE,
    };

    enp_packet_init(packet, ENP_PACKET_ACK, &origin);

    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) {
        return false;
    }

    header->destination = destination;
    header->sequence = TEST_ACK_SEQUENCE;

    enp_ack_payload_t *ack =
        (enp_ack_payload_t *)enp_packet_payload(packet);

    enp_ack_payload_init(ack, TEST_DATA_SEQUENCE, TEST_APP_SEQUENCE);

    return enp_packet_seal(packet, ENP_ACK_WIRE_SIZE) == ESP_OK;
}

static bool expect_state(enp_reliability_handle_t handle,
                         enp_reliability_state_t expected)
{
    enp_reliability_state_t state = ENP_RELIABILITY_STATE_INVALID;

    return enp_reliability_get_state(handle, &state) && state == expected;
}

static bool expect_retry_count(enp_reliability_handle_t handle,
                               uint8_t expected)
{
    uint8_t count = 0U;

    return enp_reliability_get_retry_count(handle, &count) && count == expected;
}

static void reset_observations(void)
{
    s_submit_count = 0U;
    s_result_count = 0U;
    s_last_result = ENP_RELIABILITY_RESULT_NONE;
    s_last_result_handle = ENP_RELIABILITY_INVALID_HANDLE;
    memset(&s_last_submitted, 0, sizeof(s_last_submitted));
}

static bool test_normal_regression(void)
{
    enp_packet_t data;
    enp_packet_t ack;
    enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;

    reset_observations();

    if (!make_data(&data) || !make_ack(&ack)) {
        return false;
    }

    if (!enp_reliability_send(&data, 1000U, &handle) ||
        handle == ENP_RELIABILITY_INVALID_HANDLE ||
        s_submit_count != 1U ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
        return false;
    }

    if (memcmp(&data, &s_last_submitted, sizeof(data)) != 0) {
        return false;
    }

    enp_reliability_tick(1999U);
    if (s_submit_count != 1U) {
        return false;
    }

    enp_reliability_tick(2000U);
    if (s_submit_count != 2U || !expect_retry_count(handle, 1U) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
        return false;
    }

    if (!enp_reliability_process_ack(&ack, 2050U) ||
        s_result_count != 1U ||
        s_last_result_handle != handle ||
        s_last_result != ENP_RELIABILITY_RESULT_DELIVERED) {
        return false;
    }

    if (enp_reliability_process_ack(&ack, 2100U) ||
        s_result_count != 1U) {
        return false;
    }

    ESP_LOGI(TAG, "PASS: existing DATA / ACK / retry regression");
    return true;
}

static bool test_repair_pending(void)
{
    enp_packet_t data;
    enp_packet_t ack;
    enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;

    reset_observations();

    if (!make_data(&data) || !make_ack(&ack) ||
        !enp_reliability_send(&data, 3000U, &handle)) {
        return false;
    }

    if (!enp_reliability_begin_repair(handle, REPAIR_A) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
        !expect_retry_count(handle, 0U)) {
        return false;
    }

    const unsigned submits_before_tick = s_submit_count;

    enp_reliability_tick(10000U);

    if (s_submit_count != submits_before_tick ||
        !expect_retry_count(handle, 0U) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING)) {
        return false;
    }

    if (enp_reliability_process_ack(&ack, 10001U) ||
        s_result_count != 0U ||
        !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING)) {
        return false;
    }

    ESP_LOGI(TAG, "PASS: REPAIR_PENDING suppresses timeout/retry and ACK completion");
    return true;
}

static bool test_repair_identity_and_resume(void)
{
    enp_packet_t data;
    enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;

    reset_observations();

    if (!make_data(&data) || !enp_reliability_send(&data, 20000U, &handle) ||
        !enp_reliability_begin_repair(handle, REPAIR_A)) {
        return false;
    }

    if (enp_reliability_repair_result(handle, REPAIR_B, true, 21000U) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
        !expect_retry_count(handle, 0U)) {
        return false;
    }

    if (!enp_reliability_repair_result(handle, REPAIR_A, true, 21000U) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        !expect_retry_count(handle, 1U) ||
        s_submit_count != 2U) {
        return false;
    }

    if (memcmp(&data, &s_last_submitted, sizeof(data)) != 0) {
        return false;
    }

    /* The successful repair resumes through the normal Reliability retry
     * path. The new ACK deadline is relative to the repair completion time. */
    enp_reliability_tick(21999U);
    if (s_submit_count != 2U ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        !expect_retry_count(handle, 1U)) {
        return false;
    }

    enp_reliability_tick(22000U);
    if (s_submit_count != 3U ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        !expect_retry_count(handle, 2U)) {
        return false;
    }

    /* The repair identity is cleared when the normal retransmission resumes,
     * so a stale duplicate completion cannot affect the transaction. */
    if (enp_reliability_repair_result(handle, REPAIR_A, true, 22001U) ||
        !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        s_submit_count != 3U) {
        return false;
    }

    ESP_LOGI(TAG, "PASS: repair identity validation, resume, fresh deadline and duplicate suppression");
    return true;
}

static bool test_repair_failure_and_cancel(void)
{
    enp_packet_t data;
    enp_reliability_handle_t failed_handle = ENP_RELIABILITY_INVALID_HANDLE;
    enp_reliability_handle_t cancel_handle = ENP_RELIABILITY_INVALID_HANDLE;

    reset_observations();

    if (!make_data(&data) ||
        !enp_reliability_send(&data, 30000U, &failed_handle) ||
        !enp_reliability_begin_repair(failed_handle, REPAIR_A) ||
        !enp_reliability_repair_result(failed_handle, REPAIR_A, false, 30100U) ||
        s_result_count != 1U ||
        s_last_result_handle != failed_handle ||
        s_last_result != ENP_RELIABILITY_RESULT_FAILED) {
        return false;
    }

    if (!enp_reliability_send(&data, 31000U, &cancel_handle) ||
        !enp_reliability_begin_repair(cancel_handle, REPAIR_A) ||
        !enp_reliability_cancel(cancel_handle) ||
        s_result_count != 2U ||
        s_last_result_handle != cancel_handle ||
        s_last_result != ENP_RELIABILITY_RESULT_CANCELLED) {
        return false;
    }

    /* The handle is slot-derived and can be reused. An old repair completion
     * must not affect the newly created transaction using the same handle. */
    enp_reliability_handle_t reused_handle = ENP_RELIABILITY_INVALID_HANDLE;
    if (!enp_reliability_send(&data, 32000U, &reused_handle) ||
        reused_handle != cancel_handle ||
        !enp_reliability_begin_repair(reused_handle, REPAIR_B) ||
        !expect_state(reused_handle, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
        enp_reliability_repair_result(reused_handle, REPAIR_A, true, 32100U) ||
        !expect_state(reused_handle, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
        !expect_retry_count(reused_handle, 0U) ||
        s_result_count != 2U) {
        return false;
    }

    ESP_LOGI(TAG, "PASS: repair failure, cancellation and stale completion isolation");
    return true;
}

static bool test_multiple_transactions_one_repair(void)
{
    enp_packet_t data;
    enp_reliability_handle_t a = ENP_RELIABILITY_INVALID_HANDLE;
    enp_reliability_handle_t b = ENP_RELIABILITY_INVALID_HANDLE;

    reset_observations();

    if (!make_data(&data) ||
        !enp_reliability_send(&data, 40000U, &a) ||
        !enp_reliability_send(&data, 40000U, &b) ||
        a == b ||
        !enp_reliability_begin_repair(a, REPAIR_B) ||
        !enp_reliability_begin_repair(b, REPAIR_B)) {
        return false;
    }

    if (!expect_state(a, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
        !expect_state(b, ENP_RELIABILITY_STATE_REPAIR_PENDING)) {
        return false;
    }

    if (!enp_reliability_repair_result(a, REPAIR_B, true, 40100U) ||
        !enp_reliability_repair_result(b, REPAIR_B, true, 40100U) ||
        !expect_state(a, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        !expect_state(b, ENP_RELIABILITY_STATE_WAITING_FOR_ACK) ||
        !expect_retry_count(a, 1U) ||
        !expect_retry_count(b, 1U)) {
        return false;
    }

    ESP_LOGI(TAG, "PASS: multiple Reliability transactions attach to one repair ID");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5E GATE 2");
    ESP_LOGI(TAG, "Reliability REPAIR_PENDING controlled regression");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "No E5D / E5E / routing / ESP-NOW / hardware");
    ESP_LOGI(TAG, "======================================");

    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability initialization");
        return;
    }

    bool pass = true;

    /* Existing core self-test is the first regression baseline. */
    if (!enp_reliability_self_test()) {
        ESP_LOGE(TAG, "FAIL: existing Reliability self-test regression");
        pass = false;
    } else {
        ESP_LOGI(TAG, "PASS: existing Reliability self-test regression");
    }

    /* Reinitialize because the core self-test owns its own lifecycle. */
    enp_reliability_deinit();

    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability reinitialization after baseline");
        return;
    }

    if (!test_normal_regression()) {
        ESP_LOGE(TAG, "FAIL: normal Reliability regression");
        pass = false;
    }

    enp_reliability_deinit();
    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability reinitialization");
        return;
    }

    if (!test_repair_pending()) {
        ESP_LOGE(TAG, "FAIL: REPAIR_PENDING suppression");
        pass = false;
    }

    enp_reliability_deinit();
    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability reinitialization");
        return;
    }

    if (!test_repair_identity_and_resume()) {
        ESP_LOGE(TAG, "FAIL: repair identity/resume");
        pass = false;
    }

    enp_reliability_deinit();
    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability reinitialization");
        return;
    }

    if (!test_repair_failure_and_cancel()) {
        ESP_LOGE(TAG, "FAIL: repair failure/cancellation");
        pass = false;
    }

    enp_reliability_deinit();
    if (!enp_reliability_init() ||
        !enp_reliability_set_submit_callback(test_submit, NULL) ||
        !enp_reliability_set_result_callback(test_result, NULL) ||
        !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability reinitialization");
        return;
    }

    if (!test_multiple_transactions_one_repair()) {
        ESP_LOGE(TAG, "FAIL: multiple transactions / one repair");
        pass = false;
    }

    enp_reliability_deinit();

    ESP_LOGI(TAG, "--------------------------------------");
    if (pass) {
        ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5E Gate 2 PASS");
    } else {
        ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5E Gate 2 FAIL");
    }
    ESP_LOGI(TAG, "======================================");
}
