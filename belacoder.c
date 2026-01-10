/*
    belacoder - live video encoder with dynamic bitrate control
    Copyright (C) 2020 BELABOX project

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
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include <gst/gst.h>
#include <gst/gstinfo.h>
#include <gst/app/gstappsink.h>
#include <glib-unix.h>

#include <srt.h>
#include <srt/access_control.h>

#include "bitrate_control.h"

// Ensure SRT version is at least 1.4.0 (required for SRTO_RETRANSMITALGO)
#ifndef SRT_VERSION_VALUE
#define SRT_VERSION_VALUE SRT_MAKE_VERSION_VALUE(SRT_VERSION_MAJOR, SRT_VERSION_MINOR, SRT_VERSION_PATCH)
#endif
#if SRT_VERSION_VALUE < SRT_MAKE_VERSION_VALUE(1, 4, 0)
#error "SRT 1.4.0 or later required (for SRTO_RETRANSMITALGO)"
#endif

// SRT configuration
#define SRT_MAX_OHEAD 20     // maximum SRT transmission overhead (when using appsink)
#define SRT_ACK_TIMEOUT 6000 // maximum interval between received ACKs before the connection is TOed

// Settings ranges
#define TS_PKT_SIZE 188
#define REDUCED_SRT_PKT_SIZE ((TS_PKT_SIZE)*6)
#define DEFAULT_SRT_PKT_SIZE ((TS_PKT_SIZE)*7)
#define MAX_AV_DELAY 10000
#define MIN_SRT_LATENCY 100
#define MAX_SRT_LATENCY 10000
#define DEF_SRT_LATENCY 2000

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

static GstPipeline *gst_pipeline = NULL;
GMainLoop *loop;
GstElement *encoder, *overlay;
SRTSOCKET sock = -1;
int quit = 0;

// Signal flag for async-signal-safe SIGHUP handling
volatile sig_atomic_t reload_bitrate_flag = 0;

int enc_bitrate_div = 1;

int av_delay = 0;

// Bitrate control context (replaces individual bitrate globals)
BitrateContext bitrate_ctx;
int min_bitrate = MIN_BITRATE;  // Keep for read_bitrate_file compatibility
int max_bitrate = DEF_BITRATE;  // Keep for read_bitrate_file compatibility

char *bitrate_filename = NULL;

int srt_latency = DEF_SRT_LATENCY;
int srt_pkt_size = DEFAULT_SRT_PKT_SIZE;

uint64_t getms() {
  struct timespec ts = {0, 0};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    // Should never happen, but handle gracefully
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// Parse a string to long with full error checking
// Returns 0 on success, -1 on parse error or out of range
int parse_long(const char *str, long *result, long min_val, long max_val) {
  if (str == NULL || *str == '\0') {
    return -1;
  }
  char *endptr;
  errno = 0;
  long val = strtol(str, &endptr, 10);
  // Check for conversion errors
  if (errno != 0 || endptr == str) {
    return -1;
  }
  // Allow trailing whitespace/newline but not other garbage
  while (*endptr == ' ' || *endptr == '\t' || *endptr == '\n' || *endptr == '\r') {
    endptr++;
  }
  if (*endptr != '\0') {
    return -1;
  }
  // Range check
  if (val < min_val || val > max_val) {
    return -1;
  }
  *result = val;
  return 0;
}

/* Attempts to stop the gstreamer pipeline cleanly
   Also sets up an alarm in case it doesn't */
void stop() {
  if (!quit) {
    quit = 1;
    alarm(3);
    g_main_loop_quit(loop);
  }
}

// Forward declarations
int read_bitrate_file(void);

