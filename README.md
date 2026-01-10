Ceracoder - live video encoder with dynamic bitrate control and [SRT](https://github.com/CERALIVE/srt) support
=========

**Ceracoder is a fork of [irlserver/belacoder](https://github.com/irlserver/belacoder), which itself is a fork of the original [BELABOX/belacoder](https://github.com/BELABOX/belacoder) project.**

This is a [GStreamer](https://gstreamer.freedesktop.org/)-based encoder with support for [SRT](https://github.com/CERALIVE/srt) and dynamic bitrate control depending on the network capacity. This means that if needed, the video bitrate is automatically reduced on-the-fly to match the speed of the network connection. The intended application is live video streaming over bonded 4G modems by using it on a single board computer together with a HDMI capture card and [srtla](https://github.com/CERALIVE/srtla).

Ceracoder is developed on an NVIDIA Jetson Nano ([Amazon.com](https://amzn.to/3mt2Coz) / [Amazon.co.uk](https://amzn.to/31IOgJ2) / [NVIDIA](https://developer.nvidia.com/embedded/jetson-nano-developer-kit)), and we provide GStreamer pipelines for using its hardware video encoding. However it can also be used on other platforms as long as the correct GStreamer pipeline is provided.


Architecture at a glance
------------------------

Ceracoder reads a GStreamer pipeline from a file, constructs it, and streams the output over SRT:

```
┌──────────────┐      ┌─────────────┐      ┌───────────┐
│ GStreamer    │ ──▶  │ appsink     │ ──▶  │ SRT send  │ ──▶ Network
│ Pipeline     │      │ (callback)  │      │ (libsrt)  │
└──────────────┘      └─────────────┘      └───────────┘
                             ▲
        ┌────────────────────┘
        │ Periodic stats polling
        ▼
┌───────────────────┐
│ Bitrate Controller│ ──▶ Adjusts encoder bitrate
└───────────────────┘
```

The bitrate controller polls SRT statistics (RTT, send buffer) every 20 ms and adjusts the encoder's bitrate to avoid congestion. See [docs/bitrate-control.md](docs/bitrate-control.md) for the algorithm details.


Network Bonding with srtla
--------------------------

Ceracoder is designed to work with [srtla](https://github.com/CERALIVE/srtla) (SRT Link Aggregation) for bonding multiple network connections. This is the primary use case for live streaming over cellular networks.

### How It Works

```
┌──────────────┐
│ ceracoder    │
│ (encoder +   │──SRT──▶┌─────────┐     ┌─────────┐
│  SRT sender) │        │ srtla   │     │ Modem 1 │──┐
└──────────────┘        │ (local) │────▶│ (4G/5G) │  │
                        │         │     └─────────┘  │
                        │         │     ┌─────────┐  │    ┌─────────────┐
                        │         │────▶│ Modem 2 │──┼───▶│ srtla_rec   │──SRT──▶ Server
                        │         │     │ (4G/5G) │  │    │ (receiver)  │
                        │         │     └─────────┘  │    └─────────────┘
                        │         │     ┌─────────┐  │
                        │         │────▶│ Modem 3 │──┘
                        └─────────┘     │ (WiFi)  │
                                        └─────────┘
```

1. **ceracoder** encodes video and sends SRT to localhost (where srtla runs)
2. **srtla** splits the SRT stream across multiple network interfaces (modems, WiFi, etc.)
3. **srtla_rec** on the receiving end reassembles the stream and forwards to the SRT server

### Typical Deployment

```bash
# Terminal 1: Start srtla (bonding agent)
srtla_send 127.0.0.1 5000 receiver.example.com 5000

# Terminal 2: Start ceracoder pointing to local srtla
./ceracoder pipeline/h264_camlink_1080p 127.0.0.1 5000 -s mystreamid -l 2000 -b bitrate.conf
```

### Why This Matters for Bitrate Control

When using multiple networks:
- **Aggregate bandwidth** can exceed any single connection
- **Packet loss** on one link doesn't drop the stream (redundancy)
- **Variable capacity** as modems enter/exit coverage areas

Ceracoder's adaptive bitrate algorithm adjusts to the **combined capacity** of all bonded links as reported by SRT. When a modem drops out, SRT's buffer grows and RTT increases, triggering bitrate reduction. When capacity increases, ceracoder gradually ramps up.

### Configuration Tips

| Scenario | Recommended Settings |
|----------|---------------------|
| 2x 4G modems | `-l 2000` (2s latency), max 8-12 Mbps |
| 3+ modems (aggressive) | `-l 1500`, max 15-20 Mbps |
| Single modem (no srtla) | `-l 3000`, conservative max bitrate |
| Stable network (fiber) | Higher max bitrate, can use fixed mode |

For srtla setup instructions, see [CERALIVE/srtla](https://github.com/CERALIVE/srtla).


Dependencies
------------

### Minimum Versions

| Dependency | Minimum | Notes |
|------------|---------|-------|
| GStreamer  | 1.14+   | Core + app library |
| GLib       | 2.40+   | (bundled with GStreamer) |
| libsrt     | 1.4.0+  | **Enforced at compile time**; recommend [CERALIVE/srt](https://github.com/CERALIVE/srt) fork |
| GCC/Clang  | 4.9/3.5 | C99 support |

> **Note:** belacoder includes a compile-time check for SRT 1.4.0+. If you see an error like `#error "SRT 1.4.0 or later required"`, upgrade your libsrt installation.

### Quick Install (Ubuntu 20.04+)

```bash
# Build tools + GStreamer
sudo apt-get install build-essential git pkg-config cmake \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav

# SRT (CERALIVE fork with BELABOX patches)
git clone https://github.com/CERALIVE/srt.git
cd srt
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
sudo ldconfig
```

For a complete dependency list including pipeline-specific plugins, see [docs/dependencies.md](docs/dependencies.md).


Building
--------

```bash
git clone https://github.com/CERALIVE/ceracoder.git
cd ceracoder
make
```

The Makefile uses `pkg-config` to locate GStreamer and libsrt. Ensure both are installed and discoverable:

```bash
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0 srt
```

### Testing

Ceracoder includes comprehensive integration tests that verify module behavior without requiring actual hardware:

```bash
# Install test dependencies
sudo apt-get install libcmocka-dev

# Basic tests (balancer algorithms, module integration)
make test

# Full test suite (includes SRT network integration tests)
make test_all
```

Tests verify:
- Balancer algorithm behavior (adaptive, fixed, AIMD) - 16 tests
- Config loading and reload - 8 tests
- SRT network operations (in-process and external listener) - 13 tests
- Bitrate bounds enforcement
- Network condition responses

The test suite includes both unit-style tests with fakes and real SRT network integration tests.

### Code Quality

The project uses clang-tidy for static analysis:

```bash
# Run static analysis locally
make lint
```

Static analysis runs automatically in CI on every push/PR. The configuration is tuned for C code with GStreamer patterns.


Usage
-----

```
Syntax: ceracoder PIPELINE_FILE ADDR PORT [options]

Options:
  -v                  Print the version and exit
  -c <config file>    Configuration file (INI format, recommended)
  -d <delay>          Audio-video delay in milliseconds
  -s <streamid>       SRT stream ID
  -l <latency>        SRT latency in milliseconds (default: 2000)
  -r                  Reduced SRT packet size (6 TS packets instead of 7)
  -b <bitrate file>   Bitrate settings file (legacy, use -c instead)
  -a <algorithm>      Bitrate balancer algorithm (overrides config)

Config file example (ceracoder.conf):
[general]
min_bitrate = 500    # Kbps
max_bitrate = 6000   # Kbps (6 Mbps)
balancer = adaptive  # Algorithm: adaptive, fixed, aimd

[srt]
latency = 2000       # ms

[adaptive]
incr_step = 30       # Bitrate increase step (Kbps)
decr_step = 100      # Bitrate decrease step (Kbps)
incr_interval = 500  # Min interval between increases (ms)
decr_interval = 200  # Min interval between decreases (ms)

---
Send SIGHUP to reload configuration while running:
    kill -HUP $(pidof ceracoder)
```

Where:

* `PIPELINE_FILE` is a text file containing the GStreamer pipeline to use. See the `pipeline` directory for ready-made pipelines.
* `ADDR` is the hostname or IP address of the SRT listener to stream to.
* `PORT` is the port of the SRT listener to stream to.
* `-c <config file>` is the recommended way to configure bitrate bounds and algorithm settings. See `ceracoder.conf.example` for a full example.
* `-d <delay>` is the optional delay in milliseconds to add to the audio stream relative to the video.
* `-b <bitrate file>` is the legacy way to set bitrate bounds (use `-c` instead for new deployments).

### Balancer Algorithms

Ceracoder supports multiple bitrate control algorithms:

| Algorithm | Description | Best For |
|-----------|-------------|----------|
| **adaptive** (default) | RTT and buffer-based control with graduated response | Most use cases, variable networks |
| **fixed** | Constant bitrate, no adaptation | Stable networks, testing |
| **aimd** | TCP-style Additive Increase Multiplicative Decrease | Fair bandwidth sharing |

Select via config file or override with `-a <algorithm>`.


GStreamer Pipelines
-------------------

The GStreamer pipelines are available in the `pipeline` directory, organised in machine-specific directories (for pipelines using hardware-accelerated features) or `generic` (for software-only pipelines). The filename format is `CODEC_CAPTUREDEV_[RES[FPS]]`:

* `CODEC` is `h265` or `h264` (for system-specific hw encoders), or `x264_superfast` / `x264_veryfast` for x264 software encoding
* `CAPTUREDEV` is either `camlink` for Elgato Cam Link 4K ([Amazon.com](https://amzn.to/2Hx3tFM) / [Amazon.co.uk](https://amzn.to/3jp32us)) or other uncompressed YUY2 capture cards or `v4l_mjpeg` for low cost USB2.0 MJPEG capture cards ([Amazon.com](https://amzn.to/31VOTyS) / [Amazon.co.uk](https://amzn.to/3mwlNxU))
* `RES` can be blank - capturing at the highest available resolution, `720p`, `1080p`, `1440p`, or `4k_2160p`
* `FPS` can be blank - capturing at the highest available refresh rate, `29.97`, or `30` FPS

Note that to encode 4k / 2160p video captured by a camlink you must specifically use `h265_camlink_4k_2160p` rather than `h265_camlink`, as the `preset-level` quality setting of the encoder must be set to a lower value to allow the encoder to maintain 30 FPS in all conditions.

### Pipeline Requirements

For ceracoder features to work, pipelines must include specific named elements:

| Element | Required | Purpose |
|---------|----------|---------|
| `appsink name=appsink` | Yes (for SRT output) | Hands buffers to belacoder for SRT transmission |
| `name=venc_bps` or `name=venc_kbps` | For dynamic bitrate | Video encoder with runtime-settable `bitrate` property |
| `name=overlay` | Optional | Text overlay for on-screen bitrate/stats display |
| `name=a_delay` / `name=v_delay` | Optional | Identity elements for A/V sync adjustment |
| `name=ptsfixup` | Optional | PTS jitter smoothing (helps with OBS compatibility) |

### Tips

* The Jetson Nano hardware encoders seem biased towards allocating most of the bitrate budget to I-frames, while heavily compressing P-frames, especially on lower bitrates. This can heavily affect image quality when most of the image is moving and this is why we limit the quantization range in our pipelines using `qp-range`. This range makes a big improvement over the defaults, however in some cases results can probably be further improved with different parameters.
* `identity name=a_delay signal-handoffs=TRUE` and `identity name=v_delay signal-handoffs=TRUE` elements can be used to adjust the PTS (presentation timestamp) of the audio and video streams respectively by the delay specified with `-d`. Use them to synchronise the audio and video if needed (e.g. audio delay of around 900 for a GoPro Hero7 with stabilisation enabled).


Troubleshooting
---------------

### SRT Connection Failures

| Error | Cause | Fix |
|-------|-------|-----|
| "connection timed out" | Server unreachable or port blocked | Check firewall, verify host/port |
| "streamid already in use" | Duplicate stream ID on server | Use unique `-s <streamid>` |
| "invalid streamid" | Server rejected stream ID | Check server's access control config |
| "failed to resolve address" | DNS failure | Use IP address or fix DNS |

### Pipeline Errors

* **"Failed to get an encoder element"**: Pipeline doesn't have `name=venc_bps` or `name=venc_kbps`. Dynamic bitrate control disabled.
* **"Pipeline stall detected"**: Capture device stopped providing frames. Check V4L2 device, resolution, or cable.
* **GStreamer element not found**: Missing plugin package. Run `gst-inspect-1.0 <element>` to check, install the required package (see [docs/dependencies.md](docs/dependencies.md)).

### Latency

The negotiated SRT latency is printed on connect:

```
SRT connected to example.com:4000. Negotiated latency: 2000 ms
```

If the receiver requests higher latency, belacoder will use the higher value. Adjust with `-l <ms>` if needed.


Docker
------

A Dockerfile is provided that builds ceracoder with the CERALIVE/srt fork:

```bash
# Build the image
docker build -t ceracoder .

# Extract the binary
docker create --name cc ceracoder
docker cp cc:/usr/bin/ceracoder ./ceracoder
docker rm cc
```

The container build installs SRT from [CERALIVE/srt](https://github.com/CERALIVE/srt) to `/usr`, so `pkg-config srt` works correctly inside the build.


Documentation
-------------

* [Architecture](docs/architecture.md) – System overview and dataflow
* [Dependencies](docs/dependencies.md) – Full dependency list with versions
* [Bitrate Control](docs/bitrate-control.md) – Adaptive bitrate algorithm details


License
-------
Ceracoder is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.