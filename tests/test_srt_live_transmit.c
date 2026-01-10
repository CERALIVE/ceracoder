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
 * SRT Live Transmit Integration Tests
 *
 * These tests verify SRT client functionality using the external
 * srt-live-transmit binary as a listener. Tests gracefully skip if
 * the binary is not available on the system.
 *
 * Test structure:
 * 1. Check if srt-live-transmit is available
 * 2. Spawn it as a listener outputting to /dev/null (discard mode)
 * 3. Connect using srt_client module
 * 4. Send test data and verify no errors
 * 5. Clean up processes
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "srt_client.h"

#define TEST_PORT 19876
#define TEST_LATENCY 500
#define TEST_PKT_SIZE 1316

/*
 * Check if srt-live-transmit is available on the system
 */
static int is_srt_live_transmit_available(void) {
    int ret = system("which srt-live-transmit > /dev/null 2>&1");
    return (ret == 0);
}

/*
 * Context for managing srt-live-transmit process
 */
typedef struct {
    pid_t pid;
    int port;
} SrtListenerContext;

/*
 * Start srt-live-transmit listener that discards output
 * Command: srt-live-transmit srt://127.0.0.1:<port>?mode=listener file://con > /dev/null
 */
static int start_srt_listener(SrtListenerContext *ctx, int port) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->port = port;
    
    // Fork and execute srt-live-transmit
    ctx->pid = fork();
    
    if (ctx->pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return -1;
    }
    
    if (ctx->pid == 0) {
        // Child process - execute srt-live-transmit
        char srt_url[256];
        
        snprintf(srt_url, sizeof(srt_url), 
                 "srt://127.0.0.1:%d?mode=listener&latency=%d", port, TEST_LATENCY);
        
        // Redirect stdout/stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        
        // Use file://con to write to stdout (redirected to /dev/null)
        execlp("srt-live-transmit", "srt-live-transmit", 
               srt_url, "file://con", NULL);
        
        // If execlp returns, it failed
        exit(1);
    }
    
    // Parent process - wait for listener to be ready
    // Give it time to bind to the port
    sleep(2);
    
    // Check if process is still running
    int status;
    pid_t result = waitpid(ctx->pid, &status, WNOHANG);
    if (result != 0) {
        // Process already exited - startup failed
        return -1;
    }
    
    return 0;
}

/*
 * Stop srt-live-transmit process
 */
static void stop_srt_listener(SrtListenerContext *ctx) {
    if (ctx->pid > 0) {
        // Send SIGTERM and wait
        kill(ctx->pid, SIGTERM);
        
        // Wait up to 2 seconds for graceful shutdown
        for (int i = 0; i < 20; i++) {
            int status;
            pid_t result = waitpid(ctx->pid, &status, WNOHANG);
            if (result != 0) {
                // Process exited
                ctx->pid = 0;
                return;
            }
            usleep(100000);  // 100ms
        }
        
        // Force kill if still running
        kill(ctx->pid, SIGKILL);
        waitpid(ctx->pid, NULL, 0);
        ctx->pid = 0;
    }
}

/*
 * Test: Connect to srt-live-transmit and send data
 */
static void test_connect_and_send_to_live_transmit(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    SrtListenerContext listener;
    int ret = start_srt_listener(&listener, TEST_PORT);
    if (ret != 0) {
        skip();  // Failed to start, skip test
    }
    
    // Connect using our srt_client module
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT);
    
    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL, 
                             TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);
    
    // Send test data (simulate TS packets)
    char test_data[TEST_PKT_SIZE];
    memset(test_data, 0x47, sizeof(test_data));  // 0x47 is TS sync byte
    
    // Send multiple packets - should complete without error
    for (int i = 0; i < 50; i++) {
        int sent = srt_client_send(&client, test_data, sizeof(test_data));
        assert_int_equal(sent, sizeof(test_data));
    }
    
    // Cleanup
    srt_client_close(&client);
    stop_srt_listener(&listener);
}

/*
 * Test: Send larger amount of data
 */
