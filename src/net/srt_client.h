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

#ifndef SRT_CLIENT_H
#define SRT_CLIENT_H

#include <srt.h>
#include <stdint.h>

/*
 * SRT client module - manages SRT socket connection and data transmission
 *
 * This module encapsulates all SRT-specific logic, providing a clean
 * interface for connecting, sending data, and retrieving statistics.
 */

// SRT configuration
#define SRT_MAX_OHEAD 20     // maximum SRT transmission overhead

typedef struct {
    SRTSOCKET socket;
    int latency;             // Negotiated latency (ms)
    int packet_size;         // SRT packet size (bytes)
} SrtClient;

/*
 * Initialize SRT library (must be called before any other SRT functions)
 */
void srt_client_init(void);

/*
 * Connect to SRT listener with retry logic
 *
 * Returns 0 on success, < 0 on error
 */
int srt_client_connect(SrtClient *client, const char *host, const char *port,
                       const char *stream_id, int latency, int pkt_size);

/*
 * Send data over SRT connection
 *
 * Returns number of bytes sent, or < 0 on error
 */
int srt_client_send(SrtClient *client, const void *data, int size);

/*
 * Get SRT socket statistics
 *
 * Returns 0 on success, < 0 on error
 */
int srt_client_get_stats(SrtClient *client, SRT_TRACEBSTATS *stats);

/*
 * Get SRT socket option
 *
 * Returns 0 on success, < 0 on error
 */
int srt_client_get_sockopt(SrtClient *client, SRT_SOCKOPT opt, void *optval, int *optlen);

/*
 * Close SRT connection
 */
void srt_client_close(SrtClient *client);

/*
 * Cleanup SRT library (should be called at program exit)
 */
void srt_client_cleanup(void);

#endif /* SRT_CLIENT_H */
