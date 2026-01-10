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
 * AIMD balancer - Additive Increase Multiplicative Decrease
 *
 * Classic TCP-style congestion control algorithm:
 * - Increase bitrate linearly when conditions are good
 * - Decrease bitrate by a fraction when congestion is detected
 *
 * This provides fair bandwidth sharing and stable convergence,
 * but may be slower to adapt than the default adaptive algorithm.
 */

#include "balancer.h"
#include <stdlib.h>
#include <glib.h>  // for MIN/MAX

// Default AIMD parameters (used if config values are 0)
#define AIMD_DEF_INCR_RATE      (50 * 1000)   // Additive increase: 50 Kbps per step
#define AIMD_DEF_DECR_MULT      0.75          // Multiplicative decrease: reduce to 75%
#define AIMD_DEF_INCR_INTERVAL  500           // ms between increases
#define AIMD_DEF_DECR_INTERVAL  200           // ms between decreases

// Congestion detection thresholds
#define AIMD_RTT_MULT       1.5           // Congestion if RTT > baseline * 1.5
#define AIMD_RTT_BASELINE_EMA 0.95        // Slow EMA for RTT baseline
#define AIMD_BS_THRESHOLD   100           // Buffer size threshold (packets)

/*
 * State structure
 */
typedef struct {
    int min_bitrate;
    int max_bitrate;
    int cur_bitrate;
    int srt_latency;

    // Tuning parameters (from config)
    int incr_step;
    double decr_mult;
    int incr_interval;
    int decr_interval;

    // RTT baseline tracking
    double rtt_baseline;

    // Timing
    uint64_t next_incr;
    uint64_t next_decr;
} AimdState;

/*
 * Initialize the AIMD balancer
 */
static void* aimd_init(const BalancerConfig *config) {
    AimdState *state = malloc(sizeof(AimdState));
    if (state == NULL) {
        return NULL;
    }

    state->min_bitrate = config->min_bitrate;
    state->max_bitrate = config->max_bitrate;
    state->cur_bitrate = config->max_bitrate;  // Start optimistic
    state->srt_latency = config->srt_latency;

    // Tuning parameters (use defaults if 0)
    state->incr_step = (config->aimd_incr_step > 0) ?
                       config->aimd_incr_step : AIMD_DEF_INCR_RATE;
    state->decr_mult = (config->aimd_decr_mult > 0.0) ?
                       config->aimd_decr_mult : AIMD_DEF_DECR_MULT;
    state->incr_interval = (config->aimd_incr_interval > 0) ?
                           config->aimd_incr_interval : AIMD_DEF_INCR_INTERVAL;
    state->decr_interval = (config->aimd_decr_interval > 0) ?
                           config->aimd_decr_interval : AIMD_DEF_DECR_INTERVAL;

    state->rtt_baseline = 0.0;
    state->next_incr = 0;
    state->next_decr = 0;

    return state;
}

/*
 * Compute new bitrate using AIMD
 */
static BalancerOutput aimd_step(void *state_ptr, const BalancerInput *input) {
    AimdState *state = (AimdState *)state_ptr;

    // Update RTT baseline (slow moving average of minimum RTT)
    if (state->rtt_baseline == 0.0) {
        state->rtt_baseline = input->rtt;
    } else if (input->rtt < state->rtt_baseline) {
        // Quick adaptation downward
        state->rtt_baseline = input->rtt;
    } else {
        // Slow drift upward
        state->rtt_baseline = (state->rtt_baseline * AIMD_RTT_BASELINE_EMA) +
                              (input->rtt * (1.0 - AIMD_RTT_BASELINE_EMA));
    }

    // Detect congestion
    int congested = 0;
    int rtt_threshold = (int)(state->rtt_baseline * AIMD_RTT_MULT);

    // Emergency: RTT exceeds latency/3
    if (input->rtt >= state->srt_latency / 3) {
        state->cur_bitrate = state->min_bitrate;
        state->next_decr = input->timestamp + state->decr_interval;
        congested = 1;
    }
    // Congestion: RTT exceeds threshold or buffer too full
    else if (input->rtt > rtt_threshold || input->buffer_size > AIMD_BS_THRESHOLD) {
        congested = 1;
    }

    if (congested && input->timestamp > state->next_decr) {
        // Multiplicative decrease
        state->cur_bitrate = (int)(state->cur_bitrate * state->decr_mult);
        state->next_decr = input->timestamp + state->decr_interval;

    } else if (!congested && input->timestamp > state->next_incr) {
        // Additive increase
        state->cur_bitrate += state->incr_step;
        state->next_incr = input->timestamp + state->incr_interval;
    }

    // Clamp to valid range
    state->cur_bitrate = MAX(state->min_bitrate, MIN(state->max_bitrate, state->cur_bitrate));

    // Round to 100 kbps
    int rounded_br = state->cur_bitrate / (100 * 1000) * (100 * 1000);

    BalancerOutput output = {
        .new_bitrate = rounded_br,
        .throughput = 0,  // Not tracked in AIMD
        .rtt = (int)input->rtt,
        .rtt_th_min = (int)state->rtt_baseline,
        .rtt_th_max = rtt_threshold,
        .bs = input->buffer_size,
        .bs_th1 = AIMD_BS_THRESHOLD,
        .bs_th2 = AIMD_BS_THRESHOLD,
        .bs_th3 = AIMD_BS_THRESHOLD
    };

    return output;
}

/*
 * Clean up AIMD balancer state
 */
static void aimd_cleanup(void *state_ptr) {
    free(state_ptr);
}

/*
 * AIMD balancer algorithm definition
 */
const BalancerAlgorithm balancer_aimd = {
    .name = "aimd",
    .description = "Additive Increase Multiplicative Decrease (TCP-style)",
    .init = aimd_init,
    .step = aimd_step,
    .cleanup = aimd_cleanup,
};
