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
 * Unit tests for the frame-production liveness tracker (ADR-0005, Task 12).
 *
 * These exercise the PURE zombie-encode detector with an injected monotonic
 * clock and no GStreamer/SRT: a healthy stream reports advancing frames, a
 * stalled pipeline (process alive, frames stopped) is flagged not-advancing
 * once the stall threshold elapses, and the threshold boundary is exact. Time
 * is a parameter, so the N-ms timing is verified deterministically without
 * sleeping.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "frame_liveness.h"

/*
 * Test: a healthy stream reports advancing frames.
 *
 * Frames recorded at the nominal interval keep advancing_at() true across the
 * whole run; the cumulative counter tracks every produced frame.
 */
static void test_healthy_stream_advancing(void **state) {
    (void)state;

    FrameLiveness fl;
    frame_liveness_init(&fl, FRAME_LIVENESS_DEFAULT_STALL_MS, 0);

    /* 30 fps => ~33 ms/frame, well inside the 3000 ms threshold. */
    uint64_t now = 0;
    for (int i = 0; i < 300; i++) {
        now += 33;
        frame_liveness_record(&fl, now);
        assert_true(frame_liveness_advancing_at(&fl, now));
        /* Still advancing a short time after the most recent frame. */
        assert_true(frame_liveness_advancing_at(&fl, now + 100));
    }

    assert_int_equal((int)frame_liveness_count(&fl), 300);
    assert_int_equal((int)frame_liveness_last_frame_ms(&fl), (int)now);
}

/*
 * Test: a stalled pipeline is flagged not-advancing after N ms.
 *
 * The zombie-encode case: frames flow, then stop while the process stays alive.
 * advancing_at() stays true through the threshold window, then flips false once
 * the gap reaches the threshold — that is the signal the watchdog ping is gated
 * on.
 */
static void test_stalled_pipeline_flagged(void **state) {
    (void)state;

    const uint64_t threshold = FRAME_LIVENESS_DEFAULT_STALL_MS; /* 3000 */
    FrameLiveness fl;
    frame_liveness_init(&fl, threshold, 0);

    /* Last frame produced at t=5000, then the encoder wedges. */
    uint64_t last = 5000;
    frame_liveness_record(&fl, last);
    assert_true(frame_liveness_advancing_at(&fl, last));

    /* Within the window: still considered healthy. */
    assert_true(frame_liveness_advancing_at(&fl, last + 1));
    assert_true(frame_liveness_advancing_at(&fl, last + (threshold - 1)));

    /* At/after the threshold: stalled. */
    assert_false(frame_liveness_advancing_at(&fl, last + threshold));
    assert_false(frame_liveness_advancing_at(&fl, last + threshold + 1));
    assert_false(frame_liveness_advancing_at(&fl, last + 10 * threshold));

    /* A fresh frame after the stall clears it again (recovery is observable). */
    frame_liveness_record(&fl, last + 10 * threshold);
    assert_true(frame_liveness_advancing_at(&fl, last + 10 * threshold));
}

/*
 * Test: the N-ms threshold boundary is exact.
 *
 * gap < threshold => advancing; gap >= threshold => stalled. Verified at the
 * default and a custom threshold so the timing is not hard-coded to one value.
 */
static void test_threshold_timing_boundary(void **state) {
    (void)state;

    const uint64_t thresholds[] = {FRAME_LIVENESS_DEFAULT_STALL_MS, 1000, 250, 500};
    for (size_t t = 0; t < sizeof(thresholds) / sizeof(thresholds[0]); t++) {
        uint64_t th = thresholds[t];
        FrameLiveness fl;
        frame_liveness_init(&fl, th, 0);
        assert_int_equal((int)frame_liveness_threshold_ms(&fl), (int)th);

        uint64_t last = 1000;
        frame_liveness_record(&fl, last);

        /* Largest gap that is still healthy. */
        assert_true(frame_liveness_advancing_at(&fl, last + (th - 1)));
        /* Exactly the threshold is already a stall. */
        assert_false(frame_liveness_advancing_at(&fl, last + th));
    }
}

