/*
    ceracoder - live video encoder with dynamic bitrate control
    Forked from irlserver/belacoder, itself a fork of BELABOX/belacoder
    
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <gst/app/gstappsink.h>
#include <glib-unix.h>

#include <srt.h>
#include <srt/access_control.h>

#include "cli_options.h"
#include "config.h"
#include "srt_client.h"
#include "srt_reconnect.h"
#include "pipeline_loader.h"
#include "encoder_control.h"
#include "overlay_ui.h"
#include "balancer_runner.h"
#include "bitrate_control.h"
#include "sd_notify.h"

// SRT ACK timeout
#define SRT_ACK_TIMEOUT 6000 // maximum interval between received ACKs before the connection is TOed

// Packet size constants
#define TS_PKT_SIZE 188
#define REDUCED_SRT_PKT_SIZE ((TS_PKT_SIZE)*6)
#define DEFAULT_SRT_PKT_SIZE ((TS_PKT_SIZE)*7)

// Use GLib's MIN/MAX which are type-safe and don't double-evaluate
#define min(a, b) MIN((a), (b))
#define max(a, b) MAX((a), (b))
#define min_max(a, l, h) (MAX(MIN((a), (h)), (l)))

//#define DEBUG 1
#ifdef DEBUG
  #define debug(...) fprintf (stderr, __VA_ARGS__)
#else
  #define debug(...)
#endif

// Global state
static GstPipeline *gst_pipeline = NULL;
static GMainLoop *loop;
static SrtClient srt_client;
static EncoderControl encoder_ctrl;
static OverlayUi overlay_ui;
static BalancerRunner balancer_runner;
static int quit = 0;
static int av_delay = 0;
static int srt_pkt_size = DEFAULT_SRT_PKT_SIZE;

/*
 * In-process SRT reconnect state (ADR-0005 device supervision model).
 *
 * On a transient SRT drop (send failure or ACK timeout) ceracoder retries the
 * connection in-process with exponential backoff instead of hard-exiting, so a
 * brief link blip no longer tears down the stream. The process exits non-zero
 * only when the bounded reconnect window is exhausted or a permanent reject is
 * classified, handing restart authority to systemd (Restart=on-failure).
 */
static ReconnectController reconnect_ctrl;
// Connection target captured at startup so the reconnect path can redial.
static const char *srt_host = NULL;
static const char *srt_port = NULL;
static const char *srt_stream_id = NULL;
static int srt_latency_ms = 0;
// Set by the appsink streaming thread on send failure; observed by the main
// loop's housekeeping to drive the reconnect. Treated like `quit`: a simple
// cross-thread flag in the style this codebase already uses.
static volatile sig_atomic_t srt_lost = 0;
// ACK-liveness tracking (file scope so a successful reconnect can reset it
// against the fresh socket's zeroed counters).
static uint64_t prev_ack_ts = 0;
static uint64_t prev_ack_count = 0;
// Process exit code: 0 for clean shutdown (SIGTERM/SIGINT), non-zero when an
// unrecoverable SRT failure must escalate to the systemd supervisor.
static volatile sig_atomic_t exit_code = EXIT_SUCCESS;

// Forward declarations for the reconnect machinery.
static void srt_reconnect_start(void);
gboolean srt_reconnect_attempt_cb(gpointer user_data);

// Configuration
static BelacoderConfig g_config;
static char *bitrate_filename = NULL;
static char *config_filename = NULL;

// Signal flag for async-signal-safe SIGHUP handling
volatile sig_atomic_t reload_config_flag = 0;

uint64_t getms() {
  struct timespec ts = {0, 0};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return ((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000);
}

// Parse a string to long with full error checking
static int parse_long(const char *str, long *result, long min_val, long max_val) {
  if (str == NULL || *str == '\0') {
    return -1;
  }
  char *endptr;
  int saved_errno = errno;
  errno = 0;
  long val = strtol(str, &endptr, 10);
  if (errno != 0 || endptr == str) {
    errno = saved_errno;
    return -1;
  }
  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r') {
    endptr++;
  }
  if (*endptr != '\0') {
    errno = saved_errno;
    return -1;
  }
  if (val < min_val || val > max_val) {
    errno = saved_errno;
    return -1;
  }
  *result = val;
  errno = saved_errno;
  return 0;
}

