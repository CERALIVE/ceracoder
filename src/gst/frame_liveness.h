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

#ifndef CERACODER_FRAME_LIVENESS_H
#define CERACODER_FRAME_LIVENESS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Frame-production liveness tracker (ADR-0005 device supervision model).
 *
 * This module is the PURE, testable core of ceracoder's "zombie-encode"
 * detection. Per ADR-0005 the device health-check is process-alive + frame
 * production + bond-up; this owns the *frame production* signal. The encoder
 * pipeline can wedge in a state where the process is alive and the SRT link is
 * up, yet no encoded frames are produced (capture stall, encoder hang). Process
 * liveness alone cannot see this — only "are buffers still advancing through
 * appsink" can.
 *
 * The tracker stores two things, updated cheaply once per produced frame
 * (appsink buffer) from the GStreamer streaming thread:
 *   - a monotonic-clock timestamp of the most recent frame, and
 *   - a cumulative frame counter.
 * A reader on the main loop thread (the systemd watchdog ping) asks
 * frame_liveness_advancing_at(): true while a frame has arrived within the
 * stall threshold, false once production has stalled for >= threshold ms. The
 * watchdog ping is gated on this, so a zombie-encode stops petting WatchdogSec
 * and systemd respawns the process (no independent restart authority).
 *
 * Cross-thread safety: the producer (record) and the reader (advancing/count)
 * run on different threads, so the two shared scalars are C11 relaxed atomics.
 * Relaxed ordering is sufficient — we only need a coherent per-field view of a
 * timestamp and a counter, not ordering against other memory. On the target
 * (aarch64 Jetson Nano) these compile to plain aligned load/store, so the
 * per-frame cost is just a store + an increment (no lock, no syscall).
 *
 * This translation unit has NO dependency on GStreamer, SRT, or a real clock:
 * the "now" is injected as a parameter, so the stall math and threshold timing
 * are unit-tested with cmocka deterministically (no sleeping). ceracoder.c owns
 * the singleton instance, samples CLOCK_MONOTONIC via getms(), and exposes the
 * ceracoder_frames_advancing() / ceracoder_frame_count() health surface that
 * the watchdog ping (Task 12) and the health RPC (Task 13) consume.
 */

/* Default stall threshold: no produced frame for this many ms => not advancing
 * (zombie-encode). 3s comfortably exceeds a single frame interval at any sane
 * framerate (>=1 fps) yet reacts well within a typical WatchdogSec window. */
#define FRAME_LIVENESS_DEFAULT_STALL_MS 3000u

typedef struct {
    _Atomic uint64_t frame_count;   /* cumulative produced frames (health counter) */
    _Atomic uint64_t last_frame_ms; /* monotonic ms of the most recent frame       */
    uint64_t stall_threshold_ms;    /* write-once at init; read-only thereafter     */
} FrameLiveness;

/*
 * Initialize the tracker.
 *   stall_threshold_ms: 0 selects FRAME_LIVENESS_DEFAULT_STALL_MS.
 *   now_ms:             current monotonic clock; seeds last_frame_ms so the
 *                       interval from startup to the first frame is itself
 *                       bounded by the threshold (a pipeline that never produces
 *                       a first frame is flagged stalled after the grace
 *                       window, exactly like a mid-stream stall).
 */
void frame_liveness_init(FrameLiveness *fl, uint64_t stall_threshold_ms,
                         uint64_t now_ms);

/*
 * Record one produced frame at monotonic time now_ms. Called once per appsink
 * buffer from the streaming thread. Intentionally minimal: one relaxed atomic
 * store + one relaxed atomic increment, no allocation or syscall.
 */
void frame_liveness_record(FrameLiveness *fl, uint64_t now_ms);

/*
 * True iff a frame has been produced within the last stall_threshold_ms as of
 * now_ms (strictly: now_ms - last_frame_ms < threshold). At exactly the
 * threshold the stream is considered stalled (returns false). A non-monotonic
 * now_ms earlier than last_frame_ms is treated as "fresh" (advancing) rather
 * than reporting a spurious stall.
 */
bool frame_liveness_advancing_at(const FrameLiveness *fl, uint64_t now_ms);

/* Cumulative count of produced frames since init. */
uint64_t frame_liveness_count(const FrameLiveness *fl);

/* Monotonic ms of the most recently produced frame (or the init baseline if no
 * frame has been produced yet). */
uint64_t frame_liveness_last_frame_ms(const FrameLiveness *fl);

/* The configured stall threshold in ms (resolved default included). */
uint64_t frame_liveness_threshold_ms(const FrameLiveness *fl);

#endif /* CERACODER_FRAME_LIVENESS_H */
