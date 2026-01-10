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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Default values
#define DEF_MIN_BITRATE     300     // Kbps
#define DEF_MAX_BITRATE     6000    // Kbps
#define DEF_SRT_LATENCY     2000    // ms
#define DEF_BALANCER        "adaptive"

// Adaptive defaults
#define DEF_ADAPTIVE_INCR_STEP      30      // Kbps
#define DEF_ADAPTIVE_DECR_STEP      100     // Kbps
#define DEF_ADAPTIVE_INCR_INT       500     // ms
#define DEF_ADAPTIVE_DECR_INT       200     // ms
#define DEF_ADAPTIVE_LOSS_TH        0.5

// AIMD defaults
#define DEF_AIMD_INCR_STEP          50      // Kbps
#define DEF_AIMD_DECR_MULT          0.75
#define DEF_AIMD_INCR_INT           500     // ms
#define DEF_AIMD_DECR_INT           200     // ms

void config_init_defaults(BelacoderConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    // General
    cfg->min_bitrate = DEF_MIN_BITRATE;
    cfg->max_bitrate = DEF_MAX_BITRATE;
    strncpy(cfg->balancer, DEF_BALANCER, sizeof(cfg->balancer) - 1);

    // SRT
    cfg->srt_latency = DEF_SRT_LATENCY;

    // Adaptive
    cfg->adaptive.incr_step = DEF_ADAPTIVE_INCR_STEP;
    cfg->adaptive.decr_step = DEF_ADAPTIVE_DECR_STEP;
    cfg->adaptive.incr_interval = DEF_ADAPTIVE_INCR_INT;
    cfg->adaptive.decr_interval = DEF_ADAPTIVE_DECR_INT;
    cfg->adaptive.loss_threshold = DEF_ADAPTIVE_LOSS_TH;

    // AIMD
    cfg->aimd.incr_step = DEF_AIMD_INCR_STEP;
    cfg->aimd.decr_mult = DEF_AIMD_DECR_MULT;
    cfg->aimd.incr_interval = DEF_AIMD_INCR_INT;
    cfg->aimd.decr_interval = DEF_AIMD_DECR_INT;
}

// Trim whitespace from both ends
static char* trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

// Parse a single key=value line within a section
static void parse_line(BelacoderConfig *cfg, const char *section,
                       const char *key, const char *value) {
    // [general] section
    if (strcmp(section, "general") == 0) {
        if (strcmp(key, "min_bitrate") == 0) {
            cfg->min_bitrate = atoi(value);
        } else if (strcmp(key, "max_bitrate") == 0) {
            cfg->max_bitrate = atoi(value);
        } else if (strcmp(key, "balancer") == 0) {
            strncpy(cfg->balancer, value, sizeof(cfg->balancer) - 1);
        }
    }
    // [srt] section
    else if (strcmp(section, "srt") == 0) {
        if (strcmp(key, "latency") == 0) {
            cfg->srt_latency = atoi(value);
        }
        // Note: stream_id is CLI-only (-s flag), not in config
    }
    // [adaptive] section
    else if (strcmp(section, "adaptive") == 0) {
        if (strcmp(key, "incr_step") == 0) {
            cfg->adaptive.incr_step = atoi(value);
        } else if (strcmp(key, "decr_step") == 0) {
            cfg->adaptive.decr_step = atoi(value);
        } else if (strcmp(key, "incr_interval") == 0) {
            cfg->adaptive.incr_interval = atoi(value);
        } else if (strcmp(key, "decr_interval") == 0) {
            cfg->adaptive.decr_interval = atoi(value);
        } else if (strcmp(key, "loss_threshold") == 0) {
            cfg->adaptive.loss_threshold = atof(value);
        }
    }
    // [aimd] section
    else if (strcmp(section, "aimd") == 0) {
        if (strcmp(key, "incr_step") == 0) {
            cfg->aimd.incr_step = atoi(value);
        } else if (strcmp(key, "decr_mult") == 0) {
            cfg->aimd.decr_mult = atof(value);
        } else if (strcmp(key, "incr_interval") == 0) {
            cfg->aimd.incr_interval = atoi(value);
        } else if (strcmp(key, "decr_interval") == 0) {
            cfg->aimd.decr_interval = atoi(value);
        }
    }
}

int config_load(BelacoderConfig *cfg, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        return -1;
    }

    char line[512];
    char section[64] = "general";  // Default section

    while (fgets(line, sizeof(line), f) != NULL) {
        char *trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        // Section header [section]
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (end != NULL) {
                *end = '\0';
                strncpy(section, trimmed + 1, sizeof(section) - 1);
            }
            continue;
        }

        // Key = value
        char *eq = strchr(trimmed, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = trim(trimmed);
            char *value = trim(eq + 1);
            parse_line(cfg, section, key, value);
        }
    }

    fclose(f);
    return 0;
}
