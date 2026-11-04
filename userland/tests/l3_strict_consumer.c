/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include <dtv/l3_owner.h>

#include "l3_test_harness.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void sleep_ms(unsigned int milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    nanosleep(&delay, NULL);
}

static int open_ready(const dtv_l3_provider* provider, dtv_l3_context** owner,
                      dtv_l3_result* open_result)
{
    dtv_l3_open_result opened = dtv_l3_open(provider, 1U);
    dtv_l3_op_result result;
    *owner = NULL;
    *open_result = opened.result;
    if (opened.result != DTV_L3_ACCEPTED || opened.owner == NULL ||
        opened.op == NULL) {
        return 0;
    }
    if (dtv_l3_op_wait(opened.op, 2000U) != DTV_L3_OK ||
        dtv_l3_op_poll(opened.op, &result) != DTV_L3_OK) {
        return 0;
    }
    *open_result = result.result;
    *owner = opened.owner;
    return dtv_l3_op_release(opened.op) == DTV_L3_OK;
}

static int wait_releasable(dtv_l3_context* owner, dtv_l3_close_status* status)
{
    unsigned int attempt;
    for (attempt = 0U; attempt < 2000U; ++attempt) {
        if (dtv_l3_close_status_get(owner, status) != DTV_L3_OK) {
            return 0;
        }
        if (status->releasable) {
            return 1;
        }
        sleep_ms(1U);
    }
    return 0;
}

static int close_and_release(dtv_l3_context* owner)
{
    dtv_l3_close_status status;
    if (dtv_l3_close(owner, 2000U) != DTV_L3_OK) {
        return 0;
    }
    if (!wait_releasable(owner, &status)) {
        return 0;
    }
    if (!status.physical_quiesced || !status.all_ops_recovered ||
        !status.cleanup_complete) {
        return 0;
    }
    return dtv_l3_context_release(owner) == DTV_L3_OK;
}

static int run_ready_stream_and_close(void)
{
    struct px4_l3_test_harness* harness = px4_l3_test_create(1U, -1, -1);
    const dtv_l3_provider* provider;
    struct px4_l3_test_capabilities capabilities;
    dtv_l3_context* owner = NULL;
    dtv_l3_result open_result = DTV_L3_FAILED;
    dtv_l3_open_status open_status;
    dtv_l3_lease* lease = NULL;
    dtv_l3_stream* stream = NULL;
    dtv_l3_lease_status lease_status;
    uint64_t programmed = 0U;
    uint64_t revision = 0U;
    uint8_t buffer[188U * 8U];
    size_t bytes = 0U;
    dtv_l3_read_state read_state = DTV_L3_READ_WAIT;

    CHECK(harness != NULL);
    provider = px4_l3_test_provider(harness);
    CHECK(provider != NULL);
    capabilities = px4_l3_test_capabilities(harness);
    CHECK(capabilities.bridge_count == 1U);
    CHECK(capabilities.receiver_count == 4U);
    CHECK(capabilities.dual_system == 0);
    CHECK(capabilities.supports_lnb_15v == 1);
    CHECK(capabilities.has_card_reader == 1);
    CHECK(capabilities.cancel_scope == (int)DTV_L3_CANCEL_SCOPE_PIPE);

    CHECK(open_ready(provider, &owner, &open_result));
    CHECK(open_result == DTV_L3_OK);
    CHECK(owner != NULL);
    CHECK(dtv_l3_open_status_get(owner, &open_status) == DTV_L3_OK);
    CHECK(open_status.state == DTV_L3_OPEN_READY);
    CHECK(open_status.claims_acquired == 1U);

    CHECK(dtv_l3_lease_acquire(owner, 2U, &lease) == DTV_L3_OK);
    CHECK(lease != NULL);
    CHECK(dtv_l3_lease_tune(lease, 527143000ULL, &programmed, &revision) ==
          DTV_L3_OK);
    CHECK(programmed == 527143000ULL);
    CHECK(revision == 1U);
    CHECK(px4_l3_test_tune_calls(harness) == 1U);
    CHECK(px4_l3_test_last_receiver(harness) == 2U);
    CHECK(px4_l3_test_last_hz(harness) == 527143000ULL);
    CHECK(dtv_l3_lease_status_get(lease, &lease_status) == DTV_L3_OK);
    CHECK(lease_status.receiver_id == 2U);
    CHECK(lease_status.bridge_id == 0U);

    CHECK(dtv_l3_lease_start(lease) == DTV_L3_OK);
    CHECK(dtv_l3_lease_attach(lease, &stream) == DTV_L3_OK);
    CHECK(stream != NULL);
    CHECK(px4_l3_test_start_capture_calls(harness) == 1U);

    CHECK(dtv_l3_stream_read_ts_wait(stream, buffer, sizeof(buffer), &bytes,
                                     &read_state, 2000U) == DTV_L3_OK);
    CHECK(bytes >= 188U);
    CHECK(read_state == DTV_L3_READ_DATA);
    CHECK(buffer[0] == 0x47U);

    CHECK(dtv_l3_stream_stop(stream, 2000U) == DTV_L3_OK);
    CHECK(dtv_l3_stream_release(stream, DTV_L3_STREAM_RELEASE_DISCARD_UNREAD) ==
          DTV_L3_OK);
    CHECK(dtv_l3_stream_destroy(stream) == DTV_L3_OK);
    CHECK(dtv_l3_lease_release(lease) == DTV_L3_OK);

    CHECK(close_and_release(owner));
    CHECK(px4_l3_test_release_calls(harness) == 1U);
    CHECK(px4_l3_test_release_at(harness, 0U) == 0U);
    CHECK(px4_l3_test_stop_capture_calls(harness) == 1U);
    CHECK(px4_l3_test_close_calls(harness) == 1U);
    CHECK(px4_l3_test_destroy(harness) == 0);
    return 0;
}

