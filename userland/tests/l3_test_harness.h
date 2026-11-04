/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PX4_USERLAND_L3_TEST_HARNESS_H
#define PX4_USERLAND_L3_TEST_HARNESS_H

#include <dtv/l3_owner.h>

#ifdef __cplusplus
extern "C" {
#endif

struct px4_l3_test_harness;

struct px4_l3_test_capabilities {
    unsigned int bridge_count;
    unsigned int receiver_count;
    int dual_system;
    int supports_lnb_15v;
    int has_card_reader;
    int cancel_scope;
};

struct px4_l3_test_harness* px4_l3_test_create(unsigned int claim_count,
                                               int fail_claim_index,
                                               int dual_receiver);
const dtv_l3_provider* px4_l3_test_provider(
    struct px4_l3_test_harness* harness);
struct px4_l3_test_capabilities px4_l3_test_capabilities(
    struct px4_l3_test_harness* harness);

unsigned int px4_l3_test_claim_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_claim_successes(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_release_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_release_at(struct px4_l3_test_harness* harness,
                                     unsigned int index);
unsigned int px4_l3_test_tune_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_open_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_close_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_start_capture_calls(
    struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_stop_capture_calls(
    struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_last_receiver(struct px4_l3_test_harness* harness);
unsigned long long px4_l3_test_last_hz(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_submit_calls(struct px4_l3_test_harness* harness);
unsigned int px4_l3_test_cancel_calls(struct px4_l3_test_harness* harness);
int px4_l3_test_destroy(struct px4_l3_test_harness* harness);

#ifdef __cplusplus
}
#endif

#endif  /* PX4_USERLAND_L3_TEST_HARNESS_H */
