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

#include "overlay_ui.h"
#include <stdio.h>

int overlay_ui_init(OverlayUi *overlay, GstPipeline *pipeline) {
    overlay->element = gst_bin_get_by_name(GST_BIN(pipeline), "overlay");
    
    if (!GST_IS_ELEMENT(overlay->element)) {
        overlay->element = NULL;
        return -1;
    }

    return 0;
}

void overlay_ui_update(OverlayUi *overlay,
                       int set_bitrate, double throughput,
                       int rtt, int rtt_th_min, int rtt_th_max,
                       int bs, int bs_th1, int bs_th2, int bs_th3) {
    if (!GST_IS_ELEMENT(overlay->element)) {
        return;
    }

    char overlay_text[100];
    snprintf(overlay_text, 100, "  b: %5d/%5.0f rtt: %3d/%3d/%3d bs: %3d/%3d/%3d/%3d",
             set_bitrate/1000, throughput,
             rtt, rtt_th_min, rtt_th_max,
             bs, bs_th1, bs_th2, bs_th3);
    g_object_set(G_OBJECT(overlay->element), "text", overlay_text, NULL);
}

int overlay_ui_available(const OverlayUi *overlay) {
    return GST_IS_ELEMENT(overlay->element) ? 1 : 0;
}
