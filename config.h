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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/*
 * Configuration structure for belacoder
 *
 * All bitrates are in Kbps in the config file, converted to bps internally.
 * Example: 6000 in config = 6 Mbps = 6,000,000 bps
 */

// Adaptive algorithm tuning
typedef struct {
    int incr_step;          // Bitrate increase step (Kbps, default: 30)
    int decr_step;          // Bitrate decrease step (Kbps, default: 100)
    int incr_interval;      // Min interval between increases (ms, default: 500)
    int decr_interval;      // Min interval between decreases (ms, default: 200)
    double loss_threshold;  // Packet loss threshold (default: 0.5)
} AdaptiveConfig;

// AIMD algorithm tuning
typedef struct {
    int incr_step;          // Additive increase (Kbps, default: 50)
    double decr_mult;       // Multiplicative decrease (default: 0.75)
    int incr_interval;      // Min interval between increases (ms, default: 500)
    int decr_interval;      // Min interval between decreases (ms, default: 200)
} AimdConfig;

// Main configuration
typedef struct {
    // General settings
    int min_bitrate;        // Minimum bitrate (Kbps, default: 300)
    int max_bitrate;        // Maximum bitrate (Kbps, default: 6000)
    char balancer[32];      // Algorithm name (default: "adaptive")

    // SRT settings
    int srt_latency;        // SRT latency (ms, default: 2000)
    char stream_id[256];    // SRT stream ID (optional)

    // Algorithm-specific settings
    AdaptiveConfig adaptive;
    AimdConfig aimd;
} BelacoderConfig;

/*
 * Initialize config with defaults
 */
void config_init_defaults(BelacoderConfig *cfg);

/*
 * Load config from file
 * Returns 0 on success, -1 on error
 */
int config_load(BelacoderConfig *cfg, const char *filename);

/*
 * Get bitrate in bps (converts from Kbps)
 */
static inline int config_bitrate_bps(int kbps) {
    return kbps * 1000;
}

#endif /* CONFIG_H */
