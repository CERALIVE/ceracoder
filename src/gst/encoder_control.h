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

#ifndef ENCODER_CONTROL_H
#define ENCODER_CONTROL_H

#include <gst/gst.h>

/*
 * Encoder control module - manages video encoder bitrate updates
 *
 * This module provides an abstraction over GStreamer encoder elements,
 * allowing the balancer to update bitrate without knowing GStreamer details.
 */

typedef struct {
    GstElement *element;
    int bitrate_div;         // Divisor: 1 for bps, 1000 for kbps
    int current_bitrate;     // Cached current bitrate (bps)
} EncoderControl;

/*
 * Initialize encoder control from pipeline
 *
 * Looks for "venc_bps" or "venc_kbps" elements and determines units.
 * Returns 0 on success, -1 if no encoder found.
 */
int encoder_control_init(EncoderControl *enc, GstPipeline *pipeline);

/*
 * Set encoder bitrate
 *
 * Only updates if the bitrate has changed from last call.
 * Returns 0 on success, -1 if encoder not available.
 */
int encoder_control_set_bitrate(EncoderControl *enc, int bitrate_bps);

/*
 * Check if encoder is available
 */
int encoder_control_available(const EncoderControl *enc);

#endif /* ENCODER_CONTROL_H */
