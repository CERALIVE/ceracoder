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
 * SRT Integration Tests
 *
 * These tests verify actual SRT network operations using a local listener.
 * They require the SRT library to be available.
 *
 * Test structure:
 * 1. Start an SRT listener thread
 * 2. Connect using srt_client module
 * 3. Send/receive data
 * 4. Verify behavior
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <srt.h>
#include "srt_client.h"

#define TEST_PORT 19875
#define TEST_LATENCY 500
#define TEST_PKT_SIZE 1316

/*
 * SRT Listener Thread Data
 */
typedef struct {
    int port;
    volatile int running;
    volatile int client_connected;
    volatile int bytes_received;
    pthread_t thread;
    SRTSOCKET listener;
} SrtListenerContext;

/*
 * SRT Listener Thread - accepts one connection and receives data
 */
static void *srt_listener_thread(void *arg) {
    SrtListenerContext *ctx = (SrtListenerContext *)arg;

    // Create listener socket
    ctx->listener = srt_create_socket();
    if (ctx->listener == SRT_INVALID_SOCK) {
        fprintf(stderr, "Listener: Failed to create socket\n");
        return NULL;
    }

    // Set latency
    int latency = TEST_LATENCY;
    srt_setsockflag(ctx->listener, SRTO_LATENCY, &latency, sizeof(latency));

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(ctx->port);

    if (srt_bind(ctx->listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Listener: Failed to bind: %s\n", srt_getlasterror_str());
        srt_close(ctx->listener);
        return NULL;
    }

    // Listen
    if (srt_listen(ctx->listener, 1) != 0) {
        fprintf(stderr, "Listener: Failed to listen: %s\n", srt_getlasterror_str());
        srt_close(ctx->listener);
        return NULL;
    }

    ctx->running = 1;

    // Accept with timeout
    struct sockaddr_in client_addr;
    int addrlen = sizeof(client_addr);

    // Use epoll for timeout handling
    int epid = srt_epoll_create();
    int events = SRT_EPOLL_IN;
    srt_epoll_add_usock(epid, ctx->listener, &events);

    SRTSOCKET ready[2];
    int rlen = 2;

    // Wait up to 5 seconds for connection
    int timeout_ms = 5000;
    int ret = srt_epoll_wait(epid, ready, &rlen, NULL, NULL, timeout_ms, NULL, NULL, NULL, NULL);

    if (ret < 0 || rlen == 0) {
        fprintf(stderr, "Listener: No connection received (timeout)\n");
        srt_epoll_release(epid);
        srt_close(ctx->listener);
        ctx->running = 0;
        return NULL;
    }

    SRTSOCKET client = srt_accept(ctx->listener, (struct sockaddr *)&client_addr, &addrlen);
    if (client == SRT_INVALID_SOCK) {
        fprintf(stderr, "Listener: Accept failed: %s\n", srt_getlasterror_str());
        srt_epoll_release(epid);
        srt_close(ctx->listener);
        ctx->running = 0;
        return NULL;
    }

    ctx->client_connected = 1;

    // Receive data
    char buffer[2048];
    int total_received = 0;

    // Re-add client socket to epoll
    srt_epoll_add_usock(epid, client, &events);

    while (ctx->running) {
        rlen = 2;
        ret = srt_epoll_wait(epid, ready, &rlen, NULL, NULL, 1000, NULL, NULL, NULL, NULL);

        if (ret < 0) {
            break;
        }

        if (rlen > 0) {
            int received = srt_recv(client, buffer, sizeof(buffer));
            if (received > 0) {
                total_received += received;
                ctx->bytes_received = total_received;
            } else if (received < 0) {
                // Connection closed or error
                break;
            }
        }
    }

    srt_epoll_release(epid);
    srt_close(client);
    srt_close(ctx->listener);
    ctx->running = 0;

    return NULL;
}

/*
 * Start SRT listener in background thread
 */
static int start_listener(SrtListenerContext *ctx, int port) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->port = port;
    ctx->running = 0;
    ctx->client_connected = 0;
    ctx->bytes_received = 0;

    if (pthread_create(&ctx->thread, NULL, srt_listener_thread, ctx) != 0) {
        return -1;
    }

    // Wait for listener to start
    for (int i = 0; i < 50; i++) {  // 5 seconds max
        if (ctx->running) {
            return 0;
        }
        usleep(100000);  // 100ms
    }

    return -1;
}

/*
 * Stop SRT listener
 */
static void stop_listener(SrtListenerContext *ctx) {
    ctx->running = 0;
    pthread_join(ctx->thread, NULL);
}

/*
 * Test: Connect to local SRT listener
 */
static void test_connect_to_local_listener(void **state) {
    (void)state;

    SrtListenerContext listener;
    int ret = start_listener(&listener, TEST_PORT);
    assert_int_equal(ret, 0);

    // Connect using our srt_client module
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT);

    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL, TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);

    // Verify connection
    assert_true(client.socket >= 0);
    assert_true(client.latency > 0);

    // Wait for listener to see connection
    for (int i = 0; i < 20; i++) {
        if (listener.client_connected) break;
        usleep(100000);
    }
    assert_true(listener.client_connected);

    // Cleanup
    srt_client_close(&client);
    stop_listener(&listener);
}

