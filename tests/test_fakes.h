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

#ifndef TEST_FAKES_H
#define TEST_FAKES_H

#include <stdint.h>
#include <srt.h>

/*
 * Fake GStreamer types and functions
 */

typedef struct {
    int dummy;
} FakeGstElement;

typedef struct {
    FakeGstElement encoder;
    FakeGstElement overlay;
    int encoder_bitrate;
} FakeGstPipeline;

void fake_gst_init(void);
FakeGstElement* fake_gst_bin_get_by_name(FakeGstPipeline *pipeline, const char *name);
void fake_g_object_set(FakeGstElement *element, const char *property, int value);
int fake_gst_element_is_valid(FakeGstElement *element);
int fake_get_encoder_bitrate(void);

/*
 * Fake SRT types and functions
 */

typedef struct {
    int connected;
    int latency;
    int buffer_size;
    double rtt;
    double send_rate;
    int64_t pkt_loss;
    int64_t pkt_retrans;
} FakeSrtClient;

void fake_srt_init(void);
int fake_srt_connect(const char *host, const char *port, int latency);
int fake_srt_get_stats(SRT_TRACEBSTATS *stats);
int fake_srt_get_sockopt_buffer_size(int *buffer_size);
void fake_srt_set_network_conditions(int buffer_size, double rtt, double send_rate);
void fake_srt_set_packet_loss(int64_t loss, int64_t retrans);
void fake_srt_close(void);

#endif /* TEST_FAKES_H */