// Forward declaration
int read_bitrate_file(void);

/* Attempts to stop the gstreamer pipeline cleanly */
void stop() {
  if (!quit) {
    quit = 1;
    alarm(3);
    g_main_loop_quit(loop);
  }
}

/* Stop with a specific process exit code. A non-zero code escalates an
   unrecoverable failure to the systemd supervisor (Restart=on-failure),
   per ADR-0005. The first non-zero code wins so it survives a later clean
   stop() triggered while the main loop unwinds. */
void stop_with_code(int code) {
  if (code != EXIT_SUCCESS && exit_code == EXIT_SUCCESS) {
    exit_code = code;
  }
  stop();
}

// Async-signal-safe handler for SIGHUP
void sighup_handler(int sig) {
  (void)sig;
  reload_config_flag = 1;
}

// GLib signal handler for SIGTERM/SIGINT (called from main loop, not signal context)
gboolean stop_from_signal(gpointer user_data) {
  (void)user_data;
  stop();
  return G_SOURCE_REMOVE;
}

/*
  This checks periodically for pipeline stalls. The alsasrc element tends to stall rather
  than error out when the input resolution changes for a live input into a Camlink 4K
  connected to a Jetson Nano. If you see this happening in other scenarios, please report it
*/
gboolean stall_check(gpointer data) {
  /* This will handle any signals delivered between setting up the handler and
     starting the loop. Couldn't find another way to avoid races / potentially
     losing signals */
  if (quit) {
    stop();
    return TRUE;
  }

  // Check for SIGHUP-triggered config reload
  if (reload_config_flag) {
    reload_config_flag = 0;
    int min_bitrate;
    int max_bitrate;
    int reloaded = 0;

    // Reload config file if specified
    if (config_filename != NULL) {
      if (config_load(&g_config, config_filename) == 0) {
        min_bitrate = config_bitrate_bps(g_config.min_bitrate);
        max_bitrate = config_bitrate_bps(g_config.max_bitrate);
        balancer_runner_update_bounds(&balancer_runner, min_bitrate, max_bitrate);
        fprintf(stderr, "Config reloaded: %d - %d Kbps\n",
                min_bitrate / 1000, max_bitrate / 1000);
        reloaded = 1;
      } else {
        fprintf(stderr, "Failed to reload config file: %s\n", config_filename);
      }
    }

    // Also reload legacy bitrate file if specified
    if (bitrate_filename && !reloaded) {
      read_bitrate_file();
    }
  }

  static gint64 prev_pos = -1;
  gint64 pos;
  if (!gst_element_query_position((GstElement *)gst_pipeline, GST_FORMAT_TIME, &pos))
    return TRUE;

  if (pos != -1 && pos == prev_pos) {
    fprintf(stderr, "Pipeline stall detected. Will exit now\n");
    stop();
  }

  prev_pos = pos;
  return TRUE;
}

int parse_bitrate(const char *bitrate_string) {
  long bitrate;
  if (parse_long(bitrate_string, &bitrate, MIN_BITRATE, ABS_MAX_BITRATE) != 0) {
    return -1;
  }
  return (int)bitrate;
}

int read_bitrate_file() {
  FILE *f = fopen(bitrate_filename, "r");
  if (f == NULL) return -1;

  char *buf = NULL;
  size_t buf_sz = 0;
  int br[2];

  for (int i = 0; i < 2; i++) {
    ssize_t len = getline(&buf, &buf_sz, f);
    if (len < 0) goto ret_err;
    br[i] = parse_bitrate(buf);
    if (br[i] < 0) goto ret_err;
  }

  free(buf);
  fclose(f);
  
  balancer_runner_update_bounds(&balancer_runner, br[0], br[1]);
  return 0;

ret_err:
  if (buf) free(buf);
  fclose(f);
  return -2;
}

