/*
    belacoder - live video encoder with dynamic bitrate control
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
 * Adaptive balancer - RTT and buffer-based bitrate control
 *
 * This is the default algorithm that adapts bitrate based on:
 * - Round-trip time (RTT) measurements
 * - SRT send buffer occupancy
 * - Throughput estimation
 *
 * It uses multiple congestion detection thresholds to provide
 * graduated responses from gentle decreases to emergency drops.
 */

#include "balancer.h"
#include "bitrate_control.h"
#include <stdlib.h>

/*
 * State structure - wraps BitrateContext
 */
typedef struct {
    BitrateContext ctx;
} AdaptiveState;

/*
 * Initialize the adaptive balancer
 */
static void* adaptive_init(const BalancerConfig *config) {
    AdaptiveState *state = malloc(sizeof(AdaptiveState));
    if (state == NULL) {
        return NULL;
    }

    bitrate_context_init(&state->ctx,
                         config->min_bitrate,
                         config->max_bitrate,
                         config->srt_latency,
                         config->srt_pkt_size);

    return state;
}

/*
 * Compute new bitrate based on current network stats
 */
static BalancerOutput adaptive_step(void *state_ptr, const BalancerInput *input) {
    AdaptiveState *state = (AdaptiveState *)state_ptr;
    BalancerOutput output = {0};

    // Use existing bitrate_update with a temporary BitrateResult
    BitrateResult result;
    int new_bitrate = bitrate_update(&state->ctx,
                                     input->buffer_size,
                                     input->rtt,
                                     input->send_rate_mbps,
                                     input->timestamp,
                                     &result);

    // Convert BitrateResult to BalancerOutput
    output.new_bitrate = new_bitrate;
    output.throughput = result.throughput;
    output.rtt = result.rtt;
    output.rtt_th_min = result.rtt_th_min;
    output.rtt_th_max = result.rtt_th_max;
    output.bs = result.bs;
    output.bs_th1 = result.bs_th1;
    output.bs_th2 = result.bs_th2;
    output.bs_th3 = result.bs_th3;

    return output;
}

/*
 * Clean up adaptive balancer state
 */
static void adaptive_cleanup(void *state_ptr) {
    free(state_ptr);
}

/*
 * Adaptive balancer algorithm definition
 */
const BalancerAlgorithm balancer_adaptive = {
    .name = "adaptive",
    .description = "RTT and buffer-based adaptive control (default)",
    .init = adaptive_init,
    .step = adaptive_step,
    .cleanup = adaptive_cleanup,
};
