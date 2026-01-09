# belacoder Dependencies

This document lists all build-time and runtime dependencies for belacoder, along with minimum compatible versions, package names for common distributions, and verification commands.

## Quick Reference

| Dependency | Minimum Version | Purpose |
|------------|-----------------|---------|
| GCC / Clang | GCC 4.9+ / Clang 3.5+ | C99 compiler |
| GNU Make | 3.81+ | Build system |
| pkg-config | 0.25+ | Dependency detection |
| GStreamer | 1.14+ | Media pipeline framework |
| GLib | 2.40+ | Event loop, utilities (bundled with GStreamer) |
| libsrt | 1.4.0+ | SRT protocol (recommend CERALIVE/srt fork) |

## Build-time Dependencies (Always Required)

These are needed to compile belacoder from source.

| Component | Ubuntu/Debian Package | Arch Package | Notes |
|-----------|----------------------|--------------|-------|
| C compiler | `build-essential` | `base-devel` | GCC or Clang with C99 support |
| Make | `build-essential` | `base-devel` | GNU Make |
| pkg-config | `pkg-config` | `pkgconf` | For detecting library paths |
| Git | `git` | `git` | For submodule (camlink_workaround) |
| GStreamer dev headers | `libgstreamer1.0-dev` | `gstreamer` | Core GStreamer headers |
| GStreamer app dev | `libgstreamer-plugins-base1.0-dev` | `gst-plugins-base` | For `gstappsink.h` |
| SRT dev headers | See [SRT Installation](#srt-installation) | — | libsrt headers + pkg-config file |

### Verification Commands

```bash
# Check compiler
gcc --version   # or clang --version

# Check pkg-config can find dependencies
pkg-config --modversion gstreamer-1.0      # expect ≥ 1.14
pkg-config --modversion gstreamer-app-1.0  # expect ≥ 1.14
pkg-config --modversion srt                # expect ≥ 1.4.0

# Check all required libs are linkable
pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 srt
```

## Runtime Dependencies (Core)

These are always needed to run belacoder, regardless of which pipeline you use.

| Component | Ubuntu/Debian Package | Arch Package | Notes |
|-----------|----------------------|--------------|-------|
| GStreamer core | `gstreamer1.0-plugins-base` | `gst-plugins-base` | Base plugins |
| GLib runtime | (included with GStreamer) | — | Shared library |
| libsrt runtime | See [SRT Installation](#srt-installation) | — | `libsrt.so` |

## Runtime Dependencies (Pipeline-Dependent)

Different pipeline templates require different GStreamer plugins. Use `gst-inspect-1.0 <element>` to check availability.

### Generic Pipelines (x264 Software Encoding)

| Element | Package (Ubuntu/Debian) | Package (Arch) | Pipeline |
|---------|------------------------|----------------|----------|
| `x264enc` | `gstreamer1.0-plugins-ugly` | `gst-plugins-ugly` | x264_* |
| `avenc_aac` | `gstreamer1.0-libav` | `gst-libav` | All with AAC |
| `voaacenc` | `gstreamer1.0-plugins-bad` | `gst-plugins-bad` | Alternative AAC |
| `jpegdec` | `gstreamer1.0-plugins-good` | `gst-plugins-good` | v4l_mjpeg_* |
| `jpegparse` | `gstreamer1.0-plugins-bad` | `gst-plugins-bad` | v4l_mjpeg_* |
| `mpegtsmux` | `gstreamer1.0-plugins-bad` | `gst-plugins-bad` | All |
| `v4l2src` | `gstreamer1.0-plugins-good` | `gst-plugins-good` | All V4L2 capture |
| `alsasrc` | `gstreamer1.0-plugins-base` | `gst-plugins-base` | All with audio |
| `textoverlay` | `gstreamer1.0-plugins-base` | `gst-plugins-base` | Optional overlay |
| `videoconvert` | `gstreamer1.0-plugins-base` | `gst-plugins-base` | Color conversion |
| `audioconvert` | `gstreamer1.0-plugins-base` | `gst-plugins-base` | Audio format conversion |
| `videorate` | `gstreamer1.0-plugins-base` | `gst-plugins-base` | Framerate adjustment |
| `queue` | (core) | (core) | Buffering |
| `identity` | (core) | (core) | PTS manipulation |

### Jetson Pipelines (NVIDIA Hardware Encoding)

| Element | Package / Source | Notes |
|---------|------------------|-------|
| `nvvidconv` | NVIDIA L4T / JetPack | Jetson-only |
| `nvv4l2h265enc` | NVIDIA L4T / JetPack | Jetson H.265 encoder |
| `nvv4l2h264enc` | NVIDIA L4T / JetPack | Jetson H.264 encoder |

These are provided by NVIDIA's L4T (Linux for Tegra) distribution and are not available in standard GStreamer packages.

### N100 / Intel Pipelines (VA-API / QSV)

| Element | Package (Ubuntu/Debian) | Notes |
|---------|------------------------|-------|
| `vaapih265enc` | `gstreamer1.0-vaapi` | Intel Quick Sync / VA-API |
| `vaapih264enc` | `gstreamer1.0-vaapi` | Intel Quick Sync / VA-API |
| `decklinkvideosrc` | `gstreamer1.0-plugins-bad` | Blackmagic Decklink capture |

### RK3588 Pipelines (Rockchip MPP)

| Element | Source | Notes |
|---------|--------|-------|
| `mpph265enc` | rockchip-mpp / custom GStreamer plugin | Rockchip hardware H.265 |
| `mpph264enc` | rockchip-mpp / custom GStreamer plugin | Rockchip hardware H.264 |

These require Rockchip's MPP (Media Process Platform) libraries and a compatible GStreamer plugin (often from vendor BSP or community projects like `gstreamer-rockchip`).

### libuvc-based Pipelines (USB UVC H.264)

| Element | Package | Notes |
|---------|---------|-------|
| `uvch264src` | `gstreamer1.0-plugins-bad` | UVC cameras with onboard H.264 encoding |

## SRT Installation

belacoder requires libsrt with specific BELABOX patches for optimal performance. We recommend building from the **CERALIVE/srt** fork:

```bash
# Clone the CERALIVE/srt fork
git clone https://github.com/CERALIVE/srt.git
cd srt

# Build and install to /usr/local (or /usr for system-wide)
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc)
sudo make install
sudo ldconfig

# Verify installation
pkg-config --modversion srt   # should print version ≥ 1.4.0
```

### Why CERALIVE/srt?

The [CERALIVE/srt](https://github.com/CERALIVE/srt) fork is described as an "up-to-date fork of the SRT shared library with BELABOX changes." It includes patches that improve behavior for the CERALIVE/belacoder use case, particularly around:

- Retransmission algorithm tuning
- Statistics reporting
- Compatibility with srtla (SRT link aggregation)

### Alternative: System libsrt-dev

On Ubuntu 20.04+, you can use the system package, but it may lack BELABOX-specific patches:

```bash
sudo apt-get install libsrt-dev
```

### SRT Version Compatibility

| SRT Version | Status | Notes |
|-------------|--------|-------|
| 1.4.0 – 1.4.4 | Supported | Minimum for `SRTO_RETRANSMITALGO` |
| 1.5.x | Supported | Recommended; includes connection bonding |
| < 1.4.0 | **Not supported** | Compile will fail with `#error` |

> **Compile-time enforcement:** belacoder includes a preprocessor check that fails compilation if `SRT_VERSION_VALUE < 1.4.0`. This ensures you get a clear error message instead of confusing link-time or runtime failures.

## Minimum Version Rationale

| Dependency | Min Version | Reason |
|------------|-------------|--------|
| GStreamer 1.14 | API stability | `gst_app_sink_set_callbacks`, `gst_buffer_map` API |
| GLib 2.40 | — | Required by GStreamer 1.14+ |
| libsrt 1.4.0 | `SRTO_RETRANSMITALGO` | belacoder sets `SRTO_RETRANSMITALGO = 1` |
| GCC 4.9 / Clang 3.5 | C99 | Code uses C99 features (mixed declarations, `//` comments) |

## Complete Install (Ubuntu 20.04+)

```bash
# Build tools
sudo apt-get update
sudo apt-get install -y build-essential git pkg-config cmake

# GStreamer (core + common plugins)
sudo apt-get install -y \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav

# SRT (from CERALIVE fork)
git clone https://github.com/CERALIVE/srt.git /tmp/srt
cd /tmp/srt && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
sudo ldconfig

# Build belacoder
cd /path/to/belacoder
make
```

## Complete Install (Arch Linux)

```bash
# Build tools + GStreamer
sudo pacman -S base-devel git pkgconf cmake openssl \
  gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-plugins-ugly gst-libav

# SRT (from CERALIVE fork)
git clone https://github.com/CERALIVE/srt.git /tmp/srt
cd /tmp/srt && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install

# Build belacoder
cd /path/to/belacoder
make
```

## Verifying Your Setup

```bash
# All dependencies present?
pkg-config --exists gstreamer-1.0 gstreamer-app-1.0 srt && echo "OK" || echo "MISSING"

# Check specific elements (example for x264 pipeline)
gst-inspect-1.0 x264enc >/dev/null && echo "x264enc OK" || echo "x264enc MISSING"
gst-inspect-1.0 mpegtsmux >/dev/null && echo "mpegtsmux OK" || echo "mpegtsmux MISSING"
gst-inspect-1.0 appsink >/dev/null && echo "appsink OK" || echo "appsink MISSING"

# Check SRT version
pkg-config --modversion srt
```

## See Also

- [Architecture](architecture.md) – System overview
- [README](../README.md) – Quick start guide
