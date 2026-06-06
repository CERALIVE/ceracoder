/*
    ceracoder - live video encoder with dynamic bitrate control
    Copyright (C) 2020 BELABOX project
    Copyright (C) 2026 CERALIVE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
 * Unit tests for the SRT reconnect state machine (ADR-0005).
 *
 * These exercise the PURE reconnect policy with no live SRT socket or
 * GStreamer pipeline: exponential backoff schedule, transient -> reconnect ->
 * success, permanent failure / window exhaustion -> clean-exit signal, and the
 * status flag/counter health surface.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <srt.h>
#include <srt/access_control.h>

#include "srt_reconnect.h"

/*
 * Test: exponential backoff progression (1s -> 2s -> 4s -> ... capped at 30s).
 *
 * Verifies the standalone schedule helper doubles each attempt and saturates at
 * the configured cap without overflowing.
 */
static void test_backoff_exponential_progression(void **state) {
    (void)state;

    const unsigned int base = RECONNECT_DEFAULT_BASE_MS;  /* 1000 */
    const unsigned int cap = RECONNECT_DEFAULT_MAX_MS;    /* 30000 */

    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 1), 1000);
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 2), 2000);
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 3), 4000);
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 4), 8000);
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 5), 16000);
    /* 32000 would exceed the cap -> saturates at 30000 */
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 6), 30000);
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 7), 30000);
    /* A large attempt count must stay clamped, never overflow */
    assert_int_equal(reconnect_backoff_for_attempt(base, cap, 1000), 30000);
}

/*
 * Test: the live controller hands out the same exponential schedule and the
 * per-episode attempt counter advances monotonically.
 */
static void test_controller_backoff_sequence(void **state) {
    (void)state;

    ReconnectController rc;
    reconnect_init(&rc, 0, 0, RECONNECT_UNLIMITED);  /* defaults, unlimited */
    reconnect_begin(&rc);

    long expected[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        long backoff = reconnect_next_backoff_ms(&rc);
        assert_int_equal(backoff, expected[i]);
        assert_int_equal(reconnect_attempt_count(&rc), (int)(i + 1));
        assert_true(reconnect_is_reconnecting(&rc));
    }
}

/*
 * Test: transient failure -> reconnect -> success.
 *
 * A drop starts an episode; a couple of attempts back off, then a successful
 * reconnect returns to CONNECTED, resets the episode, and bumps the cumulative
 * reconnect counter (the health surface consumed by Tasks 12/13).
 */
static void test_transient_failure_then_success(void **state) {
    (void)state;

    ReconnectController rc;
    reconnect_init(&rc, 0, 0, RECONNECT_UNLIMITED);
    assert_int_equal(reconnect_get_state(&rc), RECONNECT_STATE_CONNECTED);
    assert_false(reconnect_is_reconnecting(&rc));
    assert_int_equal(reconnect_total_reconnects(&rc), 0);

    /* Transient drop -> begin reconnecting */
    reconnect_begin(&rc);
    assert_true(reconnect_is_reconnecting(&rc));

    /* First attempt backs off 1s, simulate connect failure (keep looping) */
    assert_int_equal(reconnect_next_backoff_ms(&rc), 1000);
    /* Second attempt backs off 2s, simulate connect SUCCESS */
    assert_int_equal(reconnect_next_backoff_ms(&rc), 2000);
    reconnect_succeeded(&rc);

    /* Back to healthy, episode reset, total incremented */
    assert_int_equal(reconnect_get_state(&rc), RECONNECT_STATE_CONNECTED);
    assert_false(reconnect_is_reconnecting(&rc));
    assert_int_equal(reconnect_attempt_count(&rc), 0);
    assert_int_equal(reconnect_total_reconnects(&rc), 1);

    /* A later, independent drop restarts the backoff from the base and the
     * cumulative reconnect counter keeps climbing across episodes. */
    reconnect_begin(&rc);
    assert_int_equal(reconnect_next_backoff_ms(&rc), 1000);
    reconnect_succeeded(&rc);
    assert_int_equal(reconnect_total_reconnects(&rc), 2);
}

/*
 * Test: permanent failure / window exhaustion -> clean exit.
 *
 * A bounded window of N attempts hands out exactly N backoffs, then returns -1
 * and transitions to FAILED. ceracoder maps that to a clean non-zero exit so
 * systemd (Restart=on-failure) owns the respawn (ADR-0005).
 */
static void test_window_exhaustion_signals_clean_exit(void **state) {
    (void)state;

    const int max_attempts = 3;
    ReconnectController rc;
    reconnect_init(&rc, 0, 0, max_attempts);
    reconnect_begin(&rc);

    /* Exactly max_attempts non-negative backoffs (each a failed connect) */
    for (int i = 0; i < max_attempts; i++) {
        long backoff = reconnect_next_backoff_ms(&rc);
        assert_true(backoff >= 0);
        assert_false(reconnect_get_state(&rc) == RECONNECT_STATE_FAILED);
    }

    /* Window exhausted -> -1 and FAILED (caller must clean-exit) */
    assert_int_equal(reconnect_next_backoff_ms(&rc), -1);
    assert_int_equal(reconnect_get_state(&rc), RECONNECT_STATE_FAILED);
    assert_false(reconnect_is_reconnecting(&rc));

    /* Once FAILED, the machine stays terminal */
    assert_int_equal(reconnect_next_backoff_ms(&rc), -1);
    assert_int_equal(reconnect_get_state(&rc), RECONNECT_STATE_FAILED);
}

