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

#include "bitrate_control.h"
#include <glib.h>  // for MIN/MAX macros

// Use GLib's MIN/MAX which are type-safe and don't double-evaluate
#define min(a, b) MIN((a), (b))
#define max(a, b) MAX((a), (b))
#define min_max(a, l, h) (MAX(MIN((a), (h)), (l)))

// Convert RTT to expected buffer size based on throughput
#define RTT_TO_BS(ctx, rtt) ((ctx->throughput / 8) * (rtt) / ctx->srt_pkt_size)

void bitrate_context_init(BitrateContext *ctx, int min_br, int max_br,
                          int latency, int pkt_size,
                          int incr_step, int decr_step,
                          int incr_interval, int decr_interval) {
    // Configuration
    ctx->min_bitrate = min_br;
    ctx->max_bitrate = max_br;
    ctx->srt_latency = latency;
    ctx->srt_pkt_size = pkt_size;

    // Tuning parameters (use defaults if 0)
    ctx->incr_step = (incr_step > 0) ? incr_step : BITRATE_INCR_MIN;
    ctx->decr_step = (decr_step > 0) ? decr_step : BITRATE_DECR_MIN;
    ctx->incr_interval = (incr_interval > 0) ? incr_interval : BITRATE_INCR_INT;
    ctx->decr_interval = (decr_interval > 0) ? decr_interval : BITRATE_DECR_INT;
    ctx->decr_fast_interval = BITRATE_DECR_FAST_INT;  // Not configurable yet

    // Start at max bitrate
    ctx->cur_bitrate = max_br;

    // Buffer size tracking
    ctx->bs_avg = 0.0;
    ctx->bs_jitter = 0.0;
    ctx->prev_bs = 0;

    // RTT tracking
    ctx->rtt_avg = 0.0;
    ctx->rtt_min = RTT_MIN_INITIAL;
    ctx->rtt_jitter = 0.0;
    ctx->rtt_avg_delta = 0.0;
    ctx->prev_rtt = RTT_INITIAL;

    // Throughput tracking
    ctx->throughput = 0.0;

    // Packet loss tracking
    ctx->prev_pkt_loss = 0;
    ctx->prev_pkt_retrans = 0;
    ctx->loss_rate = 0.0;

    // Timing
    ctx->next_bitrate_incr = 0;
    ctx->next_bitrate_decr = 0;
}

// Packet loss detection threshold
#define LOSS_RATE_THRESHOLD 0.5   // Trigger congestion if losing > 0.5 packets/interval
#define EMA_LOSS 0.9              // Smoothing for loss rate
#define EMA_LOSS_NEW 0.1

