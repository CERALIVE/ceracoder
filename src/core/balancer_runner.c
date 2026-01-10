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

#include "balancer_runner.h"
#include <stdio.h>
#include <stdlib.h>

int balancer_runner_init(BalancerRunner *runner, const BelacoderConfig *cfg,
                         const char *algo_name_override, int srt_latency, int srt_pkt_size) {
    runner->algo = NULL;
    runner->state = NULL;

    // Select algorithm (CLI override takes precedence)
    const char *algo_name = algo_name_override ? algo_name_override : cfg->balancer;
    runner->algo = balancer_find(algo_name);
    
    if (runner->algo == NULL) {
        // Try default if config had invalid name
        if (algo_name_override != NULL) {
            fprintf(stderr, "Unknown balancer algorithm: %s\n\n", algo_name_override);
            balancer_print_available();
            return -1;
        }
        runner->algo = balancer_get_default();
    }

    fprintf(stderr, "Balancer: %s\n", runner->algo->name);

    // Initialize balancer config
    runner->config.min_bitrate = config_bitrate_bps(cfg->min_bitrate);
    runner->config.max_bitrate = config_bitrate_bps(cfg->max_bitrate);
    runner->config.srt_latency = srt_latency;
    runner->config.srt_pkt_size = srt_pkt_size;

    // Adaptive algorithm tuning
    runner->config.adaptive_incr_step = config_bitrate_bps(cfg->adaptive.incr_step);
    runner->config.adaptive_decr_step = config_bitrate_bps(cfg->adaptive.decr_step);
    runner->config.adaptive_incr_interval = cfg->adaptive.incr_interval;
    runner->config.adaptive_decr_interval = cfg->adaptive.decr_interval;

    // AIMD algorithm tuning
    runner->config.aimd_incr_step = config_bitrate_bps(cfg->aimd.incr_step);
    runner->config.aimd_decr_mult = cfg->aimd.decr_mult;
    runner->config.aimd_incr_interval = cfg->aimd.incr_interval;
    runner->config.aimd_decr_interval = cfg->aimd.decr_interval;

    // Initialize the algorithm
    runner->state = runner->algo->init(&runner->config);
    if (runner->state == NULL) {
        fprintf(stderr, "Failed to initialize balancer algorithm\n");
        return -2;
    }

    fprintf(stderr, "Bitrate range: %d - %d Kbps\n",
            runner->config.min_bitrate / 1000, runner->config.max_bitrate / 1000);

    return 0;
}

BalancerOutput balancer_runner_step(BalancerRunner *runner, const BalancerInput *input) {
    return runner->algo->step(runner->state, input);
}

void balancer_runner_update_bounds(BalancerRunner *runner, int min_bitrate, int max_bitrate) {
    runner->config.min_bitrate = min_bitrate;
    runner->config.max_bitrate = max_bitrate;

    // Reinitialize algorithm with new config (loses accumulated state)
    if (runner->algo != NULL && runner->state != NULL) {
        runner->algo->cleanup(runner->state);
        runner->state = runner->algo->init(&runner->config);
    }
}

const char* balancer_runner_get_name(const BalancerRunner *runner) {
    return runner->algo ? runner->algo->name : "none";
}

void balancer_runner_cleanup(BalancerRunner *runner) {
    if (runner->algo != NULL && runner->state != NULL) {
        runner->algo->cleanup(runner->state);
        runner->state = NULL;
    }
}
