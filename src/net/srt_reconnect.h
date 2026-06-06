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

#ifndef SRT_RECONNECT_H
#define SRT_RECONNECT_H

/*
 * SRT reconnect state machine (ADR-0005: device supervision model).
 *
 * This module is the PURE, testable core of ceracoder's in-process SRT
 * reconnect policy. It owns the "transient SRT loss" failure class: instead of
 * hard-exiting on the first send failure or ACK timeout, ceracoder drives this
 * state machine to retry the SRT connection in-process with exponential
 * backoff. Only when the bounded reconnect window is exhausted (or a permanent
 * failure is classified) does the process exit non-zero, handing restart
 * authority to systemd (Restart=on-failure).
 *
 * This translation unit has NO dependency on GStreamer or live SRT sockets, so
 * the backoff math and state transitions are unit-tested with cmocka without
 * any network. ceracoder.c wires the live socket reconnect around it.
 *
 * Status accessors (reconnect_is_reconnecting / reconnect_total_reconnects)
 * are the "simple flag/counter" health surface consumed by the frame-liveness
 * (Task 12) and health-RPC (Task 13) work.
 */

/* Default backoff schedule. Backoff starts at BASE and doubles each attempt,
 * capped at MAX:  1s -> 2s -> 4s -> 8s -> 16s -> 30s -> 30s ... */
#define RECONNECT_DEFAULT_BASE_MS      1000u   /* first backoff (1s)            */
#define RECONNECT_DEFAULT_MAX_MS       30000u  /* backoff cap (30s)            */

/*
 * Default bounded reconnect window (max in-process attempts before escalating
 * to systemd). Per ADR-0005 transient loss is absorbed in-process but escalates
 * on window exhaustion, so the shipped default is FINITE. With the default
 * schedule, 10 attempts spans ~1+2+4+8+16+30*5 = ~181s before clean exit.
 *
 * The mechanism is fully configurable via reconnect_init(): pass
 * RECONNECT_UNLIMITED to retry transient loss forever (no escalation), or a
 * finite count for a bounded window. Permanent rejects (auth/forbidden/conflict)
 * bypass the window entirely and exit immediately (see
 * reconnect_reason_is_permanent).
 */
#define RECONNECT_DEFAULT_MAX_ATTEMPTS 10
#define RECONNECT_UNLIMITED            0       /* max_attempts <= 0 => unlimited */

typedef enum {
    RECONNECT_STATE_CONNECTED = 0,  /* healthy, streaming                       */
    RECONNECT_STATE_RECONNECTING,   /* a drop occurred, retrying in-process     */
    RECONNECT_STATE_FAILED          /* unrecoverable: window exhausted -> exit  */
} ReconnectState;

typedef struct {
    ReconnectState state;
    int  attempt;              /* attempts used in the CURRENT reconnect episode */
    int  total_reconnects;     /* cumulative SUCCESSFUL reconnects (health counter) */
    int  max_attempts;         /* <= 0 => unlimited; > 0 => bounded window        */
    unsigned int base_backoff_ms;
    unsigned int max_backoff_ms;
    unsigned int last_backoff_ms;  /* backoff returned for the most recent attempt */
} ReconnectController;

/*
 * Compute the backoff (ms) for a 1-indexed attempt number: an overflow-safe
 * min(base * 2^(attempt-1), max). Exposed for testing the schedule directly.
 */
unsigned int reconnect_backoff_for_attempt(unsigned int base_ms,
                                           unsigned int max_ms,
                                           int attempt);

/*
 * Initialize the controller in the CONNECTED state.
 *   base_ms / max_ms: pass 0 to use RECONNECT_DEFAULT_BASE_MS / _MAX_MS.
 *   max_attempts:     <= 0 (RECONNECT_UNLIMITED) for unlimited transient retry,
 *                     > 0 for a bounded window that escalates to systemd.
 */
void reconnect_init(ReconnectController *rc, unsigned int base_ms,
                    unsigned int max_ms, int max_attempts);

/*
 * Begin a new reconnect episode (CONNECTED/FAILED -> RECONNECTING). Resets the
 * per-episode attempt counter; preserves the cumulative total_reconnects.
 */
void reconnect_begin(ReconnectController *rc);

/*
 * Advance the state machine one attempt and return how long (ms) to wait before
 * that attempt. Returns -1 when the bounded window is exhausted, transitioning
 * to RECONNECT_STATE_FAILED (caller must clean-exit for systemd).
 */
long reconnect_next_backoff_ms(ReconnectController *rc);

/*
 * Record a successful reconnect: RECONNECTING -> CONNECTED, reset the episode
 * attempt counter, and increment the cumulative reconnect total.
 */
void reconnect_succeeded(ReconnectController *rc);

/* Health-surface accessors (Task 12 / Task 13). */
int            reconnect_is_reconnecting(const ReconnectController *rc);
int            reconnect_attempt_count(const ReconnectController *rc);
int            reconnect_total_reconnects(const ReconnectController *rc);
ReconnectState reconnect_get_state(const ReconnectController *rc);

/*
 * Classify an SRT failure code as permanent (no point retrying -> exit now) vs
 * transient (retry within the bounded window). `reason` is either an
 * SRT_REJECT_REASON / SRT_REJX_* reject code from srt_getrejectreason(), or one
 * of srt_client_connect()'s negative local-setup codes.
 *
 * Returns non-zero for permanent failures (auth/forbidden/conflict/version
 * mismatch and similar that will not self-heal), zero otherwise.
 */
int reconnect_reason_is_permanent(int reason);

#endif /* SRT_RECONNECT_H */