static void test_send_large_data_to_live_transmit(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    SrtListenerContext listener;
    int ret = start_srt_listener(&listener, TEST_PORT + 1);
    if (ret != 0) {
        skip();
    }
    
    // Connect
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 1);
    
    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL,
                             TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);
    
    // Send a larger stream (simulate ~1 second at 5 Mbps)
    char test_data[TEST_PKT_SIZE];
    memset(test_data, 0x47, sizeof(test_data));
    
    int packets_to_send = 500;  // ~650 KB
    
    for (int i = 0; i < packets_to_send; i++) {
        // Vary data slightly
        test_data[10] = (char)(i & 0xFF);
        int sent = srt_client_send(&client, test_data, sizeof(test_data));
        assert_int_equal(sent, sizeof(test_data));
        
        // Small delay to avoid overwhelming buffer
        if (i % 50 == 0) {
            usleep(10000);  // 10ms every 50 packets
        }
    }
    
    // Cleanup
    srt_client_close(&client);
    stop_srt_listener(&listener);
}

/*
 * Test: Connection failure - no listener running
 */
static void test_connection_failure_no_listener(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available (we still test our client)
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    // Try to connect to a port with no listener
    SrtClient client;
    int ret = srt_client_connect(&client, "127.0.0.1", "19998", NULL,
                                 TEST_LATENCY, TEST_PKT_SIZE);
    
    // Should fail (non-zero return)
    assert_true(ret != 0);
}

/*
 * Test: Connection failure - invalid host
 */
static void test_connection_failure_invalid_host(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available (we still test our client)
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    SrtClient client;
    int ret = srt_client_connect(&client, "invalid.nonexistent.host.local",
                                 "4000", NULL, TEST_LATENCY, TEST_PKT_SIZE);
    
    // Should fail
    assert_true(ret != 0);
}

/*
 * Test: Connect with stream ID
 */
static void test_connect_with_stream_id(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    SrtListenerContext listener;
    int ret = start_srt_listener(&listener, TEST_PORT + 2);
    if (ret != 0) {
        skip();
    }
    
    // Connect with stream ID
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 2);
    
    ret = srt_client_connect(&client, "127.0.0.1", port_str, "test_stream_456",
                             TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);
    
    // Send some data
    char test_data[TEST_PKT_SIZE];
    memset(test_data, 0x47, sizeof(test_data));
    
    for (int i = 0; i < 20; i++) {
        int sent = srt_client_send(&client, test_data, sizeof(test_data));
        assert_int_equal(sent, sizeof(test_data));
    }
    
    // Cleanup
    srt_client_close(&client);
    stop_srt_listener(&listener);
}

/*
 * Test: Verify statistics are available during transmission
 */
static void test_stats_during_transmission(void **state) {
    (void)state;
    
    // Skip if srt-live-transmit not available
    if (!is_srt_live_transmit_available()) {
        skip();
    }
    
    SrtListenerContext listener;
    int ret = start_srt_listener(&listener, TEST_PORT + 3);
    if (ret != 0) {
        skip();
    }
    
    // Connect
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 3);
    
    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL,
                             TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);
    
    // Send data and check stats periodically
    char test_data[TEST_PKT_SIZE];
    memset(test_data, 0x47, sizeof(test_data));
    
    SRT_TRACEBSTATS stats;
    
    for (int i = 0; i < 30; i++) {
        int sent = srt_client_send(&client, test_data, sizeof(test_data));
        assert_int_equal(sent, sizeof(test_data));
        
        if (i % 10 == 9) {
            ret = srt_client_get_stats(&client, &stats);
            assert_int_equal(ret, 0);
            
            // Verify some stats are reasonable
            assert_true(stats.msRTT >= 0);
            assert_true(stats.msRTT < 1000);  // Should be very low for localhost
        }
        
        usleep(20000);  // 20ms
    }
    
    // Cleanup
    srt_client_close(&client);
    stop_srt_listener(&listener);
}

int main(void) {
    // Initialize SRT library
    srt_client_init();
    
    // Check if srt-live-transmit is available
    if (!is_srt_live_transmit_available()) {
        printf("srt-live-transmit not found - all tests will be skipped\n");
        printf("Install srt-tools package to enable these tests\n");
    }
    
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_connect_and_send_to_live_transmit),
        cmocka_unit_test(test_send_large_data_to_live_transmit),
        cmocka_unit_test(test_connection_failure_no_listener),
        cmocka_unit_test(test_connection_failure_invalid_host),
        cmocka_unit_test(test_connect_with_stream_id),
        cmocka_unit_test(test_stats_during_transmission),
    };
    
    int result = cmocka_run_group_tests(tests, NULL, NULL);
    
    // Cleanup SRT library
    srt_client_cleanup();
    
    return result;
}
