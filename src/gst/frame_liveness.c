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

#include "frame_liveness.h"

void frame_liveness_init(FrameLiveness *fl, uint64_t stall_threshold_ms,
                         uint64_t now_ms) {
    atomic_store_explicit(&fl->frame_count, 0, memory_order_relaxed);
    /* Seed the "last frame" baseline with startup time so the time-to-first-frame
       is bounded by the same threshold as a mid-stream stall (ADR-0005). */
    atomic_store_explicit(&fl->last_frame_ms, now_ms, memory_order_relaxed);
    fl->stall_threshold_ms =
        (stall_threshold_ms != 0) ? stall_threshold_ms
                                  : FRAME_LIVENESS_DEFAULT_STALL_MS;
}

void frame_liveness_record(FrameLiveness *fl, uint64_t now_ms) {
    /* Hot path: one produced frame. Keep it to a relaxed counter bump + a
       relaxed timestamp store — no lock, no allocation, no syscall. */
    atomic_fetch_add_explicit(&fl->frame_count, 1, memory_order_relaxed);
    atomic_store_explicit(&fl->last_frame_ms, now_ms, memory_order_relaxed);
}

bool frame_liveness_advancing_at(const FrameLiveness *fl, uint64_t now_ms) {
    uint64_t last = atomic_load_explicit(&fl->last_frame_ms, memory_order_relaxed);
    /* Defend against a non-monotonic / racing now_ms that predates the last
       recorded frame: that is not evidence of a stall, so report advancing. */
    if (now_ms <= last) {
        return true;
    }
    return (now_ms - last) < fl->stall_threshold_ms;
}

uint64_t frame_liveness_count(const FrameLiveness *fl) {
    return atomic_load_explicit(&fl->frame_count, memory_order_relaxed);
}

uint64_t frame_liveness_last_frame_ms(const FrameLiveness *fl) {
    return atomic_load_explicit(&fl->last_frame_ms, memory_order_relaxed);
}

uint64_t frame_liveness_threshold_ms(const FrameLiveness *fl) {
    return fl->stall_threshold_ms;
}
