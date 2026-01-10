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

#include "pipeline_loader.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int pipeline_file_load(PipelineFile *pfile, const char *filename) {
    pfile->launch_string = NULL;
    pfile->length = 0;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open the pipeline file %s: ", filename);
        perror("");
        return -1;
    }

    pfile->length = lseek(fd, 0, SEEK_END);
    if (pfile->length == 0) {
        fprintf(stderr, "The pipeline file is empty, exiting\n");
        close(fd);
        return -2;
    }

    pfile->launch_string = mmap(0, pfile->length, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  // mmap keeps its own reference

    if (pfile->launch_string == MAP_FAILED) {
        pfile->launch_string = NULL;
        pfile->length = 0;
        return -3;
    }

    fprintf(stderr, "Gstreamer pipeline: %s\n", pfile->launch_string);
    return 0;
}

GstPipeline* pipeline_create(const PipelineFile *pfile) {
    GError *error = NULL;
    GstPipeline *pipeline = (GstPipeline*)gst_parse_launch(pfile->launch_string, &error);
    
    if (pipeline == NULL) {
        fprintf(stderr, "Failed to parse launch: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }
    
    if (error) {
        g_error_free(error);
    }

    return pipeline;
}

void pipeline_file_unload(PipelineFile *pfile) {
    if (pfile->launch_string != NULL) {
        munmap(pfile->launch_string, pfile->length);
        pfile->launch_string = NULL;
        pfile->length = 0;
    }
}
