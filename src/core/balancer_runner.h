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

#ifndef BALANCER_RUNNER_H
#define BALANCER_RUNNER_H

#include "balancer.h"
#include "config.h"
#include <stdint.h>

/*
 * Balancer runner module - orchestrates balancer algorithm execution
 *
 * This module initializes and manages the balancer algorithm lifecycle,
 * providing a clean interface for updating bitrate based on network stats.
 */

typedef struct {
    const BalancerAlgorithm *algo;
    void *state;
    BalancerConfig config;
} BalancerRunner;

/*
 * Initialize balancer runner with configuration
 *
 * Returns 0 on success, < 0 on error.
 */
int balancer_runner_init(BalancerRunner *runner, const BelacoderConfig *cfg,
                         const char *algo_name_override, int srt_latency, int srt_pkt_size);

/*
 * Update bitrate based on network statistics
 *
 * This is called periodically to compute new bitrate.
 * Returns BalancerOutput with new bitrate and debug info.
 */
BalancerOutput balancer_runner_step(BalancerRunner *runner, const BalancerInput *input);

/*
 * Update min/max bitrate bounds (for config reload)
 */
void balancer_runner_update_bounds(BalancerRunner *runner, int min_bitrate, int max_bitrate);

/*
 * Get current algorithm name
 */
const char* balancer_runner_get_name(const BalancerRunner *runner);

/*
 * Cleanup balancer runner
 */
void balancer_runner_cleanup(BalancerRunner *runner);

#endif /* BALANCER_RUNNER_H */