int bitrate_update(BitrateContext *ctx, int buffer_size, double rtt,
                   double send_rate_mbps, uint64_t timestamp,
                   int64_t pkt_loss_total, int64_t pkt_retrans_total,
                   BitrateResult *result) {
    int bs = buffer_size;
    int rtt_int = (int)rtt;

    /*
     * Packet loss tracking
     */
    int64_t loss_delta = pkt_loss_total - ctx->prev_pkt_loss;
    int64_t retrans_delta = pkt_retrans_total - ctx->prev_pkt_retrans;
    ctx->prev_pkt_loss = pkt_loss_total;
    ctx->prev_pkt_retrans = pkt_retrans_total;

    // Smooth the loss rate (packet losses per update interval)
    if (loss_delta > 0 || retrans_delta > 0) {
        double new_loss = (double)(loss_delta + retrans_delta);
        ctx->loss_rate = ctx->loss_rate * EMA_LOSS + new_loss * EMA_LOSS_NEW;
    } else {
        ctx->loss_rate *= EMA_LOSS;  // Decay when no loss
    }

    // Flag for packet loss congestion
    int pkt_loss_congestion = (ctx->loss_rate > LOSS_RATE_THRESHOLD);

    /*
     * Send buffer size stats
     */
    // Rolling average
    ctx->bs_avg = ctx->bs_avg * EMA_SLOW + (double)bs * EMA_FAST;

    // Update the buffer size jitter
    ctx->bs_jitter = EMA_SLOW * ctx->bs_jitter;
    int delta_bs = bs - ctx->prev_bs;
    if (delta_bs > ctx->bs_jitter) {
        ctx->bs_jitter = (double)delta_bs;
    }
    ctx->prev_bs = bs;

    /*
     * RTT stats
     */
    // Update the average RTT
    if (ctx->rtt_avg == 0.0) {
        ctx->rtt_avg = rtt;
    } else {
        ctx->rtt_avg = ctx->rtt_avg * EMA_SLOW + EMA_FAST * rtt;
    }

    // Update the average RTT delta
    double delta_rtt = rtt - (double)ctx->prev_rtt;
    ctx->rtt_avg_delta = ctx->rtt_avg_delta * EMA_RTT_DELTA + delta_rtt * EMA_RTT_DELTA_NEW;
    ctx->prev_rtt = rtt_int;

    // Update the minimum RTT
    ctx->rtt_min *= RTT_MIN_DRIFT;
    if (rtt_int != RTT_IGNORE_VALUE && rtt < ctx->rtt_min && ctx->rtt_avg_delta < 1.0) {
        ctx->rtt_min = rtt;
    }

    // Update the RTT jitter
    ctx->rtt_jitter *= EMA_SLOW;
    if (delta_rtt > ctx->rtt_jitter) {
        ctx->rtt_jitter = delta_rtt;
    }

    /*
     * Rolling average of the network throughput
     */
    ctx->throughput *= EMA_THROUGHPUT;
    ctx->throughput += (send_rate_mbps * 1000.0 * 1000.0 / 1024.0) * EMA_THROUGHPUT_NEW;

    /*
     * Compute thresholds
     */
    int bs_th3 = (ctx->bs_avg + ctx->bs_jitter) * BS_TH3_MULT;
    int bs_th2 = max(BS_TH_MIN, ctx->bs_avg + max(ctx->bs_jitter * BS_TH2_JITTER_MULT, ctx->bs_avg));
    bs_th2 = min(bs_th2, (int)RTT_TO_BS(ctx, ctx->srt_latency / 2));
    int bs_th1 = max(BS_TH_MIN, ctx->bs_avg + ctx->bs_jitter * BS_TH1_JITTER_MULT);
    int rtt_th_max = ctx->rtt_avg + max(ctx->rtt_jitter * RTT_JITTER_MULT, ctx->rtt_avg * RTT_AVG_PERCENT / 100);
    int rtt_th_min = ctx->rtt_min + max(RTT_MIN_JITTER, ctx->rtt_jitter * 2);

    /*
     * Bitrate decision logic
     *
     * Congestion signals (in priority order):
     * 1. Emergency: RTT >= latency/3 OR buffer > bs_th3
     * 2. Heavy: RTT > latency/5 OR buffer > bs_th2 OR packet loss
     * 3. Light: RTT > rtt_th_max OR buffer > bs_th1
     * 4. Stable: RTT < rtt_th_min AND RTT not rising AND no packet loss
     */
    // Use int64_t for bitrate calculations to prevent overflow at high bitrates
    int64_t bitrate = ctx->cur_bitrate;

    if (bitrate > ctx->min_bitrate && (rtt_int >= (ctx->srt_latency / 3) || bs > bs_th3)) {
        // Emergency: drop to minimum
        bitrate = ctx->min_bitrate;
        ctx->next_bitrate_decr = timestamp + ctx->decr_interval;

    } else if (timestamp > ctx->next_bitrate_decr &&
               (rtt_int > (ctx->srt_latency / 5) || bs > bs_th2 || pkt_loss_congestion)) {
        // Heavy congestion: fast decrease (now includes packet loss)
        bitrate -= ctx->decr_step + bitrate / BITRATE_DECR_SCALE;
        ctx->next_bitrate_decr = timestamp + ctx->decr_fast_interval;

    } else if (timestamp > ctx->next_bitrate_decr &&
               (rtt_int > rtt_th_max || bs > bs_th1)) {
        // Light congestion: slow decrease
        bitrate -= ctx->decr_step;
        ctx->next_bitrate_decr = timestamp + ctx->decr_interval;

    } else if (timestamp > ctx->next_bitrate_incr &&
               rtt_int < rtt_th_min && ctx->rtt_avg_delta < RTT_STABLE_DELTA &&
               !pkt_loss_congestion) {
        // Stable: increase (only if no packet loss)
        bitrate += ctx->incr_step + bitrate / BITRATE_INCR_SCALE;
        ctx->next_bitrate_incr = timestamp + ctx->incr_interval;
    }

    // Clamp to valid range
    bitrate = min_max(bitrate, (int64_t)ctx->min_bitrate, (int64_t)ctx->max_bitrate);
    ctx->cur_bitrate = (int)bitrate;

    // Round to 100 kbps
    int rounded_br = ctx->cur_bitrate / (100 * 1000) * (100 * 1000);

    // Fill result structure if provided
    if (result != NULL) {
        result->new_bitrate = rounded_br;
        result->throughput = ctx->throughput;
        result->rtt = rtt_int;
        result->rtt_th_min = rtt_th_min;
        result->rtt_th_max = rtt_th_max;
        result->bs = bs;
        result->bs_th1 = bs_th1;
        result->bs_th2 = bs_th2;
        result->bs_th3 = bs_th3;
    }

    return rounded_br;
}
