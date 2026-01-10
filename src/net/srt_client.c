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

#include "srt_client.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <netdb.h>

void srt_client_init(void) {
    srt_startup();
}

int srt_client_connect(SrtClient *client, const char *host, const char *port,
                       const char *stream_id, int latency, int pkt_size) {
    struct addrinfo hints;
    struct addrinfo *addrs;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    int ret = getaddrinfo(host, port, &hints, &addrs);
    if (ret != 0) {
        return -1;
    }

    client->socket = srt_create_socket();
    if (client->socket == SRT_INVALID_SOCK) {
        freeaddrinfo(addrs);
        return -2;
    }

#if SRT_MAX_OHEAD > 0
    // auto, based on input rate
    int64_t max_bw = 0;
    if (srt_setsockflag(client->socket, SRTO_MAXBW, &max_bw, sizeof(max_bw)) != 0) {
        fprintf(stderr, "Failed to set SRTO_MAXBW: %s\n", srt_getlasterror_str());
        freeaddrinfo(addrs);
        return -4;
    }

    // overhead(retransmissions)
    int32_t ohead = SRT_MAX_OHEAD;
    if (srt_setsockflag(client->socket, SRTO_OHEADBW, &ohead, sizeof(ohead)) != 0) {
        fprintf(stderr, "Failed to set SRTO_OHEADBW: %s\n", srt_getlasterror_str());
        freeaddrinfo(addrs);
        return -4;
    }
#endif

    if (srt_setsockflag(client->socket, SRTO_LATENCY, &latency, sizeof(latency)) != 0) {
        fprintf(stderr, "Failed to set SRTO_LATENCY: %s\n", srt_getlasterror_str());
        freeaddrinfo(addrs);
        return -4;
    }

    if (stream_id != NULL) {
        if (srt_setsockflag(client->socket, SRTO_STREAMID, stream_id, (int)strlen(stream_id)) != 0) {
            fprintf(stderr, "Failed to set SRTO_STREAMID: %s\n", srt_getlasterror_str());
            freeaddrinfo(addrs);
            return -4;
        }
    }

    int32_t algo = 1;
    if (srt_setsockflag(client->socket, SRTO_RETRANSMITALGO, &algo, sizeof(algo)) != 0) {
        fprintf(stderr, "Failed to set SRTO_RETRANSMITALGO: %s\n", srt_getlasterror_str());
        freeaddrinfo(addrs);
        return -4;
    }

    int connected = -3;
    for (struct addrinfo *addr = addrs; addr != NULL; addr = addr->ai_next) {
        ret = srt_connect(client->socket, addr->ai_addr, (int)addr->ai_addrlen);
        if (ret == 0) {
            connected = 0;

            int len = sizeof(client->latency);
            if (srt_getsockflag(client->socket, SRTO_PEERLATENCY, &client->latency, &len) != 0) {
                fprintf(stderr, "Warning: Failed to get SRTO_PEERLATENCY: %s\n", srt_getlasterror_str());
                client->latency = latency;
            }
            fprintf(stderr, "SRT connected to %s:%s. Negotiated latency: %d ms\n",
                    host, port, client->latency);
            break;
        }
        connected = srt_getrejectreason(client->socket);
    }
    freeaddrinfo(addrs);

    if (connected != 0) {
        return connected;
    }

    client->packet_size = pkt_size;
    return 0;
}

int srt_client_send(SrtClient *client, const void *data, int size) {
    return srt_send(client->socket, data, size);
}

int srt_client_get_stats(SrtClient *client, SRT_TRACEBSTATS *stats) {
    return srt_bstats(client->socket, stats, 1);
}

int srt_client_get_sockopt(SrtClient *client, SRT_SOCKOPT opt, void *optval, int *optlen) {
    return srt_getsockflag(client->socket, opt, optval, optlen);
}

void srt_client_close(SrtClient *client) {
    if (client->socket >= 0) {
        srt_close(client->socket);
        client->socket = -1;
    }
}

void srt_client_cleanup(void) {
    srt_cleanup();
}