/*
 * Test: an unlimited (transient) policy never exhausts its window, so a
 * persistently flaky link keeps being absorbed in-process rather than exiting.
 */
static void test_unlimited_never_exhausts(void **state) {
    (void)state;

    ReconnectController rc;
    reconnect_init(&rc, 0, 0, RECONNECT_UNLIMITED);
    reconnect_begin(&rc);

    for (int i = 0; i < 1000; i++) {
        long backoff = reconnect_next_backoff_ms(&rc);
        assert_true(backoff >= 0);
        assert_true(backoff <= (long)RECONNECT_DEFAULT_MAX_MS);
    }
    assert_false(reconnect_get_state(&rc) == RECONNECT_STATE_FAILED);
    assert_true(reconnect_is_reconnecting(&rc));
}

/*
 * Test: a successful reconnect inside a bounded window resets the attempt
 * budget, so a subsequent drop gets the full window again (the window bounds a
 * single uninterrupted reconnect episode, not the process lifetime).
 */
static void test_success_resets_bounded_window(void **state) {
    (void)state;

    ReconnectController rc;
    reconnect_init(&rc, 0, 0, 2);  /* tiny window: 2 attempts */
    reconnect_begin(&rc);

    /* Burn one attempt, then succeed */
    assert_int_equal(reconnect_next_backoff_ms(&rc), 1000);
    reconnect_succeeded(&rc);
    assert_int_equal(reconnect_attempt_count(&rc), 0);

    /* New episode still gets the full 2-attempt window */
    reconnect_begin(&rc);
    assert_true(reconnect_next_backoff_ms(&rc) >= 0);  /* attempt 1 */
    assert_true(reconnect_next_backoff_ms(&rc) >= 0);  /* attempt 2 */
    assert_int_equal(reconnect_next_backoff_ms(&rc), -1);  /* exhausted */
    assert_int_equal(reconnect_get_state(&rc), RECONNECT_STATE_FAILED);
}

/*
 * Test: failure-reason classification.
 *
 * Permanent rejects (auth/forbidden/conflict/version/...) must exit immediately;
 * transient ones (timeout/peer/resource and local setup errors) keep retrying.
 */
static void test_reason_classification(void **state) {
    (void)state;

    /* Permanent: server/peer rejected on purpose, will not self-heal */
    assert_true(reconnect_reason_is_permanent(SRT_REJX_FORBIDDEN));
    assert_true(reconnect_reason_is_permanent(SRT_REJX_CONFLICT));
    assert_true(reconnect_reason_is_permanent(SRT_REJX_UNAUTHORIZED));
    assert_true(reconnect_reason_is_permanent(SRT_REJX_BAD_REQUEST));
    assert_true(reconnect_reason_is_permanent(SRT_REJ_VERSION));
    assert_true(reconnect_reason_is_permanent(SRT_REJ_BADSECRET));

    /* Transient: keep retrying within the bounded window */
    assert_false(reconnect_reason_is_permanent(SRT_REJ_TIMEOUT));
    assert_false(reconnect_reason_is_permanent(SRT_REJ_PEER));
    assert_false(reconnect_reason_is_permanent(SRT_REJ_RESOURCE));
    assert_false(reconnect_reason_is_permanent(SRT_REJ_SYSTEM));
    assert_false(reconnect_reason_is_permanent(SRT_REJ_UNKNOWN));
    /* srt_client_connect()'s local-setup negative codes are transient */
    assert_false(reconnect_reason_is_permanent(-1));  /* DNS resolve */
    assert_false(reconnect_reason_is_permanent(-2));  /* socket open */
    assert_false(reconnect_reason_is_permanent(-4));  /* sockopt     */
}

/*
 * Test: a custom (non-default) backoff schedule is honored end to end.
 */
static void test_custom_backoff_bounds(void **state) {
    (void)state;

    ReconnectController rc;
    reconnect_init(&rc, 500, 4000, RECONNECT_UNLIMITED);  /* 0.5s base, 4s cap */
    reconnect_begin(&rc);

    assert_int_equal(reconnect_next_backoff_ms(&rc), 500);
    assert_int_equal(reconnect_next_backoff_ms(&rc), 1000);
    assert_int_equal(reconnect_next_backoff_ms(&rc), 2000);
    assert_int_equal(reconnect_next_backoff_ms(&rc), 4000);
    assert_int_equal(reconnect_next_backoff_ms(&rc), 4000);  /* capped */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_backoff_exponential_progression),
        cmocka_unit_test(test_controller_backoff_sequence),
        cmocka_unit_test(test_transient_failure_then_success),
        cmocka_unit_test(test_window_exhaustion_signals_clean_exit),
        cmocka_unit_test(test_unlimited_never_exhausts),
        cmocka_unit_test(test_success_resets_bounded_window),
        cmocka_unit_test(test_reason_classification),
        cmocka_unit_test(test_custom_backoff_bounds),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
