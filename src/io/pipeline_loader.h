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

#ifndef PIPELINE_LOADER_H
#define PIPELINE_LOADER_H

#include <gst/gst.h>
#include <stddef.h>

/*
 * Pipeline loader module - loads GStreamer pipeline from file
 *
 * This module handles loading pipeline descriptions from files
 * and creating GStreamer pipelines from them.
 */

typedef struct {
    char *launch_string;
    size_t length;
} PipelineFile;

/*
 * Load pipeline file into memory
 *
 * Uses mmap for efficient loading. Returns 0 on success, < 0 on error.
 */
int pipeline_file_load(PipelineFile *pfile, const char *filename);

/*
 * Create GStreamer pipeline from loaded file
 *
 * Returns pipeline on success, NULL on error.
 */
GstPipeline* pipeline_create(const PipelineFile *pfile);

/*
 * Unload pipeline file
 */
void pipeline_file_unload(PipelineFile *pfile);

#endif /* PIPELINE_LOADER_H */
