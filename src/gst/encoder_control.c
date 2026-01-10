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

#include "encoder_control.h"
#include <stdio.h>

int encoder_control_init(EncoderControl *enc, GstPipeline *pipeline) {
    enc->element = NULL;
    enc->bitrate_div = 1;
    enc->current_bitrate = 0;

    // Try to find encoder by name (bps first, then kbps)
    enc->element = gst_bin_get_by_name(GST_BIN(pipeline), "venc_bps");
    if (!GST_IS_ELEMENT(enc->element)) {
        enc->element = gst_bin_get_by_name(GST_BIN(pipeline), "venc_kbps");
        enc->bitrate_div = 1000;
    }

    if (!GST_IS_ELEMENT(enc->element)) {
        fprintf(stderr, "Failed to get an encoder element from the pipeline, "
                        "no dynamic bitrate control\n");
        enc->element = NULL;
        return -1;
    }

    return 0;
}

int encoder_control_set_bitrate(EncoderControl *enc, int bitrate_bps) {
    if (!GST_IS_ELEMENT(enc->element)) {
        return -1;
    }

    // Only update if changed
    if (bitrate_bps != enc->current_bitrate) {
        enc->current_bitrate = bitrate_bps;
        g_object_set(G_OBJECT(enc->element), "bps", bitrate_bps / enc->bitrate_div, NULL);
    }

    return 0;
}

int encoder_control_available(const EncoderControl *enc) {
    return GST_IS_ELEMENT(enc->element) ? 1 : 0;
}