void do_bitrate_update(SRT_TRACEBSTATS *stats, uint64_t ctime) {
  // Get send buffer size from SRT
  int bs = -1;
  int sz = sizeof(bs);
  int ret = srt_client_get_sockopt(&srt_client, SRTO_SNDDATA, &bs, &sz);
  if (ret != 0 || bs < 0) return;

  // Prepare input for balancer
  BalancerInput input = {
    .buffer_size = bs,
    .rtt = stats->msRTT,
    .send_rate_mbps = stats->mbpsSendRate,
    .timestamp = ctime,
    .pkt_loss_total = stats->pktSndLossTotal,
    .pkt_retrans_total = stats->pktRetransTotal
  };

  // Call the balancer algorithm
  BalancerOutput output = balancer_runner_step(&balancer_runner, &input);

  // Update the overlay display
  overlay_ui_update(&overlay_ui, output.new_bitrate, output.throughput,
                    output.rtt, output.rtt_th_min, output.rtt_th_max,
                    output.bs, output.bs_th1, output.bs_th2, output.bs_th3);

  // Set encoder bitrate
  encoder_control_set_bitrate(&encoder_ctrl, output.new_bitrate);
}

/*
  One in-process reconnect attempt, scheduled as a one-shot GLib timeout after
  the backoff delay (keeps the main loop signal-responsive between attempts).
  Adaptive-bitrate and encoder state are intentionally untouched — only the SRT
  socket is rebuilt. Re-arms itself for the next attempt; on a permanent reject
  or exhausted window it escalates to systemd via a clean non-zero exit.
*/
gboolean srt_reconnect_attempt_cb(gpointer user_data) {
  (void)user_data;
  if (quit) return G_SOURCE_REMOVE;

  int attempt = reconnect_attempt_count(&reconnect_ctrl);
  int ret = srt_client_connect(&srt_client, srt_host, srt_port,
                               srt_stream_id, srt_latency_ms, srt_pkt_size);
  if (ret == 0) {
    reconnect_succeeded(&reconnect_ctrl);
    // Reset ACK-liveness so the dead socket's stale timestamp can't immediately
    // re-trigger the timeout against the fresh socket's zeroed counters.
    prev_ack_ts = 0;
    prev_ack_count = 0;
    srt_lost = 0;
    fprintf(stderr, "SRT reconnected on attempt %d (total reconnects this run: %d)\n",
            attempt, reconnect_total_reconnects(&reconnect_ctrl));
    return G_SOURCE_REMOVE;
  }

  // A reject the server will not retract: stop retrying, hand off to systemd.
  if (reconnect_reason_is_permanent(ret)) {
    fprintf(stderr, "SRT reconnect: permanent failure (reason %d); "
                    "exiting for supervisor restart\n", ret);
    stop_with_code(EXIT_FAILURE);
    return G_SOURCE_REMOVE;
  }

  long backoff = reconnect_next_backoff_ms(&reconnect_ctrl);
  if (backoff < 0) {
    fprintf(stderr, "SRT reconnect window exhausted after %d attempts; "
                    "exiting for supervisor restart\n", attempt);
    stop_with_code(EXIT_FAILURE);
    return G_SOURCE_REMOVE;
  }

  fprintf(stderr, "SRT reconnect attempt %d failed (reason %d); "
                  "retrying in %ld ms\n", attempt, ret, backoff);
  g_timeout_add((guint)backoff, srt_reconnect_attempt_cb, NULL);
  return G_SOURCE_REMOVE;
}

/* Begin a reconnect episode: drop the dead socket, arm the backoff machine, and
   schedule the first attempt. Idempotent while a reconnect is already running. */
static void srt_reconnect_start(void) {
  if (quit) return;
  if (reconnect_is_reconnecting(&reconnect_ctrl)) return;

  srt_client_close(&srt_client);
  reconnect_begin(&reconnect_ctrl);

  long backoff = reconnect_next_backoff_ms(&reconnect_ctrl);
  if (backoff < 0) {
    fprintf(stderr, "SRT reconnect disabled by configuration; "
                    "exiting for supervisor restart\n");
    stop_with_code(EXIT_FAILURE);
    return;
  }

  fprintf(stderr, "SRT connection lost; starting in-process reconnect "
                  "(attempt %d in %ld ms, backoff cap %u ms, %s window)\n",
          reconnect_attempt_count(&reconnect_ctrl), backoff,
          RECONNECT_DEFAULT_MAX_MS,
          reconnect_ctrl.max_attempts > 0 ? "bounded" : "unlimited");
  g_timeout_add((guint)backoff, srt_reconnect_attempt_cb, NULL);
}

