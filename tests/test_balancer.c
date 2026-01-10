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
 * Integration tests for balancer algorithms
 *
 * These tests verify that the balancer responds appropriately to
 * network conditions without requiring actual GStreamer/SRT.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>

#include "balancer.h"
#include "config.h"
#include "balancer_runner.h"

/*
 * Test: Adaptive balancer recovers bitrate after congestion on good network
 *
 * The adaptive algorithm starts at max_bitrate. This test verifies that
 * after congestion reduces bitrate, good conditions allow recovery.
 */
static void test_adaptive_recovers_on_good_network(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;   // 500 Kbps
    cfg.max_bitrate = 6000;  // 6000 Kbps
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // First, induce congestion to lower the bitrate
    BalancerInput input = {
        .buffer_size = 300,
        .rtt = 600.0,
        .send_rate_mbps = 2.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    for (int i = 0; i < 10; i++) {
        input.timestamp += 250;
        balancer_runner_step(&runner, &input);
    }

    // Get the reduced bitrate
    BalancerOutput reduced = balancer_runner_step(&runner, &input);
    int reduced_bitrate = reduced.new_bitrate;

    // Now simulate good network conditions
    input.buffer_size = 10;
    input.rtt = 30.0;
    input.send_rate_mbps = 5.0;

    int final_bitrate = 0;
    for (int i = 0; i < 30; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        final_bitrate = output.new_bitrate;
    }

    // Bitrate should have recovered (increased from reduced state)
    assert_true(final_bitrate > reduced_bitrate);
    assert_true(final_bitrate <= cfg.max_bitrate * 1000);

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Adaptive balancer decreases bitrate on congestion
 */
static void test_adaptive_decreases_on_congestion(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Start with good conditions to build up bitrate
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    // Build up bitrate
    for (int i = 0; i < 10; i++) {
        input.timestamp += 500;
        balancer_runner_step(&runner, &input);
    }

    // Get current bitrate
    BalancerOutput output = balancer_runner_step(&runner, &input);
    int high_bitrate = output.new_bitrate;

    // Now simulate congestion: high RTT, high buffer
    input.buffer_size = 200;
    input.rtt = 500.0;

    for (int i = 0; i < 10; i++) {
        input.timestamp += 250;  // Faster updates during congestion
        output = balancer_runner_step(&runner, &input);
    }

    // Bitrate should have decreased
    assert_true(output.new_bitrate < high_bitrate);
    assert_true(output.new_bitrate >= cfg.min_bitrate * 1000);

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Fixed balancer maintains constant bitrate
 */
static void test_fixed_maintains_constant_bitrate(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.max_bitrate = 4000;  // 4 Mbps
    strcpy(cfg.balancer, "fixed");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Test various network conditions - bitrate should stay constant
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 4.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    int expected_bitrate = 4000000;  // 4 Mbps in bps

    // Try good network
    input.timestamp += 1000;
    input.buffer_size = 5;
    input.rtt = 20.0;
    BalancerOutput output2 = balancer_runner_step(&runner, &input);
    assert_int_equal(output2.new_bitrate, expected_bitrate);

    // Try congested network
    input.timestamp += 1000;
    input.buffer_size = 200;
    input.rtt = 600.0;
    BalancerOutput output3 = balancer_runner_step(&runner, &input);
    assert_int_equal(output3.new_bitrate, expected_bitrate);

    balancer_runner_cleanup(&runner);
}

/*
 * Test: AIMD increases additively
 */
static void test_aimd_additive_increase(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "aimd");
    cfg.aimd.incr_step = 100;  // 100 Kbps per step

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Good network conditions
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    BalancerOutput prev_output = balancer_runner_step(&runner, &input);

    // Run steps and check additive increase
    for (int i = 0; i < 5; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);

        // Should increase by approximately the step size
        int diff = output.new_bitrate - prev_output.new_bitrate;
        if (diff > 0) {
            // Allow some rounding
            assert_in_range(diff, 50000, 150000);  // 50-150 Kbps
        }
        prev_output = output;
    }

    balancer_runner_cleanup(&runner);
}

/*
 * Test: AIMD decreases multiplicatively
 */
static void test_aimd_multiplicative_decrease(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "aimd");
    cfg.aimd.decr_mult = 0.75;  // Reduce to 75%

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Build up bitrate first
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    for (int i = 0; i < 10; i++) {
        input.timestamp += 500;
        balancer_runner_step(&runner, &input);
    }

    BalancerOutput high_output = balancer_runner_step(&runner, &input);
    int high_bitrate = high_output.new_bitrate;

    // Trigger congestion
    input.buffer_size = 200;
    input.rtt = 500.0;
    input.timestamp += 250;

    BalancerOutput low_output = balancer_runner_step(&runner, &input);

    // Should decrease multiplicatively (approximately 75%)
    double ratio = (double)low_output.new_bitrate / (double)high_bitrate;
    assert_in_range(ratio, 0.60, 0.85);  // Allow some margin

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Balancer respects min/max bounds
 */
static void test_balancer_respects_bounds(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 1000;  // 1 Mbps
    cfg.max_bitrate = 3000;  // 3 Mbps
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Try to push below minimum with severe congestion
    BalancerInput input = {
        .buffer_size = 500,
        .rtt = 800.0,
        .send_rate_mbps = 0.5,
        .timestamp = 1000,
        .pkt_loss_total = 100,
        .pkt_retrans_total = 50
    };

    for (int i = 0; i < 20; i++) {
        input.timestamp += 250;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        assert_true(output.new_bitrate >= cfg.min_bitrate * 1000);
    }

    // Try to push above maximum with perfect conditions
    input.buffer_size = 0;
    input.rtt = 10.0;
    input.send_rate_mbps = 10.0;
    input.pkt_loss_total = 0;
    input.pkt_retrans_total = 0;

    for (int i = 0; i < 50; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        assert_true(output.new_bitrate <= cfg.max_bitrate * 1000);
    }

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Packet loss triggers bitrate reduction
 */
static void test_packet_loss_triggers_reduction(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 500;
    cfg.max_bitrate = 6000;
    strcpy(cfg.balancer, "adaptive");

    BalancerRunner runner;
    int ret = balancer_runner_init(&runner, &cfg, NULL, 2000, 1316);
    assert_int_equal(ret, 0);

    // Build up bitrate with good conditions
    BalancerInput input = {
        .buffer_size = 10,
        .rtt = 30.0,
        .send_rate_mbps = 5.0,
        .timestamp = 1000,
        .pkt_loss_total = 0,
        .pkt_retrans_total = 0
    };

    for (int i = 0; i < 15; i++) {
        input.timestamp += 500;
        balancer_runner_step(&runner, &input);
    }

    BalancerOutput stable_output = balancer_runner_step(&runner, &input);
    int stable_bitrate = stable_output.new_bitrate;

    // Introduce packet loss
    input.pkt_loss_total = 50;
    input.pkt_retrans_total = 30;

    for (int i = 0; i < 10; i++) {
        input.timestamp += 250;
        input.pkt_loss_total += 5;
        input.pkt_retrans_total += 3;
        balancer_runner_step(&runner, &input);
    }

    BalancerOutput loss_output = balancer_runner_step(&runner, &input);

    // Bitrate should have decreased due to packet loss
    assert_true(loss_output.new_bitrate < stable_bitrate);

    balancer_runner_cleanup(&runner);
}

/*
 * Test: Min equals max enforces fixed bitrate
 */
static void test_min_equals_max_fixed_range(void **state) {
    (void) state;

    BelacoderConfig cfg;
    config_init_defaults(&cfg);
    cfg.min_bitrate = 3000; // Kbps
    cfg.max_bitrate = 3000; // Kbps
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

    for (int i = 0; i < 10; i++) {
        input.timestamp += 500;
        BalancerOutput output = balancer_runner_step(&runner, &input);
        assert_int_equal(output.new_bitrate, 3000000); // 3 Mbps in bps
    }

    balancer_runner_cleanup(&runner);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_adaptive_recovers_on_good_network),
        cmocka_unit_test(test_adaptive_decreases_on_congestion),
        cmocka_unit_test(test_fixed_maintains_constant_bitrate),
        cmocka_unit_test(test_aimd_additive_increase),
        cmocka_unit_test(test_aimd_multiplicative_decrease),
        cmocka_unit_test(test_balancer_respects_bounds),
        cmocka_unit_test(test_packet_loss_triggers_reduction),
        cmocka_unit_test(test_min_equals_max_fixed_range),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