static int run_partial_claim_failure(void)
{
    struct px4_l3_test_harness* harness = px4_l3_test_create(3U, 1, -1);
    const dtv_l3_provider* provider;
    dtv_l3_open_result opened;
    dtv_l3_op_result result;
    dtv_l3_open_status status;

    CHECK(harness != NULL);
    provider = px4_l3_test_provider(harness);
    opened = dtv_l3_open(provider, 3U);
    CHECK(opened.result == DTV_L3_ACCEPTED);
    CHECK(opened.owner != NULL);
    CHECK(opened.op != NULL);
    CHECK(dtv_l3_op_wait(opened.op, 2000U) == DTV_L3_OK);
    CHECK(dtv_l3_op_poll(opened.op, &result) == DTV_L3_OK);
    CHECK(result.state == DTV_L3_OP_FAILED);
    CHECK(dtv_l3_op_release(opened.op) == DTV_L3_OK);
    CHECK(px4_l3_test_claim_calls(harness) == 2U);
    CHECK(px4_l3_test_claim_successes(harness) == 1U);
    CHECK(dtv_l3_open_status_get(opened.owner, &status) == DTV_L3_OK);
    CHECK(status.state == DTV_L3_OPEN_FAILED);
    CHECK(status.claims_attempted == 2U);
    CHECK(close_and_release(opened.owner));
    CHECK(px4_l3_test_release_calls(harness) == 2U);
    CHECK(px4_l3_test_release_at(harness, 0U) == 1U);
    CHECK(px4_l3_test_release_at(harness, 1U) == 0U);
    CHECK(px4_l3_test_destroy(harness) == 0);
    return 0;
}

static int run_dual_system_is_not_guessed(void)
{
    struct px4_l3_test_harness* harness = px4_l3_test_create(1U, -1, 1);
    const dtv_l3_provider* provider;
    dtv_l3_context* owner = NULL;
    dtv_l3_result open_result = DTV_L3_FAILED;
    dtv_l3_lease* lease = NULL;
    uint64_t programmed = 0U;
    uint64_t revision = 0U;

    CHECK(harness != NULL);
    provider = px4_l3_test_provider(harness);
    CHECK(open_ready(provider, &owner, &open_result));
    CHECK(open_result == DTV_L3_OK);
    CHECK(dtv_l3_lease_acquire(owner, 1U, &lease) == DTV_L3_OK);
    CHECK(dtv_l3_lease_tune(lease, 1100000000ULL, &programmed, &revision) ==
          DTV_L3_UNSUPPORTED);
    CHECK(px4_l3_test_tune_calls(harness) == 0U);
    CHECK(dtv_l3_lease_release(lease) == DTV_L3_OK);
    CHECK(close_and_release(owner));
    CHECK(px4_l3_test_destroy(harness) == 0);
    return 0;
}

static int run_transfer_and_pipe_scope(void)
{
    struct px4_l3_test_harness* harness = px4_l3_test_create(1U, -1, -1);
    const dtv_l3_provider* provider;
    dtv_l3_context* owner = NULL;
    dtv_l3_result open_result = DTV_L3_FAILED;
    dtv_l3_transfer_request request;
    dtv_l3_op* op = NULL;
    dtv_l3_op_result result;

    CHECK(harness != NULL);
    provider = px4_l3_test_provider(harness);
    CHECK(open_ready(provider, &owner, &open_result));
    CHECK(open_result == DTV_L3_OK);
    memset(&request, 0, sizeof(request));
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = 0x81U;
    request.timeout_ms = 100U;
    request.claim_index = 0U;
    request.in_capacity = 4U;
    CHECK(dtv_l3_transfer_start(owner, &request, &op) == DTV_L3_ACCEPTED);
    CHECK(op != NULL);
    CHECK(dtv_l3_op_wait(op, 2000U) == DTV_L3_OK);
    CHECK(dtv_l3_op_poll(op, &result) == DTV_L3_OK);
    CHECK(result.state == DTV_L3_OP_DONE);
    CHECK(result.result == DTV_L3_OK);
    CHECK(result.data_size == 4U);
    CHECK(px4_l3_test_submit_calls(harness) == 1U);
    CHECK(dtv_l3_op_release(op) == DTV_L3_OK);
    CHECK(close_and_release(owner));
    CHECK(px4_l3_test_destroy(harness) == 0);
    return 0;
}

int main(void)
{
    if (run_ready_stream_and_close() != 0) {
        return 1;
    }
    if (run_partial_claim_failure() != 0) {
        return 1;
    }
    if (run_dual_system_is_not_guessed() != 0) {
        return 1;
    }
    if (run_transfer_and_pipe_scope() != 0) {
        return 1;
    }
    return 0;
}