gboolean connection_housekeeping(gpointer user_data) {
  (void)user_data;
  if (quit) return TRUE;

  uint64_t ctime = getms();

  // Socket is closed/redialing during a reconnect — skip stats until it settles.
  if (reconnect_is_reconnecting(&reconnect_ctrl)) {
    return TRUE;
  }

  // srt_lost is raised by the appsink thread; the main loop owns the reconnect
  // so every GLib timer is scheduled from a single thread.
  if (srt_lost) {
    srt_reconnect_start();
    return TRUE;
  }

  // SRT stats
  SRT_TRACEBSTATS stats;
  int ret = srt_client_get_stats(&srt_client, &stats);
  if (ret != 0) goto r;

  // Track when the most recent ACK was received
  if (stats.pktRecvACKTotal != prev_ack_count) {
    prev_ack_count = stats.pktRecvACKTotal;
    prev_ack_ts = ctime;
  }
  /* ACK timeout now triggers in-process reconnect instead of a hard exit, so a
     transient ACK gap no longer tears down the stream (ADR-0005). */
  if (prev_ack_count != 0 && (ctime - prev_ack_ts) > SRT_ACK_TIMEOUT) {
    fprintf(stderr, "The SRT connection timed out; attempting in-process reconnect\n");
    srt_lost = 1;
    srt_reconnect_start();
    return TRUE;
  }

  // Update bitrate when we have a configurable encoder
  if (encoder_control_available(&encoder_ctrl)) {
    do_bitrate_update(&stats, ctime);
  }

r:
  return TRUE;
}

GstFlowReturn new_buf_cb(GstAppSink *sink, gpointer user_data) {
  static char pkt[DEFAULT_SRT_PKT_SIZE];
  static int pkt_len = 0;
  GstFlowReturn code = GST_FLOW_OK;

  GstSample *sample = gst_app_sink_pull_sample(sink);
  if (!sample) return GST_FLOW_ERROR;

  GstBuffer *buffer = NULL;
  GstMapInfo map = {0};

  buffer = gst_sample_get_buffer(sample);
  gst_buffer_map(buffer, &map, GST_MAP_READ);

  // Send srt_pkt_size packets, splitting and merging samples if needed
  int sample_sz = (int)map.size;
  do {
    int copy_sz = MIN(srt_pkt_size - pkt_len, sample_sz);
    memcpy((void *)pkt + pkt_len, map.data, copy_sz);
    pkt_len += copy_sz;

    if (pkt_len == srt_pkt_size) {
      /* While the connection is down we drop encoded packets rather than block
         or send on a dead socket; the main loop reconnects in-process. On a send
         failure we only raise srt_lost (no stop()) — systemd, not ceracoder,
         owns process restart (ADR-0005). The pipeline stays alive either way. */
      if (!srt_lost) {
        int nb = srt_client_send(&srt_client, pkt, srt_pkt_size);
        if (nb != srt_pkt_size && !quit) {
          fprintf(stderr, "SRT send failed; flagging in-process reconnect\n");
          srt_lost = 1;
        }
      }
      pkt_len = 0;
    }

    sample_sz -= copy_sz;
  } while(sample_sz);

  gst_buffer_unmap(buffer, &map);
  gst_sample_unref(sample);

  return code;
}

static void cb_delay (GstElement *identity, GstBuffer *buffer, gpointer data) {
  buffer = gst_buffer_make_writable(buffer);
  GST_BUFFER_PTS (buffer) += GST_SECOND * abs(av_delay) / 1000;
}

static int get_sink_framerate(GstElement *element, gint *numerator, gint *denominator) {
  int ret = -1;

  GstPad *pad = gst_element_get_static_pad(element, "sink");
  if (!pad) {
    return -1;
  }

  GstCaps *caps = gst_pad_get_current_caps(pad);
  if (caps != NULL) {
    if (gst_caps_is_fixed(caps)) {
      const GstStructure *str = gst_caps_get_structure (caps, 0);
      if (gst_structure_get_fraction(str, "framerate", numerator, denominator)) {
        ret = 0;
      }
    }

    gst_caps_unref(caps);
  }

  gst_object_unref(pad);
  return ret;
}

