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

/*
 * Integration tests for module interactions
 *
 * These tests verify that modules work together correctly,
 * including config reload, encoder control, and error handling.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "balancer_runner.h"
#include "cli_options.h"

/*
 * Test: Config loading and parsing
 */
static void test_config_load(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);

    // Verify defaults
    assert_int_equal(cfg.min_bitrate, 300);
    assert_int_equal(cfg.max_bitrate, 6000);
    assert_string_equal(cfg.balancer, "adaptive");
    assert_int_equal(cfg.srt_latency, 2000);

    // Adaptive defaults
    assert_int_equal(cfg.adaptive.incr_step, 30);
    assert_int_equal(cfg.adaptive.decr_step, 100);
    assert_int_equal(cfg.adaptive.incr_interval, 500);
    assert_int_equal(cfg.adaptive.decr_interval, 200);

    // AIMD defaults
    assert_int_equal(cfg.aimd.incr_step, 50);
    assert_true(cfg.aimd.decr_mult > 0.74 && cfg.aimd.decr_mult < 0.76);
}

/*
 * Test: Balancer initialization with config
 */
static void test_balancer_init_from_config(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 1000;
    cfg.max_bitrate = 5000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);
    assert_string_equal(balancer_runner_get_name(&runner), "adaptive");

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Balancer algorithm override via CLI
 */
static void test_balancer_cli_override(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    strcpy(cfg.balancer, "adaptive");

    // CLI override to AIMD
    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, "aimd", 2000, 1316);
    assert_int_equal(ret, 0);
    assert_string_equal(balancer_runner_get_name(&runner), "aimd");

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Balancer bounds update (config reload simulation)
 */
static void test_balancer_bounds_update(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Run balancer with good conditions
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    balancer_runner_step(&runner, &input);

    // Update bounds (simulating config reload)
    int new_min = 1000 * 1000;  // 1 Mbps
    int new_max = 3000 * 1000;  // 3 Mbps
    balancer_runner_update_bounds(&runner, new_min, new_max);

    // Continue running - should respect new bounds
    for (int i = 0; i < 20; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        assert_true(output.new_bitrate >= new_min);
        assert_true(output.new_bitrate <= new_max);
    }

    balancer_runner_cleanup(&runner);
}

/*
 * Test: End-to-end balancer flow with encoder updates
 */
static void test_end_to_end_balancer_flow(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Simulate network condition changes over time
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 0,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    int prev_bitrate = 0;
    int bitrate_changes = 0;

    // Phase 1: Good network
    for (int i = 0; i < 10; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);

        if (output.new_bitrate != prev_bitrate) {
            bitrate_changes++;
            prev_bitrate = output.new_bitrate;
        }
    }

    // Should have increased bitrate
    assert_true(bitrate_changes > 0);
    int good_network_bitrate = prev_bitrate;

    // Phase 2: Congestion
    input.buffer_size = 150;
    input.rtt = 400.0;
    bitrate_changes = 0;

    for (int i = 0; i < 10; i++) {
        input.timestamp += 250;
        BalancerOutput output = balancer_runner_step(&runner, &input);

        if (output.new_bitrate != prev_bitrate) {
            bitrate_changes++;
            prev_bitrate = output.new_bitrate;
        }
    }

    // Should have decreased bitrate
    assert_true(bitrate_changes > 0);
    assert_true(prev_bitrate < good_network_bitrate);

    // Phase 3: Recovery
    input.buffer_size = 20;
    input.rtt = 50.0;

    for (int i = 0; i < 15; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        prev_bitrate = output.new_bitrate;
    }

    // Should have increased again
    assert_true(prev_bitrate > cfg.min_bitrate * 1000);

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Config bitrate conversion (Kbps to bps)
 */
static void test_config_bitrate_conversion(void **state) {
    (void) state;

    assert_int_equal(config_bitrate_bps(500), 500000);
    assert_int_equal(config_bitrate_bps(6000), 6000000);
    assert_int_equal(config_bitrate_bps(1), 1000);
}

/*
 * Test: Multiple balancer switches
 */
static void test_balancer_algorithm_switching(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);

    // Test each algorithm
    const char *algorithms[] = {"adaptive", "fixed", "aimd"};

    for (int i = 0; i < 3; i++) {
        strcpy(cfg.balancer, algorithms[i]);

        BalancerRunner runner;
        int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
        assert_int_equal(ret, 0);
        assert_string_equal(balancer_runner_get_name(&runner), algorithms[i]);

        // Run a few steps
        BalancerInput input = {
            .buffer_size = 10,
            .rtt = 30.0,
            .send_rate_mbps = 5.0,
            .timestamp = 1000,
            .pkt_loss_total = 0,
            .pkt_retrans_total = 0
        };

        for (int j = 0; j < 5; j++) {
            input.timestamp += 500;
            BalancerOutput output = balancer_runner_step(&runner, &input);
            assert_true(output.new_bitrate > 0);
        }

        balancer_runner_cleanup(&runner);
    }
}

/*
 * Test: Stress test with rapid network changes
 */
static void test_rapid_network_changes(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 0,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    // Alternate between good and bad conditions rapidly
    for (int i = 0; i < 50; i++) {
        input.timestamp += 100;

        if (i % 4 < 2) {
            // Good conditions
            input.buffer_size = 5;
            input.rtt = 25.0;
        } else {
            // Bad conditions
            input.buffer_size = 200;
            input.rtt = 500.0;
        }

        BalancerOutput output = balancer_runner_step(&runner, &input);

        // Should always respect bounds
        assert_true(output.new_bitrate >= cfg.min_bitrate * 1000);
        assert_true(output.new_bitrate <= cfg.max_bitrate * 1000);
    }

    balancer_runner_cleanup(&runner);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_load),
        cmocka_unit_test(test_balancer_init_from_config),
        cmocka_unit_test(test_balancer_cli_override),
        cmocka_unit_test(test_balancer_bounds_update),
        cmocka_unit_test(test_end_to_end_balancer_flow),
        cmocka_unit_test(test_config_bitrate_conversion),
        cmocka_unit_test(test_balancer_algorithm_switching),
        cmocka_unit_test(test_rapid_network_changes),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
