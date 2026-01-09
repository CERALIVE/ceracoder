# Code Quality and Risks

This document identifies code quality issues, potential bugs, and improvement opportunities in `belacoder.c`. Issues are categorized by severity and area.

## Summary

Most issues have been fixed. The remaining items are marked as "Deferred" for future work.

| Category | Fixed | Deferred | Total |
|----------|-------|----------|-------|
| Resource Management | 3 | 0 | 3 |
| Error Handling | 3 | 0 | 3 |
| Signal Safety | 2 | 0 | 2 |
| Global State | 0 | 1 | 1 |
| Portability | 4 | 0 | 4 |
| Maintainability | 4 | 1 | 5 |

## Fixed Issues

### Critical

#### 1. Signal Handler Calls Non-Async-Signal-Safe Functions ✓ FIXED

**Commit:** `fix: use async-signal-safe SIGHUP handler`

Changed to use a volatile flag (`reload_bitrate_flag`) that is set by the signal handler and checked in `stall_check()`. Also fixed missing `fclose()` in `read_bitrate_file()`.

### High Priority

#### 2. Missing `srt_cleanup()` Call ✓ FIXED

**Commit:** `fix: add srt_cleanup() call at end of main`

Added `srt_cleanup()` call before program exit.

#### 3. Pipeline File Descriptor and mmap Not Cleaned Up ✓ FIXED

**Commit:** `fix: clean up pipeline file descriptor and mmap region`

- Close `pipeline_fd` immediately after mmap
- Add `munmap()` call at program exit
- Renamed variable to `launch_string_len` for clarity

#### 4. Unchecked Return Values from `srt_setsockflag` ✓ FIXED

**Commit:** `fix: replace assert() with proper error handling for SRT socket options`

Replaced all `assert(ret == 0)` with proper error handling that prints descriptive messages using `srt_getlasterror_str()`.

#### 5. `stop()` Called from Signal Context May Race ✓ FIXED

**Commit:** `fix: use g_unix_signal_add for SIGTERM/SIGINT handling`

Replaced raw `signal()` calls with `g_unix_signal_add()` for async-signal-safe handling.

### Medium Priority

#### 7. `min`/`max` Macros Evaluate Arguments Multiple Times ✓ FIXED

**Commit:** `fix: use GLib MIN/MAX for type-safe min/max operations`

Macros now wrap GLib's `MIN()`/`MAX()` which don't double-evaluate arguments.

#### 8. Magic Numbers Throughout ✓ FIXED

**Commit:** `refactor: extract magic numbers to named constants`

Added named constants for:
- `EMA_*` - Exponential moving average smoothing factors
- `RTT_*` - RTT tracking parameters
- `BS_TH*` - Buffer size threshold multipliers

#### 9. `strtol` Without Full Error Checking ✓ FIXED

**Commit:** `fix: add proper error handling for strtol parsing`

Added `parse_long()` helper function with full error checking (errno, endptr, range validation).

#### 10. Potential Integer Overflow in Bitrate Calculations ✓ FIXED

**Commit:** `fix: use int64_t for bitrate calculations to prevent overflow`

Changed local bitrate variable to `int64_t` for intermediate calculations.

#### 11. `getms()` Uses `CLOCK_MONOTONIC_RAW` ✓ FIXED

**Commit:** `fix: use CLOCK_MONOTONIC instead of CLOCK_MONOTONIC_RAW`

Changed to POSIX-compliant `CLOCK_MONOTONIC` and added proper error handling.

### Low Priority

#### 12. Missing `#include` for `inet_addr` ✓ FIXED

**Commit:** `fix: add missing includes for portability`

Added `#include <arpa/inet.h>`, `#include <errno.h>`, and `#include <time.h>`.

#### 13. Cast Hides Signedness Warning ✓ FIXED

Fixed as part of Issue #1 - new `sighup_handler` has correct signature.

#### 15. No Version Check for SRT API ✓ FIXED

**Commit:** `fix: add compile-time check for SRT version 1.4.0+`

Added compile-time check using `SRT_VERSION_VALUE`.

#### 16. `usleep()` is Obsolete ✓ FIXED

**Commit:** `fix: replace deprecated usleep() with nanosleep()`

Changed to POSIX-compliant `nanosleep()`.

#### 17. Inconsistent Logging ✓ FIXED

**Commit:** `refactor: standardize logging to use fprintf(stderr, ...)`

All logging now uses `fprintf(stderr, ...)` except version output which uses stdout.

## Deferred Issues

These issues require more significant refactoring and are deferred for future work:

### 6. Global Variables for State (Medium)

**Status:** Deferred to Phase 4

**Problem:** Approximately 15 global variables for state. Makes the code hard to test, reason about, and extend.

**Proposed Fix:** Consolidate into a context struct:
```c
typedef struct {
    GstPipeline *pipeline;
    GMainLoop *loop;
    GstElement *encoder;
    SRTSOCKET sock;
    int quit;
    // ... etc
} BelacoderContext;
```

This is planned as part of the file-splitting refactor.

### 14. Hardcoded Audio Device (Low)

**Status:** Deferred - Pipeline configuration issue

**Problem:** Device `hw:2` is hardcoded in many pipeline files.

**Proposed Fix:** Document in README; consider supporting device as environment variable or CLI argument.

## Future Improvements (Phase 4)

These are larger structural improvements planned for future work:

1. **Split into modules:** `srt_sender.c`, `bitrate_control.c`, `gst_helpers.c`
2. **Implement controller interface** for multi-algorithm support
3. **Add unit tests** for bitrate controller logic
4. **Consolidate globals** into context struct

## Testing Recommendations

Currently there are no automated tests. Recommended additions:

1. **Unit tests for bitrate controller** – Feed synthetic stats, verify decisions
2. **Integration test** – Connect to local SRT listener, verify stream
3. **Fuzz testing** – Invalid pipeline files, malformed bitrate files
4. **Static analysis** – Run `cppcheck`, `clang-tidy`, or Coverity

## See Also

- [Architecture](architecture.md) – System overview
- [Bitrate Control](bitrate-control.md) – Algorithm specification
- [Balancing Algorithms](balancing-algorithms.md) – Multi-algorithm design