unsigned long pts = 0;
static void cb_ptsfixup(GstElement *identity, GstBuffer *buffer, gpointer data) {
  static long period = 0;
  static long prev_pts = 0;
  long input_pts = GST_BUFFER_PTS(buffer);

  // get rid of the DTS, the following elements should use the PTS
  GST_BUFFER_DTS(buffer) = 0;

  // First frame, obtain the framerate and initial PTS
  if (pts == 0) {
    int fr_numerator = 0;
    int fr_denominator = 0;
    if (get_sink_framerate(identity, &fr_numerator, &fr_denominator) == 0) {
      pts = input_pts;
      period = GST_SECOND * fr_denominator / fr_numerator;
      fprintf(stderr, "%s: framerate: %d / %d, period is %ld\n",
              __FUNCTION__, fr_numerator, fr_denominator, period);
    }

  // Subsequent frames, adjust the PTS
  } else {
    #define AVG_MULT 1000
    #define AVG_WEIGHT 3 // AVG_WEIGHT out of AVG_MULT
    #define AVG_PREV (AVG_MULT-AVG_WEIGHT)
    #define AVG_ROUNDING (AVG_MULT/2)
    /* Rolling average to account for slight differences from the nominal framerate
       and even slight drifting over time due to temperature or voltage variation
       Have to add AVG_ROUNDING to avoid precision loss due to dividing by AVG_MULT
    */
    period = ((period * AVG_PREV + AVG_ROUNDING) / AVG_MULT) +
             (((input_pts - prev_pts) * AVG_WEIGHT + AVG_ROUNDING) / AVG_MULT);

    /* As long as the input PTS is within 0 to 2.0 periods of the previous
       output PTS, assume that it was a continuous read at period ns from
       the previous frame and increment the PTS accordingly. Otherwise, handle
       the discontinuity by either dropping an input buffer or skipping an
       output period, as needed. */
    long diff = (long)(input_pts - pts);
    long incr = (diff/2 + period) / period * period;
    if (incr > 0) {
      pts += incr;
      debug("%s: in pts: %lu, out pts: %lu, incr %ld, diff %ld, period %ld\n",
             __FUNCTION__, GST_BUFFER_PTS(buffer), pts, incr, diff, period);
      GST_BUFFER_PTS(buffer) = pts;
    } else {
      debug("skipping frame: pts %lu, prev pts %lu, output pts: %lu, diff %ld\n",
             input_pts, prev_pts, pts, diff);
      GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DROPPABLE);
    }
  }

  prev_pts = input_pts;
}

void cb_pipeline (GstBus *bus, GstMessage *message, gpointer user_data) {
  switch(GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
      fprintf(stderr, "gstreamer error from %s\n", message->src->name);
      stop();
      break;
    case GST_MESSAGE_EOS:
      fprintf(stderr, "gstreamer eos from %s\n", message->src->name);
      stop();
      break;
    default:
      break;
  }
}

// Only called if the pipeline failed to stop in time. Preserve the chosen exit
// code so an unrecoverable-failure shutdown still exits non-zero for systemd,
// while a SIGINT/SIGTERM shutdown stays clean (exit_code defaults to SUCCESS).
void cb_sigalarm(int signum) {
  (void)signum;
  _exit(exit_code);
}

/*
  Pets the systemd watchdog (WatchdogSec=) from the GLib main loop.

  Per ADR-0005, systemd is the sole process-restart authority; ceracoder pets
  the watchdog so a hung or zombie process (main loop stuck) is killed and
  respawned. Reaching this callback proves the main loop is still dispatching —
  that is the liveness source wired here. Task 12 will additionally gate the
  ping on the encoded-frame production counter, so a frame-production stall
  (capture alive, no encoded frames) also fails the ping and triggers a restart.

  No-op when not supervised by systemd (NOTIFY_SOCKET unset).
*/
gboolean watchdog_ping(gpointer user_data) {
  (void)user_data;
  if (quit) {
    return G_SOURCE_REMOVE;
  }
  sd_notify_watchdog();
  return TRUE;
}

