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

/*
 * Test fakes and stubs for integration testing
 *
 * This provides fake implementations of GStreamer and SRT dependencies
 * to allow testing balancer logic without actual hardware/network.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "test_fakes.h"

// Fake GStreamer pipeline state
static FakeGstPipeline fake_pipeline = {0};

// Fake SRT client state
static FakeSrtClient fake_srt = {0};

/*
 * Fake GStreamer functions
 */

void fake_gst_init(void) {
    memset(&fake_pipeline, 0, sizeof(fake_pipeline));
    fake_pipeline.encoder_bitrate = 0;
}

FakeGstElement* fake_gst_bin_get_by_name(FakeGstPipeline *pipeline, const char *name) {
    if (strcmp(name, "venc_bps") == 0) {
        return &fake_pipeline.encoder;
    } else if (strcmp(name, "overlay") == 0) {
        return &fake_pipeline.overlay;
    }
    return NULL;
}

void fake_g_object_set(FakeGstElement *element, const char *property, int value) {
    if (element == &fake_pipeline.encoder && strcmp(property, "bps") == 0) {
        fake_pipeline.encoder_bitrate = value;
    }
}

int fake_gst_element_is_valid(FakeGstElement *element) {
    return (element == &fake_pipeline.encoder || element == &fake_pipeline.overlay) ? 1 : 0;
}

int fake_get_encoder_bitrate(void) {
    return fake_pipeline.encoder_bitrate;
}

/*
 * Fake SRT functions
 */

void fake_srt_init(void) {
    memset(&fake_srt, 0, sizeof(fake_srt));
    fake_srt.connected = 0;
    fake_srt.buffer_size = 0;
    fake_srt.rtt = 50.0;
    fake_srt.send_rate = 5.0;
}

int fake_srt_connect(const char *host, const char *port, int latency) {
    (void)host;
    (void)port;
    fake_srt.connected = 1;
    fake_srt.latency = latency;
    return 0;
}

int fake_srt_get_stats(SRT_TRACEBSTATS *stats) {
    if (!fake_srt.connected) {
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    stats->msRTT = fake_srt.rtt;
    stats->mbpsSendRate = fake_srt.send_rate;
    stats->pktSndLossTotal = fake_srt.pkt_loss;
    stats->pktRetransTotal = fake_srt.pkt_retrans;

    return 0;
}

int fake_srt_get_sockopt_buffer_size(int *buffer_size) {
    *buffer_size = fake_srt.buffer_size;
    return 0;
}

void fake_srt_set_network_conditions(int buffer_size, double rtt, double send_rate) {
    fake_srt.buffer_size = buffer_size;
    fake_srt.rtt = rtt;
    fake_srt.send_rate = send_rate;
}

void fake_srt_set_packet_loss(int64_t loss, int64_t retrans) {
    fake_srt.pkt_loss = loss;
    fake_srt.pkt_retrans = retrans;
}

void fake_srt_close(void) {
    fake_srt.connected = 0;
}
