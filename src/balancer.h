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

#ifndef BALANCER_H
#define BALANCER_H

#include <stdint.h>

/*
 * Balancer configuration - passed to init()
 */
typedef struct {
    int min_bitrate;      // Minimum allowed bitrate (bps)
    int max_bitrate;      // Maximum allowed bitrate (bps)
    int srt_latency;      // Configured SRT latency (ms)
    int srt_pkt_size;     // SRT packet size (bytes)

    // Adaptive algorithm tuning (bps for bitrate values, ms for intervals)
    int adaptive_incr_step;      // Bitrate increase step (bps, default: 30000)
    int adaptive_decr_step;      // Bitrate decrease step (bps, default: 100000)
    int adaptive_incr_interval;  // Min interval between increases (ms, default: 500)
    int adaptive_decr_interval;  // Min interval between decreases (ms, default: 200)

    // AIMD algorithm tuning
    int aimd_incr_step;          // Additive increase (bps, default: 50000)
    double aimd_decr_mult;       // Multiplicative decrease (0.0-1.0, default: 0.75)
    int aimd_incr_interval;      // Min interval between increases (ms, default: 500)
    int aimd_decr_interval;      // Min interval between decreases (ms, default: 200)
} BalancerConfig;

/*
 * Balancer input - passed to step() every update cycle
 */
typedef struct {
    int buffer_size;      // Current SRT send buffer size (packets)
    double rtt;           // Current round-trip time (ms)
    double send_rate_mbps;// Current send rate (Mbps)
    uint64_t timestamp;   // Current timestamp (ms)
    int64_t pkt_loss_total;  // Total packets lost (cumulative)
    int64_t pkt_retrans_total; // Total packets retransmitted (cumulative)
} BalancerInput;

/*
 * Balancer output - returned from step()
 */
typedef struct {
    int new_bitrate;      // Computed bitrate (bps, rounded to 100 Kbps)
    double throughput;    // Smoothed throughput (for overlay)
    int rtt;              // Current RTT (for overlay)
    int rtt_th_min;       // RTT threshold min (for overlay)
    int rtt_th_max;       // RTT threshold max (for overlay)
    int bs;               // Current buffer size (for overlay)
    int bs_th1;           // Buffer threshold 1 (for overlay)
    int bs_th2;           // Buffer threshold 2 (for overlay)
    int bs_th3;           // Buffer threshold 3 (for overlay)
} BalancerOutput;

/*
 * Balancer algorithm interface
 *
 * Each algorithm implements these three functions:
 * - init:    Allocate and initialize algorithm state
 * - step:    Compute new bitrate based on current network stats
 * - cleanup: Free algorithm state
 */
typedef struct {
    const char *name;        // Algorithm name (e.g., "adaptive", "fixed", "aimd")
    const char *description; // Human-readable description

    // Initialize algorithm state, returns opaque state pointer
    void* (*init)(const BalancerConfig *config);

    // Compute new bitrate, returns output with bitrate and debug info
    BalancerOutput (*step)(void *state, const BalancerInput *input);

    // Clean up algorithm state
    void (*cleanup)(void *state);
} BalancerAlgorithm;

/*
 * Registry functions
 */

// Get the default algorithm (used when --balancer not specified)
const BalancerAlgorithm* balancer_get_default(void);

// Find algorithm by name, returns NULL if not found
const BalancerAlgorithm* balancer_find(const char *name);

// Get array of all registered algorithms (NULL-terminated)
const BalancerAlgorithm* const* balancer_list_all(void);

// Print list of available algorithms to stderr
void balancer_print_available(void);

#endif /* BALANCER_H */