#define FIXED_ARGS 3
int main(int argc, char** argv) {
  CliOptions opts;
  PipelineFile pfile;
  
  // Parse command-line options
  cli_options_parse(&opts, argc, argv);

  // Set global state from options
  av_delay = opts.av_delay;
  srt_pkt_size = opts.reduced_pkt_size ? REDUCED_SRT_PKT_SIZE : DEFAULT_SRT_PKT_SIZE;
  config_filename = opts.config_file;
  bitrate_filename = opts.bitrate_file;

  // Load pipeline file
  if (pipeline_file_load(&pfile, opts.pipeline_file) != 0) {
    exit(EXIT_FAILURE);
  }

  // Initialize GStreamer and create pipeline
  gst_init(&argc, &argv);
  gst_pipeline = pipeline_create(&pfile);
  if (gst_pipeline == NULL) {
    pipeline_file_unload(&pfile);
    return -1;
  }

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(gst_pipeline));
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message", (GCallback)cb_pipeline, gst_pipeline);

  // Initialize configuration with defaults
  config_init_defaults(&g_config);

  // Load config file if specified
  if (config_filename != NULL) {
    if (config_load(&g_config, config_filename) != 0) {
      fprintf(stderr, "Failed to load config file: %s\n", config_filename);
      exit(EXIT_FAILURE);
    }
    fprintf(stderr, "Loaded config from %s\n", config_filename);
  }

  // Legacy bitrate file support (overrides config if both specified)
  if (bitrate_filename) {
    int ret = read_bitrate_file();
    if (ret != 0) {
      if (ret == -1) {
        fprintf(stderr, "Failed to read the bitrate settings file %s\n", bitrate_filename);
      } else {
        fprintf(stderr, "Failed to read valid bitrate settings from %s\n", bitrate_filename);
      }
      cli_options_print_usage();
      exit(EXIT_FAILURE);
    }
  }

  // Determine SRT latency (CLI -l takes precedence over config)
  int srt_latency = (opts.srt_latency != 2000) ? opts.srt_latency : 
                    (g_config.srt_latency > 0 ? g_config.srt_latency : 2000);

  // Capture the SRT target + arm the in-process reconnect machine so a transient
  // loss can redial without a process restart (ADR-0005). Default = bounded
  // window; pass RECONNECT_UNLIMITED to never escalate transient loss.
  srt_host = opts.srt_host;
  srt_port = opts.srt_port;
  srt_stream_id = opts.stream_id;
  srt_latency_ms = srt_latency;
  reconnect_init(&reconnect_ctrl, RECONNECT_DEFAULT_BASE_MS, RECONNECT_DEFAULT_MAX_MS,
                 RECONNECT_DEFAULT_MAX_ATTEMPTS);

  // Initialize balancer
  if (balancer_runner_init(&balancer_runner, &g_config, opts.balancer_name, 
                           srt_latency, srt_pkt_size) != 0) {
    exit(EXIT_FAILURE);
  }
  signal(SIGHUP, sighup_handler);

  // Initialize encoder control
  encoder_control_init(&encoder_ctrl, gst_pipeline);
  if (encoder_control_available(&encoder_ctrl)) {
    // Start at max bitrate
    encoder_control_set_bitrate(&encoder_ctrl, config_bitrate_bps(g_config.max_bitrate));
  }

  // Initialize overlay
  overlay_ui_init(&overlay_ui, gst_pipeline);
  overlay_ui_update(&overlay_ui, 0,0,0,0,0,0,0,0,0);

  // Optional sound delay via identity element
  fprintf(stderr, "A-V delay: %d ms\n", av_delay);
  GstElement *identity_elem = gst_bin_get_by_name(GST_BIN(gst_pipeline), 
                                                   av_delay >= 0 ? "a_delay" : "v_delay");
  if (GST_IS_ELEMENT(identity_elem)) {
    g_object_set(G_OBJECT(identity_elem), "signal-handoffs", TRUE, NULL);
    g_signal_connect(identity_elem, "handoff", G_CALLBACK(cb_delay), NULL);
  } else {
    fprintf(stderr, "Failed to get a delay element from the pipeline, not applying a delay\n");
  }

  // Optional video PTS interval fixup
  identity_elem = gst_bin_get_by_name(GST_BIN(gst_pipeline), "ptsfixup");
  if (GST_IS_ELEMENT(identity_elem)) {
    g_object_set(G_OBJECT(identity_elem), "signal-handoffs", TRUE, NULL);
    g_signal_connect(identity_elem, "handoff", G_CALLBACK(cb_ptsfixup), NULL);
  } else {
    fprintf(stderr, "Failed to get a ptsfixup element from the pipeline, "
                    "not removing PTS jitter\n");
  }

  // Setup SRT streaming via appsink
  GstAppSinkCallbacks callbacks = {NULL, NULL, new_buf_cb};
  GstElement *srt_app_sink = gst_bin_get_by_name(GST_BIN(gst_pipeline), "appsink");
  if (GST_IS_ELEMENT(srt_app_sink)) {
    gst_app_sink_set_callbacks(GST_APP_SINK(srt_app_sink), &callbacks, NULL, NULL);
    
    // Initialize SRT and connect
    srt_client_init();
    
    int ret_srt;
    do {
      ret_srt = srt_client_connect(&srt_client, opts.srt_host, opts.srt_port,
                                    opts.stream_id, srt_latency, srt_pkt_size);
      if (ret_srt != 0) {
        char *reason = NULL;
        switch (ret_srt) {
          case SRT_REJ_TIMEOUT:
            reason = "connection timed out";
            break;
          case SRT_REJX_CONFLICT:
            reason = "streamid already in use";
            break;
          case SRT_REJX_FORBIDDEN:
            reason = "invalid streamid";
            break;
          case -1:
            reason = "failed to resolve address";
            break;
          case -2:
            reason = "failed to open the SRT socket";
            break;
          case -4:
            reason = "failed to set SRT socket options";
            break;
          default:
            reason = "unknown";
            break;
        }
        fprintf(stderr, "Failed to establish an SRT connection: %s. Retrying...\n", reason);
        struct timespec retry_delay = { .tv_sec = 0, .tv_nsec = 500L * 1000L * 1000L };
        nanosleep(&retry_delay, NULL);
      }
    } while(ret_srt != 0);
  }

  // Monitor connection when using appsink
  if (GST_IS_ELEMENT(srt_app_sink)) {
    g_timeout_add(BITRATE_UPDATE_INT, connection_housekeeping, NULL);
  }

  // Setup main loop
  loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGTERM, stop_from_signal, NULL);
  g_unix_signal_add(SIGINT, stop_from_signal, NULL);
  signal(SIGALRM, cb_sigalarm);
  g_timeout_add(1000, stall_check, NULL);

  // Start pipeline
  gst_element_set_state((GstElement*)gst_pipeline, GST_STATE_PLAYING);

  /* Startup is complete: pipeline is PLAYING and (when using appsink) the SRT
     connection is established. Tell systemd we are READY so a Type=notify unit
     leaves the "activating" state. No-op when not run under systemd. */
  sd_notify_ready();

  /* If systemd configured a watchdog for this unit (WatchdogSec=), pet it from
     the main loop at half the configured interval (the interval systemd
     recommends). If the main loop hangs/zombies, the ping stops and systemd
     kills + respawns the process (ADR-0005). WatchdogSec is intentionally set
     larger than ceracoder's in-process SRT reconnect backoff cap so a
     legitimate reconnect is never mistaken for a hang. */
  unsigned long long wd_usec = sd_watchdog_usec();
  if (wd_usec > 0) {
    guint wd_interval_ms = (guint)((wd_usec / 1000ULL) / 2ULL);
    if (wd_interval_ms < 1) wd_interval_ms = 1;
    g_timeout_add(wd_interval_ms, watchdog_ping, NULL);
    fprintf(stderr,
            "systemd watchdog enabled: petting every %u ms (WatchdogSec=%llu s)\n",
            wd_interval_ms, wd_usec / 1000000ULL);
  }

  g_main_loop_run(loop);

  // Cleanup
  srt_client_close(&srt_client);
  gst_element_set_state((GstElement*)gst_pipeline, GST_STATE_NULL);
  srt_client_cleanup();
  balancer_runner_cleanup(&balancer_runner);
  pipeline_file_unload(&pfile);

  // Non-zero on unrecoverable SRT failure so systemd (Restart=on-failure)
  // respawns; zero on a clean SIGINT/SIGTERM shutdown (ADR-0005).
  return exit_code;
}