// Async-signal-safe handler for SIGHUP - just sets a flag
void sighup_handler(int sig) {
  (void)sig;
  reload_bitrate_flag = 1;
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

  // Check for SIGHUP-triggered bitrate reload (async-signal-safe approach)
  if (reload_bitrate_flag) {
    reload_bitrate_flag = 0;
    if (bitrate_filename) {
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

void update_overlay(int set_bitrate, double throughput,
                    int rtt, int rtt_th_min, int rtt_th_max,
                    int bs, int bs_th1, int bs_th2, int bs_th3) {
  if (GST_IS_ELEMENT(overlay)) {
    char overlay_text[100];
    snprintf(overlay_text, 100, "  b: %5d/%5.0f rtt: %3d/%3d/%3d bs: %3d/%3d/%3d/%3d",
             set_bitrate/1000, throughput,
             rtt, rtt_th_min, rtt_th_max,
             bs, bs_th1, bs_th2, bs_th3);
    g_object_set (G_OBJECT(overlay), "text", overlay_text, NULL);
  }
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
    buf_sz = getline(&buf, &buf_sz, f);
    if (buf_sz < 0) goto ret_err;
    br[i] = parse_bitrate(buf);
    if (br[i] < 0) goto ret_err;
  }

  free(buf);
  fclose(f);
  min_bitrate = br[0];
  max_bitrate = br[1];
  // Update context if initialized
  bitrate_ctx.min_bitrate = min_bitrate;
  bitrate_ctx.max_bitrate = max_bitrate;
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
  int ret = srt_getsockflag(sock, SRTO_SNDDATA, &bs, &sz);
  if (ret != 0 || bs < 0) return;

  // Call the bitrate control module
  BitrateResult result;
  static int prev_set_bitrate = 0;

  int new_bitrate = bitrate_update(&bitrate_ctx, bs, stats->msRTT,
                                   stats->mbpsSendRate, ctime, &result);

  // Update the overlay display
  update_overlay(result.new_bitrate, result.throughput,
                 result.rtt, result.rtt_th_min, result.rtt_th_max,
                 result.bs, result.bs_th1, result.bs_th2, result.bs_th3);

  // Set encoder bitrate if changed
  if (new_bitrate != prev_set_bitrate) {
    prev_set_bitrate = new_bitrate;
    g_object_set(G_OBJECT(encoder), "bps", new_bitrate / enc_bitrate_div, NULL);
    debug("set bitrate to %d, internal value %d\n", new_bitrate, bitrate_ctx.cur_bitrate);
  }
}

gboolean connection_housekeeping(gpointer user_data) {
  (void)user_data;
  uint64_t ctime = getms();
  static uint64_t prev_ack_ts = 0;
  static uint64_t prev_ack_count = 0;

  // SRT stats
  SRT_TRACEBSTATS stats;
  int ret = srt_bstats(sock, &stats, 1);
  if (ret != 0) goto r;

  // Track when the most recent ACK was received
  if (stats.pktRecvACKTotal != prev_ack_count) {
    prev_ack_count = stats.pktRecvACKTotal;
    prev_ack_ts = ctime;
  }
  /* Manual check for connection timeout, because SRT is Pepega
     and will fail to timeout if RTT was high */
  if (prev_ack_count != 0 && (ctime - prev_ack_ts) > SRT_ACK_TIMEOUT) {
    fprintf(stderr, "The SRT connection timed out, exiting\n");
    stop();
  }

  // We can only update the bitrate when we have a configurable encoder
  if (GST_IS_ELEMENT(encoder)) {
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

  // We send srt_pkt_size size packets, splitting and merging samples if needed
  int sample_sz = map.size;
  do {
    int copy_sz = min(srt_pkt_size - pkt_len, sample_sz);
    memcpy((void *)pkt + pkt_len, map.data, copy_sz);
    pkt_len += copy_sz;

    if (pkt_len == srt_pkt_size) {
      int nb = srt_send(sock, pkt, srt_pkt_size);
      if (nb != srt_pkt_size) {
        if (!quit) {
          fprintf(stderr, "The SRT connection failed, exiting\n");
          stop();
        }
        code = GST_FLOW_ERROR;
        goto ret;
      }
      pkt_len = 0;
    }

    sample_sz -= copy_sz;
  } while(sample_sz);

ret:
  gst_buffer_unmap(buffer, &map);
  gst_sample_unref(sample);

  return code;
}

int parse_ip(struct sockaddr_in *addr, char *ip_str) {
  in_addr_t ip = inet_addr(ip_str);
  if (ip == -1) return -1;

  memset(addr, 0, sizeof(*addr));
  addr->sin_family = AF_INET; 
  addr->sin_addr.s_addr = ip;

  return 0;
}

int parse_ip_port(struct sockaddr_in *addr, char *ip_str, char *port_str) {
  if (parse_ip(addr, ip_str) != 0) return -1;

  long port;
  if (parse_long(port_str, &port, 1, 65535) != 0) return -2;
  addr->sin_port = htons((uint16_t)port);

  return 0;
}

int connect_srt(char *host, char *port, char *stream_id) {
  struct addrinfo hints;
  struct addrinfo *addrs;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  int ret = getaddrinfo(host, port, &hints, &addrs);
  if (ret != 0) return -1;

  sock = srt_create_socket();
  if (sock == SRT_INVALID_SOCK) return -2;

#if SRT_MAX_OHEAD > 0
  // auto, based on input rate
  int64_t max_bw = 0;
  if (srt_setsockflag(sock, SRTO_MAXBW, &max_bw, sizeof(max_bw)) != 0) {
    fprintf(stderr, "Failed to set SRTO_MAXBW: %s\n", srt_getlasterror_str());
    return -4;
  }

  // overhead(retransmissions)
  int32_t ohead = SRT_MAX_OHEAD;
  if (srt_setsockflag(sock, SRTO_OHEADBW, &ohead, sizeof(ohead)) != 0) {
    fprintf(stderr, "Failed to set SRTO_OHEADBW: %s\n", srt_getlasterror_str());
    return -4;
  }
#endif

  if (srt_setsockflag(sock, SRTO_LATENCY, &srt_latency, sizeof(srt_latency)) != 0) {
    fprintf(stderr, "Failed to set SRTO_LATENCY: %s\n", srt_getlasterror_str());
    return -4;
  }

  if (stream_id != NULL) {
    if (srt_setsockflag(sock, SRTO_STREAMID, stream_id, strlen(stream_id)) != 0) {
      fprintf(stderr, "Failed to set SRTO_STREAMID: %s\n", srt_getlasterror_str());
      return -4;
    }
  }

  int32_t algo = 1;
  if (srt_setsockflag(sock, SRTO_RETRANSMITALGO, &algo, sizeof(algo)) != 0) {
    fprintf(stderr, "Failed to set SRTO_RETRANSMITALGO: %s\n", srt_getlasterror_str());
    return -4;
  }

  int connected = -3;
  for (struct addrinfo *addr = addrs; addr != NULL; addr = addr->ai_next) {
    ret = srt_connect(sock, addr->ai_addr, addr->ai_addrlen);
    if (ret == 0) {
      connected = 0;

      int len = sizeof(srt_latency);
      if (srt_getsockflag(sock, SRTO_PEERLATENCY, &srt_latency, &len) != 0) {
        fprintf(stderr, "Warning: Failed to get SRTO_PEERLATENCY: %s\n", srt_getlasterror_str());
      }
      fprintf(stderr, "SRT connected to %s:%s. Negotiated latency: %d ms\n",
              host, port, srt_latency);
      break;
    }
    connected = srt_getrejectreason(sock);
  }
  freeaddrinfo(addrs);

  return connected;
}

void exit_syntax() {
  fprintf(stderr, "Syntax: belacoder PIPELINE_FILE ADDR PORT [options]\n\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -v                  Print the version and exit\n");
  fprintf(stderr, "  -d <delay>          Audio-video delay in milliseconds\n");
  fprintf(stderr, "  -s <streamid>       SRT stream ID\n");
  fprintf(stderr, "  -l <latency>        SRT latency in milliseconds\n");
  fprintf(stderr, "  -r                  Reduced SRT packet size\n");
  fprintf(stderr, "  -b <bitrate file>   Bitrate settings file, see below\n\n");
  fprintf(stderr, "Bitrate settings file syntax:\n");
  fprintf(stderr, "MIN BITRATE (bps)\n");
  fprintf(stderr, "MAX BITRATE (bps)\n---\n");
  fprintf(stderr, "example for 500 Kbps - 60000 Kbps:\n\n");
  fprintf(stderr, "    printf \"500000\\n6000000\" > bitrate_file\n\n");
  fprintf(stderr, "---\n");
  fprintf(stderr, "Send SIGHUP to reload the bitrate settings while running.\n");
  exit(EXIT_FAILURE);
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
    int fr_numerator = 0, fr_denominator = 0;
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
    period = (period * AVG_PREV + AVG_ROUNDING) / AVG_MULT +
             ((input_pts - prev_pts) * AVG_WEIGHT + AVG_ROUNDING)/ AVG_MULT;

    /* As long as the input PTS is within 0 to 2.0 periods of the previous
       output PTS, assume that it was a continuous read at period ns from
       the previous frame and increment the PTS accordingly. Otherwise, handle
       the discontinuity by either dropping an input buffer or skipping an
       output period, as needed. */
    long diff = input_pts - pts;
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

// Only called if the pipeline failed to stop
void cb_sigalarm(int signum) {
  _exit(EXIT_SUCCESS); // exiting deliberately following SIGINT or SIGTERM
}

#define FIXED_ARGS 3
int main(int argc, char** argv) {
  int opt;
  char *srt_host = NULL;
  char *srt_port = NULL;
  char *stream_id = NULL;
  srt_latency = DEF_SRT_LATENCY;

  while ((opt = getopt(argc, argv, "d:b:s:l:rv")) != -1) {
    switch (opt) {
      case 'b':
        bitrate_filename = optarg;
        break;
      case 'd': {
        long delay;
        if (parse_long(optarg, &delay, -MAX_AV_DELAY, MAX_AV_DELAY) != 0) {
          fprintf(stderr, "Invalid delay value. Maximum sound delay +/- %d\n\n", MAX_AV_DELAY);
          exit_syntax();
        }
        av_delay = (int)delay;
        break;
      }
      case 's':
        stream_id = optarg;
        break;
      case 'l': {
        long latency;
        if (parse_long(optarg, &latency, MIN_SRT_LATENCY, MAX_SRT_LATENCY) != 0) {
          fprintf(stderr, "Invalid latency value. Must be between %d and %d ms\n\n",
                  MIN_SRT_LATENCY, MAX_SRT_LATENCY);
          exit_syntax();
        }
        srt_latency = (int)latency;
        break;
      }
      case 'r':
        srt_pkt_size = REDUCED_SRT_PKT_SIZE;
        break;
      case 'v':
        printf(VERSION "\n");
        exit(EXIT_SUCCESS);
      default:
        exit_syntax();
    }
  }

  if (argc - optind != FIXED_ARGS) {
    exit_syntax();
  }


  // Read the pipeline file
  int pipeline_fd = open(argv[optind], O_RDONLY);
  if (pipeline_fd < 0) {
    fprintf(stderr, "Failed to open the pipeline file %s: ", argv[optind]);
    perror("");
    exit(EXIT_FAILURE);
  }
  size_t launch_string_len = lseek(pipeline_fd, 0, SEEK_END);
  if (launch_string_len == 0) {
    fprintf(stderr, "The pipeline file is empty, exiting\n");
    close(pipeline_fd);
    exit(EXIT_FAILURE);
  }
  char *launch_string = mmap(0, launch_string_len, PROT_READ, MAP_PRIVATE, pipeline_fd, 0);
  close(pipeline_fd);  // mmap keeps its own reference, fd no longer needed
  fprintf(stderr, "Gstreamer pipeline: %s\n", launch_string);

  gst_init (&argc, &argv);
  GError *error = NULL;
  gst_pipeline  = (GstPipeline*) gst_parse_launch(launch_string, &error);
  if (gst_pipeline == NULL) {
    fprintf(stderr, "Failed to parse launch: %s\n", error->message);
    return -1;
  }
  if (error) g_error_free(error);
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(gst_pipeline));
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message", (GCallback)cb_pipeline, gst_pipeline);


  // Optional dynamic video bitrate
  if (bitrate_filename) {
    int ret;
    if ((ret = read_bitrate_file()) != 0) {
      if (ret == -1) {
        fprintf(stderr, "Failed to read the bitrate settings file %s\n", bitrate_filename);
      } else {
        fprintf(stderr, "Failed to read valid bitrate settings from %s\n", bitrate_filename);
      }
      exit_syntax();
    }
  }
  // Initialize the bitrate controller
  bitrate_context_init(&bitrate_ctx, min_bitrate, max_bitrate, srt_latency, srt_pkt_size);
  fprintf(stderr, "Max bitrate: %d\n", max_bitrate);
  signal(SIGHUP, sighup_handler);

  encoder = gst_bin_get_by_name(GST_BIN(gst_pipeline), "venc_bps");
  if (!GST_IS_ELEMENT(encoder)) {
    encoder = gst_bin_get_by_name(GST_BIN(gst_pipeline), "venc_kbps");
    enc_bitrate_div = 1000;
  }
  if (GST_IS_ELEMENT(encoder)) {
    g_object_set (G_OBJECT(encoder), "bps", bitrate_ctx.cur_bitrate / enc_bitrate_div, NULL);
  } else {
    fprintf(stderr, "Failed to get an encoder element from the pipeline, "
                    "no dynamic bitrate control\n");
    encoder = NULL;
  }


  // Optional bitrate overlay
  overlay = gst_bin_get_by_name(GST_BIN(gst_pipeline), "overlay");
  update_overlay(0,0,0,0,0,0,0,0,0);


  // Optional sound delay via an identity element
  fprintf(stderr, "A-V delay: %d ms\n", av_delay);
  GstElement *identity_elem = gst_bin_get_by_name(GST_BIN(gst_pipeline), av_delay >= 0 ? "a_delay" : "v_delay");
  if (GST_IS_ELEMENT(identity_elem)) {
    g_object_set(G_OBJECT(identity_elem), "signal-handoffs", TRUE, NULL);
    g_signal_connect(identity_elem, "handoff", G_CALLBACK(cb_delay), NULL);
  } else {
    fprintf(stderr, "Failed to get a delay element from the pipeline, not applying a delay\n");
  }


  // Optional video PTS interval fixup
  // To avoid OBS dropping frames due to PTS jitter
  identity_elem = gst_bin_get_by_name(GST_BIN(gst_pipeline), "ptsfixup");
  if (GST_IS_ELEMENT(identity_elem)) {
    g_object_set(G_OBJECT(identity_elem), "signal-handoffs", TRUE, NULL);
    g_signal_connect(identity_elem, "handoff", G_CALLBACK(cb_ptsfixup), NULL);
  } else {
    fprintf(stderr, "Failed to get a ptsfixup element from the pipeline, "
                    "not removing PTS jitter\n");
  }


  // Optional SRT streaming via an appsink (needed for dynamic video bitrate)
  GstAppSinkCallbacks callbacks = {NULL, NULL, new_buf_cb};
  GstElement *srt_app_sink = gst_bin_get_by_name(GST_BIN(gst_pipeline), "appsink");
  if (GST_IS_ELEMENT(srt_app_sink)) {
    gst_app_sink_set_callbacks (GST_APP_SINK(srt_app_sink), &callbacks, NULL, NULL);
    srt_host = argv[optind+1];
    srt_port = argv[optind+2];

    srt_startup();
  }

  if (GST_IS_ELEMENT(srt_app_sink)) {
    int ret_srt;
    do {
      ret_srt = connect_srt(srt_host, srt_port, stream_id);
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
        struct timespec retry_delay = { .tv_sec = 0, .tv_nsec = 500 * 1000 * 1000 };
        nanosleep(&retry_delay, NULL);
      }
    } while(ret_srt != 0);
  }

  // We can only monitor the connection when we use an appsink
  if (GST_IS_ELEMENT(srt_app_sink)) {
    g_timeout_add(BITRATE_UPDATE_INT, connection_housekeeping, NULL);
  }

  /*
    We used to attempt to restart the pipeline in case of errors
    However the version of flvdemux distributed with Ubuntu 18.04
    for the Jetson Nano fails to restart.
    Rather than deal with glitchy pipeline elements, just give up
    and exit. Ensure you run belacoder in a wrapper script which
    can restart it if needed, e.g. belaUI
  */
  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add(SIGTERM, stop_from_signal, NULL);
  g_unix_signal_add(SIGINT, stop_from_signal, NULL);
  signal(SIGALRM, cb_sigalarm);
  g_timeout_add(1000, stall_check, NULL); // check every second

  // Everything good so far, start the gstreamer pipeline
  gst_element_set_state((GstElement*)gst_pipeline, GST_STATE_PLAYING);
  g_main_loop_run(loop);

  /*
    Close the SRT socket, if connected
    This must be done before trying to stop the pipeline, as the latter
    may block, causing cb_sigalarm to terminate the process
  */
  if (sock >= 0) {
    srt_close(sock);
  }

  gst_element_set_state((GstElement*)gst_pipeline, GST_STATE_NULL);

  // Clean up SRT library resources
  srt_cleanup();

  // Clean up mmap'd pipeline file
  munmap(launch_string, launch_string_len);

  return 0;
}