/*
 * Test: a zero threshold resolves to the shipped default.
 *
 * Mirrors the init convention used elsewhere (pass 0 => default), so callers
 * that do not override the env knob get FRAME_LIVENESS_DEFAULT_STALL_MS.
 */
static void test_zero_threshold_uses_default(void **state) {
    (void)state;

    FrameLiveness fl;
    frame_liveness_init(&fl, 0, 0);
    assert_int_equal((int)frame_liveness_threshold_ms(&fl),
                     (int)FRAME_LIVENESS_DEFAULT_STALL_MS);
}

/*
 * Test: the startup grace bounds time-to-first-frame.
 *
 * Before any frame is produced the init baseline acts as the "last activity"
 * time, so a pipeline that never emits a first frame is flagged stalled after
 * the same threshold as a mid-stream stall (no special-casing required).
 */
static void test_startup_grace_then_stall(void **state) {
    (void)state;

    const uint64_t threshold = FRAME_LIVENESS_DEFAULT_STALL_MS;
    FrameLiveness fl;
    uint64_t start = 10000;
    frame_liveness_init(&fl, threshold, start);

    assert_int_equal((int)frame_liveness_count(&fl), 0);

    /* During the grace window (no frame yet) we still report advancing so the
       watchdog keeps being pet through normal startup. */
    assert_true(frame_liveness_advancing_at(&fl, start));
    assert_true(frame_liveness_advancing_at(&fl, start + (threshold - 1)));

    /* First frame never arrives: stalled once the grace window elapses. */
    assert_false(frame_liveness_advancing_at(&fl, start + threshold));

    /* A first frame inside the grace window clears it and counts. */
    frame_liveness_record(&fl, start + 500);
    assert_int_equal((int)frame_liveness_count(&fl), 1);
    assert_true(frame_liveness_advancing_at(&fl, start + 500 + (threshold - 1)));
}

/*
 * Test: a non-monotonic / racing reader clock never reports a spurious stall.
 *
 * The producer (streaming thread) and reader (main loop) are unsynchronized; a
 * now_ms sampled at or before the last recorded frame must be treated as fresh
 * rather than as a giant backwards gap.
 */
static void test_non_monotonic_now_is_fresh(void **state) {
    (void)state;

    FrameLiveness fl;
    frame_liveness_init(&fl, FRAME_LIVENESS_DEFAULT_STALL_MS, 0);

    frame_liveness_record(&fl, 100000);
    assert_true(frame_liveness_advancing_at(&fl, 100000));        /* equal */
    assert_true(frame_liveness_advancing_at(&fl, 99999));         /* earlier */
    assert_true(frame_liveness_advancing_at(&fl, 0));             /* far earlier */
}

/*
 * Test: the frame counter is monotonic and matches the number of records.
 */
static void test_counter_monotonic(void **state) {
    (void)state;

    FrameLiveness fl;
    frame_liveness_init(&fl, FRAME_LIVENESS_DEFAULT_STALL_MS, 0);
    assert_int_equal((int)frame_liveness_count(&fl), 0);

    uint64_t now = 0;
    uint64_t prev = 0;
    for (int i = 1; i <= 1000; i++) {
        now += 16;
        frame_liveness_record(&fl, now);
        uint64_t c = frame_liveness_count(&fl);
        assert_int_equal((int)c, i);
        assert_true(c > prev);
        prev = c;
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_healthy_stream_advancing),
        cmocka_unit_test(test_stalled_pipeline_flagged),
        cmocka_unit_test(test_threshold_timing_boundary),
        cmocka_unit_test(test_zero_threshold_uses_default),
        cmocka_unit_test(test_startup_grace_then_stall),
        cmocka_unit_test(test_non_monotonic_now_is_fresh),
        cmocka_unit_test(test_counter_monotonic),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
