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

#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

/*
 * CLI options module - command-line argument parsing
 *
 * This module encapsulates all CLI option parsing logic,
 * providing a clean interface between main() and the rest
 * of the application.
 */

typedef struct {
    // Required arguments
    char *pipeline_file;
    char *srt_host;
    char *srt_port;

    // Optional arguments
    char *config_file;
    char *balancer_name;       // Overrides config
    char *bitrate_file;        // Legacy, overrides config
    char *stream_id;           // SRT stream identifier
    int srt_latency;           // SRT latency in ms
    int av_delay;              // Audio-video delay in ms
    int reduced_pkt_size;      // Use reduced SRT packet size (bool)
} CliOptions;

/*
 * Parse command-line arguments
 *
 * Returns 0 on success, exits on error.
 */
int cli_options_parse(CliOptions *opts, int argc, char **argv);

/*
 * Print usage and exit
 */
void cli_options_print_usage(void);

#endif /* CLI_OPTIONS_H */
