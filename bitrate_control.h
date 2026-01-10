/*
    belacoder - live video encoder with dynamic bitrate control
    Copyright (C) 2020 BELABOX project

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

#ifndef BITRATE_CONTROL_H
#define BITRATE_CONTROL_H

#include <stdint.h>

/*
 * Bitrate control constants
 */

// Bitrate limits
#define MIN_BITRATE (300 * 1000)
#define ABS_MAX_BITRATE (30 * 1000 * 1000)
#define DEF_BITRATE (6 * 1000 * 1000)

// Update intervals (ms)
#define BITRATE_UPDATE_INT 20
#define BITRATE_INCR_INT       500        // min interval for increasing bitrate
#define BITRATE_DECR_INT       200        // light congestion: min interval for decreasing
#define BITRATE_DECR_FAST_INT  250        // heavy congestion: min interval for decreasing

// Bitrate adjustment amounts (bps)
#define BITRATE_INCR_MIN       (30*1000)  // minimum bitrate increment step
#define BITRATE_INCR_SCALE     30         // bitrate increased by INCR_MIN + cur_bitrate/INCR_SCALE
#define BITRATE_DECR_MIN       (100*1000) // minimum bitrate decrement step
#define BITRATE_DECR_SCALE     10         // heavy congestion: decrease by DECR_MIN + cur_bitrate/DECR_SCALE

// Exponential moving average smoothing factors
#define EMA_SLOW           0.99   // for bs_avg, rtt_avg, jitter decay
#define EMA_FAST           0.01   // complement of EMA_SLOW (1 - 0.99)
#define EMA_RTT_DELTA      0.8    // for rtt_avg_delta smoothing
#define EMA_RTT_DELTA_NEW  0.2    // complement (1 - 0.8)
#define EMA_THROUGHPUT     0.97   // for throughput smoothing
#define EMA_THROUGHPUT_NEW 0.03   // complement (1 - 0.97)

// RTT tracking constants
#define RTT_MIN_DRIFT      1.001  // per-sample drift rate for min RTT tracking
#define RTT_IGNORE_VALUE   100    // RTT value that indicates no valid measurement
#define RTT_INITIAL        300    // initial prev_rtt value
#define RTT_MIN_INITIAL    200.0  // initial rtt_min value

// Threshold multipliers for congestion detection
#define BS_TH3_MULT        4      // heavy congestion: (bs_avg + bs_jitter) * 4
#define BS_TH2_JITTER_MULT 3.0    // medium congestion jitter multiplier
#define BS_TH1_JITTER_MULT 2.5    // light congestion jitter multiplier
#define BS_TH_MIN          50     // minimum buffer threshold
#define RTT_JITTER_MULT    4      // rtt_th_max jitter multiplier
#define RTT_AVG_PERCENT    15     // rtt_th_max percentage of average (15%)
#define RTT_STABLE_DELTA   0.01   // max rtt_avg_delta for stable conditions
#define RTT_MIN_JITTER     1      // minimum jitter for rtt_th_min calculation

/*
 * Bitrate controller context - holds all state for the adaptive bitrate algorithm
 */
typedef struct {
    // Configuration (set once at init)
    int min_bitrate;
    int max_bitrate;
    int srt_latency;
    int srt_pkt_size;

    // Current bitrate
    int cur_bitrate;

    // Buffer size tracking
    double bs_avg;
    double bs_jitter;
    int prev_bs;

    // RTT tracking
    double rtt_avg;
    double rtt_min;
    double rtt_jitter;
    double rtt_avg_delta;
    int prev_rtt;

    // Throughput tracking
    double throughput;

    // Timing for rate limiting bitrate changes
    uint64_t next_bitrate_incr;
    uint64_t next_bitrate_decr;
} BitrateContext;

/*
 * Output structure with debug/overlay information
 */
typedef struct {
    int new_bitrate;      // Computed bitrate (rounded to 100 Kbps)
    double throughput;    // Smoothed throughput
    int rtt;              // Current RTT
    int rtt_th_min;       // RTT threshold min
    int rtt_th_max;       // RTT threshold max
    int bs;               // Current buffer size
    int bs_th1;           // Buffer threshold 1 (light congestion)
    int bs_th2;           // Buffer threshold 2 (medium congestion)
    int bs_th3;           // Buffer threshold 3 (heavy congestion)
} BitrateResult;

/*
 * Initialize a bitrate context with configuration values
 */
void bitrate_context_init(BitrateContext *ctx, int min_br, int max_br, int latency, int pkt_size);

/*
 * Update the bitrate based on current SRT statistics
 *
 * Parameters:
 *   ctx         - Bitrate context (holds state)
 *   buffer_size - Current SRT send buffer size (packets)
 *   rtt         - Current round-trip time (ms)
 *   send_rate_mbps - Current send rate from SRT stats (Mbps)
 *   timestamp   - Current timestamp in milliseconds
 *   result      - Output structure (can be NULL if debug info not needed)
 *
 * Returns:
 *   The new bitrate in bps (rounded to 100 Kbps), or -1 if no update
 */
int bitrate_update(BitrateContext *ctx, int buffer_size, double rtt,
                   double send_rate_mbps, uint64_t timestamp, BitrateResult *result);

#endif /* BITRATE_CONTROL_H */
