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

#ifndef OVERLAY_UI_H
#define OVERLAY_UI_H

#include <gst/gst.h>

/*
 * Overlay UI module - manages on-screen text overlay for stats display
 *
 * This module provides a clean interface for updating the text overlay
 * with bitrate, RTT, and buffer statistics.
 */

typedef struct {
    GstElement *element;
} OverlayUi;

/*
 * Initialize overlay UI from pipeline
 *
 * Looks for "overlay" element. Returns 0 on success, -1 if not found.
 */
int overlay_ui_init(OverlayUi *overlay, GstPipeline *pipeline);

/*
 * Update overlay with current statistics
 *
 * All parameters are as reported by the balancer.
 */
void overlay_ui_update(OverlayUi *overlay,
                       int set_bitrate, double throughput,
                       int rtt, int rtt_th_min, int rtt_th_max,
                       int bs, int bs_th1, int bs_th2, int bs_th3);

/*
 * Check if overlay is available
 */
int overlay_ui_available(const OverlayUi *overlay);

#endif /* OVERLAY_UI_H */
