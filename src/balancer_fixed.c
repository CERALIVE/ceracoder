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
 * Fixed balancer - maintains constant bitrate
 *
 * This algorithm simply outputs the configured max_bitrate without
 * any adaptation. Useful for:
 * - Testing and debugging
 * - Stable network connections where adaptation isn't needed
 * - Comparing against adaptive algorithms
 */

#include "balancer.h"
#include <stdlib.h>

/*
 * State structure
 */
typedef struct {
    int fixed_bitrate;  // The constant bitrate to output
} FixedState;

/*
 * Initialize the fixed balancer
 */
static void* fixed_init(const BalancerConfig *config) {
    FixedState *state = malloc(sizeof(FixedState));
    if (state == NULL) {
        return NULL;
    }

    // Use max_bitrate as the fixed output
    state->fixed_bitrate = config->max_bitrate;

    // Round to 100 kbps
    state->fixed_bitrate = state->fixed_bitrate / (100 * 1000) * (100 * 1000);

    return state;
}

/*
 * Always return the fixed bitrate
 */
static BalancerOutput fixed_step(void *state_ptr, const BalancerInput *input) {
    FixedState *state = (FixedState *)state_ptr;
    (void)input;  // Unused - we ignore network conditions

    BalancerOutput output = {
        .new_bitrate = state->fixed_bitrate,
        .throughput = 0,      // No tracking
        .rtt = (int)input->rtt,
        .rtt_th_min = 0,
        .rtt_th_max = 0,
        .bs = input->buffer_size,
        .bs_th1 = 0,
        .bs_th2 = 0,
        .bs_th3 = 0
    };

    return output;
}

/*
 * Clean up fixed balancer state
 */
static void fixed_cleanup(void *state_ptr) {
    free(state_ptr);
}

/*
 * Fixed balancer algorithm definition
 */
const BalancerAlgorithm balancer_fixed = {
    .name = "fixed",
    .description = "Constant bitrate, no adaptation",
    .init = fixed_init,
    .step = fixed_step,
    .cleanup = fixed_cleanup,
};
