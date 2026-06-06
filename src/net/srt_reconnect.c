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

#include "srt_reconnect.h"

#include <srt.h>
#include <srt/access_control.h>

unsigned int reconnect_backoff_for_attempt(unsigned int base_ms,
                                           unsigned int max_ms,
                                           int attempt) {
    if (base_ms == 0u) base_ms = RECONNECT_DEFAULT_BASE_MS;
    if (max_ms == 0u)  max_ms  = RECONNECT_DEFAULT_MAX_MS;
    if (max_ms < base_ms) max_ms = base_ms;

    /* attempt is 1-indexed; attempt <= 1 yields the base backoff. */
    if (attempt <= 1) {
        return (base_ms < max_ms) ? base_ms : max_ms;
    }

    /* Overflow-safe doubling: stop as soon as we reach/exceed the cap. */
    unsigned long backoff = base_ms;
    for (int i = 1; i < attempt; i++) {
        backoff <<= 1;
        if (backoff >= max_ms) {
            return max_ms;
        }
    }
    return (unsigned int)backoff;
}

void reconnect_init(ReconnectController *rc, unsigned int base_ms,
                    unsigned int max_ms, int max_attempts) {
    rc->state = RECONNECT_STATE_CONNECTED;
    rc->attempt = 0;
    rc->total_reconnects = 0;
    rc->max_attempts = max_attempts;
    rc->base_backoff_ms = (base_ms != 0u) ? base_ms : RECONNECT_DEFAULT_BASE_MS;
    rc->max_backoff_ms = (max_ms != 0u) ? max_ms : RECONNECT_DEFAULT_MAX_MS;
    if (rc->max_backoff_ms < rc->base_backoff_ms) {
        rc->max_backoff_ms = rc->base_backoff_ms;
    }
    rc->last_backoff_ms = 0;
}

void reconnect_begin(ReconnectController *rc) {
    rc->state = RECONNECT_STATE_RECONNECTING;
    rc->attempt = 0;
    rc->last_backoff_ms = 0;
}

long reconnect_next_backoff_ms(ReconnectController *rc) {
    if (rc->state == RECONNECT_STATE_FAILED) {
        return -1;
    }

    /* Bounded window: once every allowed attempt has been consumed, the window
     * is exhausted -> escalate to systemd via a clean non-zero exit. A
     * non-positive max_attempts means "unlimited" and never exhausts. */
    if (rc->max_attempts > 0 && rc->attempt >= rc->max_attempts) {
        rc->state = RECONNECT_STATE_FAILED;
        return -1;
    }

    rc->attempt += 1;
    rc->state = RECONNECT_STATE_RECONNECTING;
    rc->last_backoff_ms = reconnect_backoff_for_attempt(
        rc->base_backoff_ms, rc->max_backoff_ms, rc->attempt);
    return (long)rc->last_backoff_ms;
}

void reconnect_succeeded(ReconnectController *rc) {
    rc->state = RECONNECT_STATE_CONNECTED;
    rc->attempt = 0;
    rc->last_backoff_ms = 0;
    rc->total_reconnects += 1;
}

int reconnect_is_reconnecting(const ReconnectController *rc) {
    return rc->state == RECONNECT_STATE_RECONNECTING;
}

int reconnect_attempt_count(const ReconnectController *rc) {
    return rc->attempt;
}

int reconnect_total_reconnects(const ReconnectController *rc) {
    return rc->total_reconnects;
}

ReconnectState reconnect_get_state(const ReconnectController *rc) {
    return rc->state;
}

int reconnect_reason_is_permanent(int reason) {
    switch (reason) {
        /* --- Internal SRT_REJECT_REASON codes that will not self-heal --- */
        case SRT_REJ_ROGUE:      /* malformed handshake: misconfig, not transient */
        case SRT_REJ_VERSION:    /* peer below minimum version                    */
        case SRT_REJ_RDVCOOKIE:  /* rendezvous cookie collision                   */
        case SRT_REJ_BADSECRET:  /* wrong passphrase                              */
        case SRT_REJ_UNSECURE:   /* passphrase required/unexpected                */
        case SRT_REJ_MESSAGEAPI: /* stream/message API mismatch                   */
        case SRT_REJ_CONGESTION: /* incompatible congestion controller            */
        case SRT_REJ_FILTER:     /* incompatible packet filter                    */
        case SRT_REJ_GROUP:      /* incompatible group                            */
#ifdef ENABLE_AEAD_API_PREVIEW
        case SRT_REJ_CRYPTO:     /* conflicting crypto configuration              */
#endif
        /* --- Predefined access-control rejects (server said no on purpose) --- */
        case SRT_REJX_BAD_REQUEST:  /* 1400: malformed streamid                   */
        case SRT_REJX_UNAUTHORIZED: /* 1401: auth failed                          */
        case SRT_REJX_FORBIDDEN:    /* 1403: access denied (invalid streamid)     */
        case SRT_REJX_NOTFOUND:     /* 1404: resource not found                   */
        case SRT_REJX_BAD_MODE:     /* 1405: unsupported mode                     */
        case SRT_REJX_UNACCEPTABLE: /* 1406: unsatisfiable parameters             */
        case SRT_REJX_CONFLICT:     /* 1409: streamid already in use              */
            return 1;

        /* Everything else (timeout, peer-busy, resource, backlog, system, close,
         * DNS/socket/sockopt local-setup errors) is treated as transient and is
         * retried within the bounded window. */
        default:
            return 0;
    }
}