/*
 * Test: Send data and verify receipt
 */
static void test_send_data_verified(void **state) {
    (void)state;

    SrtListenerContext listener;
    int ret = start_listener(&listener, TEST_PORT + 1);
    assert_int_equal(ret, 0);

    // Connect
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 1);

    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL, TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);

    // Wait for connection
    for (int i = 0; i < 20; i++) {
        if (listener.client_connected) break;
        usleep(100000);
    }
    assert_true(listener.client_connected);

    // Send test data
    char test_data[1316];
    memset(test_data, 'A', sizeof(test_data));

    int sent = srt_client_send(&client, test_data, sizeof(test_data));
    assert_int_equal(sent, sizeof(test_data));

    // Send more data
    for (int i = 0; i < 10; i++) {
        test_data[0] = 'B' + i;
        sent = srt_client_send(&client, test_data, sizeof(test_data));
        assert_int_equal(sent, sizeof(test_data));
    }

    // Wait for data to be received
    usleep(500000);  // 500ms

    // Verify data was received
    assert_true(listener.bytes_received >= sizeof(test_data));

    // Cleanup
    srt_client_close(&client);
    stop_listener(&listener);
}

/*
 * Test: Get statistics after sending data
 */
static void test_get_stats_after_send(void **state) {
    (void)state;

    SrtListenerContext listener;
    int ret = start_listener(&listener, TEST_PORT + 2);
    assert_int_equal(ret, 0);

    // Connect
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 2);

    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL, TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);

    // Wait for connection
    for (int i = 0; i < 20; i++) {
        if (listener.client_connected) break;
        usleep(100000);
    }

    // Send some data
    char test_data[1316];
    memset(test_data, 'X', sizeof(test_data));
    for (int i = 0; i < 5; i++) {
        srt_client_send(&client, test_data, sizeof(test_data));
    }

    usleep(200000);  // 200ms

    // Get stats
    SRT_TRACEBSTATS stats;
    ret = srt_client_get_stats(&client, &stats);
    assert_int_equal(ret, 0);

    // Verify some stats are populated
    // (RTT should be very low for localhost)
    assert_true(stats.msRTT >= 0);

    // Cleanup
    srt_client_close(&client);
    stop_listener(&listener);
}

/*
 * Test: Connection failure - no listener
 */
static void test_connection_failure_no_listener(void **state) {
    (void)state;

    // Try to connect to a port with no listener
    SrtClient client;
    int ret = srt_client_connect(&client, "127.0.0.1", "19999", NULL, TEST_LATENCY, TEST_PKT_SIZE);

    // Should fail (non-zero return)
    assert_true(ret != 0);
}

/*
 * Test: Connection failure - invalid host
 */
static void test_connection_failure_invalid_host(void **state) {
    (void)state;

    SrtClient client;
    int ret = srt_client_connect(&client, "invalid.host.that.does.not.exist.local",
                                  "4000", NULL, TEST_LATENCY, TEST_PKT_SIZE);

    // Should fail
    assert_true(ret != 0);
}

/*
 * Test: Connect with stream ID
 */
static void test_connect_with_stream_id(void **state) {
    (void)state;

    SrtListenerContext listener;
    int ret = start_listener(&listener, TEST_PORT + 3);
    assert_int_equal(ret, 0);

    // Connect with stream ID
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 3);

    ret = srt_client_connect(&client, "127.0.0.1", port_str, "test_stream_123",
                              TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);

    // Connection should work
    assert_true(client.socket >= 0);

    // Cleanup
    srt_client_close(&client);
    stop_listener(&listener);
}

/*
 * Test: Get socket options
 */
static void test_get_socket_options(void **state) {
    (void)state;

    SrtListenerContext listener;
    int ret = start_listener(&listener, TEST_PORT + 4);
    assert_int_equal(ret, 0);

    // Connect
    SrtClient client;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", TEST_PORT + 4);

    ret = srt_client_connect(&client, "127.0.0.1", port_str, NULL, TEST_LATENCY, TEST_PKT_SIZE);
    assert_int_equal(ret, 0);

    // Get send buffer data
    int buffer_data;
    int optlen = sizeof(buffer_data);
    ret = srt_client_get_sockopt(&client, SRTO_SNDDATA, &buffer_data, &optlen);
    assert_int_equal(ret, 0);
    assert_true(buffer_data >= 0);

    // Cleanup
    srt_client_close(&client);
    stop_listener(&listener);
}

int main(void) {
    // Initialize SRT library
    srt_client_init();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_connect_to_local_listener),
        cmocka_unit_test(test_send_data_verified),
        cmocka_unit_test(test_get_stats_after_send),
        cmocka_unit_test(test_connection_failure_no_listener),
        cmocka_unit_test(test_connection_failure_invalid_host),
        cmocka_unit_test(test_connect_with_stream_id),
        cmocka_unit_test(test_get_socket_options),
    };

    int result = cmocka_run_group_tests(tests, NULL, NULL);

    // Cleanup SRT library
    srt_client_cleanup();

    return result;
}
