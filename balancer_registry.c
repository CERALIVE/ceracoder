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
 * Balancer registry - manages available algorithms
 */

#include "balancer.h"
#include <stdio.h>
#include <string.h>

/*
 * External algorithm definitions
 */
extern const BalancerAlgorithm balancer_adaptive;

/*
 * Registry of all available algorithms
 * First entry is the default
 */
static const BalancerAlgorithm* const algorithms[] = {
    &balancer_adaptive,
    // Add new algorithms here
    NULL  // Sentinel
};

/*
 * Get the default algorithm (first in registry)
 */
const BalancerAlgorithm* balancer_get_default(void) {
    return algorithms[0];
}

/*
 * Find algorithm by name
 */
const BalancerAlgorithm* balancer_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; algorithms[i] != NULL; i++) {
        if (strcmp(algorithms[i]->name, name) == 0) {
            return algorithms[i];
        }
    }

    return NULL;
}

/*
 * Get array of all registered algorithms
 */
const BalancerAlgorithm* const* balancer_list_all(void) {
    return algorithms;
}

/*
 * Print list of available algorithms to stderr
 */
void balancer_print_available(void) {
    fprintf(stderr, "Available balancer algorithms:\n");
    for (int i = 0; algorithms[i] != NULL; i++) {
        fprintf(stderr, "  %-12s - %s\n",
                algorithms[i]->name,
                algorithms[i]->description);
    }
}
