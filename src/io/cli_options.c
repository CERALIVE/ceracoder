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

#include "cli_options.h"
#include "balancer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

// Settings ranges
#define MAX_AV_DELAY 10000
#define MIN_SRT_LATENCY 100
#define MAX_SRT_LATENCY 10000
#define DEF_SRT_LATENCY 2000

// Parse a string to long with full error checking
static int parse_long(const char *str, long *result, long min_val, long max_val) {
    if (str == NULL || *str == '\0') {
        return -1;
    }
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str) {
        return -1;
    }
    while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r') {
        endptr++;
    }
    if (*endptr != '\0') {
        return -1;
    }
    if (val < min_val || val > max_val) {
        return -1;
    }
    *result = val;
    return 0;
}

void cli_options_print_usage(void) {
    fprintf(stderr, "Syntax: ceracoder PIPELINE_FILE ADDR PORT [options]\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v                  Print the version and exit\n");
    fprintf(stderr, "  -c <config file>    Configuration file (INI format)\n");
    fprintf(stderr, "  -d <delay>          Audio-video delay in milliseconds\n");
    fprintf(stderr, "  -s <streamid>       SRT stream ID\n");
    fprintf(stderr, "  -l <latency>        SRT latency in milliseconds\n");
    fprintf(stderr, "  -r                  Reduced SRT packet size\n");
    fprintf(stderr, "  -b <bitrate file>   Bitrate settings file (legacy, use -c instead)\n");
    fprintf(stderr, "  -a <algorithm>      Bitrate balancer algorithm (overrides config)\n\n");
    fprintf(stderr, "Config file example:\n");
    fprintf(stderr, "  [general]\n");
    fprintf(stderr, "  min_bitrate = 500    # Kbps\n");
    fprintf(stderr, "  max_bitrate = 6000   # Kbps (6 Mbps)\n");
    fprintf(stderr, "  balancer = adaptive\n\n");
    fprintf(stderr, "  [srt]\n");
    fprintf(stderr, "  latency = 2000       # ms\n\n");
    fprintf(stderr, "Send SIGHUP to reload configuration while running.\n\n");
    balancer_print_available();
}

int cli_options_parse(CliOptions *opts, int argc, char **argv) {
    memset(opts, 0, sizeof(*opts));
    opts->srt_latency = DEF_SRT_LATENCY;
    opts->av_delay = 0;
    opts->reduced_pkt_size = 0;

    int opt;
    while ((opt = getopt(argc, argv, "a:c:d:b:s:l:rv")) != -1) {
        switch (opt) {
            case 'a':
                opts->balancer_name = optarg;
                break;
            case 'b':
                opts->bitrate_file = optarg;
                break;
            case 'c':
                opts->config_file = optarg;
                break;
            case 'd': {
                long delay;
                if (parse_long(optarg, &delay, -MAX_AV_DELAY, MAX_AV_DELAY) != 0) {
                    fprintf(stderr, "Invalid delay value. Maximum sound delay +/- %d\n\n", MAX_AV_DELAY);
                    cli_options_print_usage();
                    exit(EXIT_FAILURE);
                }
                opts->av_delay = (int)delay;
                break;
            }
            case 's':
                opts->stream_id = optarg;
                break;
            case 'l': {
                long latency;
                if (parse_long(optarg, &latency, MIN_SRT_LATENCY, MAX_SRT_LATENCY) != 0) {
                    fprintf(stderr, "Invalid latency value. Must be between %d and %d ms\n\n",
                            MIN_SRT_LATENCY, MAX_SRT_LATENCY);
                    cli_options_print_usage();
                    exit(EXIT_FAILURE);
                }
                opts->srt_latency = (int)latency;
                break;
            }
            case 'r':
                opts->reduced_pkt_size = 1;
                break;
            case 'v':
                printf(VERSION "\n");
                exit(EXIT_SUCCESS);
            default:
                cli_options_print_usage();
                exit(EXIT_FAILURE);
        }
    }

    // Check for required positional arguments
    #define FIXED_ARGS 3
    if (argc - optind != FIXED_ARGS) {
        cli_options_print_usage();
        exit(EXIT_FAILURE);
    }

    opts->pipeline_file = argv[optind];
    opts->srt_host = argv[optind + 1];
    opts->srt_port = argv[optind + 2];

    return 0;
}
