# Balancing Algorithms

This document catalogs potential bitrate control ("balancing") algorithms for belacoder and proposes a design for supporting multiple algorithms at runtime.

## Current Algorithm: RTT + Buffer Heuristic

The current implementation (see [bitrate-control.md](bitrate-control.md)) uses a hand-tuned heuristic that:

- Monitors RTT and send buffer occupancy
- Decreases bitrate when either metric exceeds thresholds
- Increases bitrate slowly when both metrics are stable

### Pros

- Simple and battle-tested in BELABOX deployments
- Low computational overhead
- Reasonably responsive to congestion

### Cons

- No active bandwidth probing
- Fixed smoothing factors may not suit all network types
- Thresholds coupled to SRT latency configuration
- Many magic numbers; hard to tune for different scenarios

## Candidate Alternative Algorithms

### 1. AIMD (Additive Increase, Multiplicative Decrease)

Classic TCP-style congestion control.

**Behavior:**
- **Increase:** Add fixed increment each interval when no congestion detected
- **Decrease:** Multiply by factor (e.g., 0.5) on congestion signal

**Pros:**
- Well-understood behavior
- Converges to fair share when multiple streams compete
- Simple to implement

**Cons:**
- Can be slow to recover after a drop
- May oscillate ("sawtooth") in stable conditions
- Not optimized for real-time video (prefers throughput over latency)

**Parameters:**
- `increment`: bps to add per interval (e.g., 50 Kbps)
- `decrease_factor`: multiplicative factor on congestion (e.g., 0.5)
- `congestion_threshold`: RTT or buffer level that triggers decrease

### 2. BBR-style (Bottleneck Bandwidth and RTT)

Inspired by Google's BBR congestion control, adapted for real-time video.

**Behavior:**
- Estimate bottleneck bandwidth from max observed throughput
- Estimate minimum RTT from recent samples
- Target sending rate = bandwidth × (1 - margin), keeping RTT near minimum

**Pros:**
- Actively probes for available bandwidth
- Maintains low latency by targeting RTT_min
- Adapts to changing network conditions

**Cons:**
- More complex state machine
- Probing phases can cause brief latency spikes
- Requires careful tuning of probing interval and pacing

**Parameters:**
- `rtt_probe_interval`: how often to probe for RTT_min
- `bandwidth_probe_gain`: multiplier during probing phase
- `drain_gain`: factor to drain queue after probing
- `steady_gain`: factor for steady-state operation

### 3. Latency-Target Controller

Keep measured RTT below a configurable target, regardless of throughput.

**Behavior:**
- If RTT > target, decrease bitrate proportionally to overshoot
- If RTT < target and stable, increase bitrate
- Emergency decrease if RTT >> target

**Pros:**
- Directly optimizes for latency (critical for live streaming)
- Easy to understand: "I want latency under X ms"
- Works well with srtla and bonded connections

**Cons:**
- May underutilize bandwidth if target is conservative
- Requires good RTT_min estimate to set achievable target
- Doesn't account for buffer buildup separately

**Parameters:**
- `rtt_target`: desired maximum RTT in ms
- `rtt_tolerance`: acceptable RTT range around target
- `increase_rate`: bps/interval when under target
- `decrease_rate`: bps/interval when over target

### 4. PID Controller

Classic control theory approach: Proportional-Integral-Derivative controller.

**Behavior:**
- Error = target_rtt - measured_rtt (or target_buffer - measured_buffer)
- Bitrate adjustment = Kp×error + Ki×integral(error) + Kd×derivative(error)

**Pros:**
- Well-understood control theory
- Can be tuned for specific response characteristics
- Handles steady-state error (via integral term)

**Cons:**
- Requires careful tuning of Kp, Ki, Kd
- Integral windup can cause problems after large disturbances
- May oscillate if poorly tuned

**Parameters:**
- `Kp`: proportional gain
- `Ki`: integral gain
- `Kd`: derivative gain
- `setpoint`: target RTT or buffer level

### 5. Fixed Bitrate / Rate Ladder

No adaptive control; use fixed bitrate or switch between preset levels.

**Behavior:**
- **Fixed:** Encoder stays at configured bitrate
- **Ladder:** Define 3-5 bitrate levels; switch based on simple thresholds

**Pros:**
- Predictable behavior
- No oscillation
- Good for stable networks (fiber, ethernet)

**Cons:**
- Cannot adapt to varying conditions
- May cause buffering on congestion (fixed mode)
- Ladder mode still needs thresholds

**Parameters:**
- `fixed_bitrate`: single target (fixed mode)
- `levels[]`: array of bitrate levels (ladder mode)
- `up_threshold`, `down_threshold`: RTT or buffer thresholds for level changes

### 6. Hybrid: Conservative + Aggressive Modes

Two-state machine: conservative mode for stability, aggressive mode for probing.

**Behavior:**
- **Conservative:** Slow increase, fast decrease (like current)
- **Aggressive:** Faster increase, probe for max bandwidth
- Switch based on recent stability

**Pros:**
- Best of both worlds
- Stable in bad conditions, responsive in good conditions
- Can include active probing in aggressive mode

**Cons:**
- More complex state management
- Mode switching logic needs tuning
- Risk of oscillating between modes

## Proposed Runtime Selection Design

### CLI Interface

```bash
belacoder pipeline.txt host 4000 --balancer=rtt_buffer  # Current (default)
belacoder pipeline.txt host 4000 --balancer=aimd
belacoder pipeline.txt host 4000 --balancer=latency_target --rtt-target=150
belacoder pipeline.txt host 4000 --balancer=fixed --fixed-bitrate=4000000
```

### Configuration File Support

For complex algorithms, support a config file:

```bash
belacoder pipeline.txt host 4000 --balancer-config=balancer.conf
```

Example `balancer.conf`:
```ini
algorithm = bbr_style
rtt_probe_interval = 5000
bandwidth_probe_gain = 1.25
drain_gain = 0.75
steady_gain = 1.0
min_bitrate = 500000
max_bitrate = 8000000
```

### Controller Interface (C Pseudocode)

```c
typedef struct {
    int min_bitrate;
    int max_bitrate;
    int current_bitrate;
    int srt_latency;
    // Algorithm-specific config via union or void* config
} BalancerConfig;

typedef struct {
    int rtt;
    int buffer_size;
    double throughput;
    uint64_t timestamp;
} BalancerInput;

typedef struct {
    int new_bitrate;
    int confidence;  // 0-100, for logging/debugging
} BalancerOutput;

// Function pointer type for controller step function
typedef BalancerOutput (*balancer_step_fn)(
    BalancerConfig *config,
    BalancerInput *input,
    void *state
);

// Function pointer type for state initialization
typedef void* (*balancer_init_fn)(BalancerConfig *config);

// Function pointer type for state cleanup
typedef void (*balancer_cleanup_fn)(void *state);

typedef struct {
    const char *name;
    balancer_init_fn init;
    balancer_step_fn step;
    balancer_cleanup_fn cleanup;
} BalancerAlgorithm;

// Registry of available algorithms
extern BalancerAlgorithm balancer_registry[];
```

### Implementation Steps (Future Work)

1. **Refactor current algorithm** into the interface above
2. **Add algorithm selection** to CLI parser
3. **Implement AIMD** as second algorithm (simplest alternative)
4. **Add fixed/ladder** mode for stable networks
5. **Consider BBR-style** for advanced users

### Metrics Contract

All algorithms receive the same inputs from SRT:

| Metric | Source | Frequency |
|--------|--------|-----------|
| RTT (ms) | `srt_bstats()` | Every 20 ms |
| Send buffer (packets) | `SRTO_SNDDATA` | Every 20 ms |
| Throughput (bps) | `mbpsSendRate` | Every 20 ms |
| ACK count | `pktRecvACKTotal` | Every 20 ms |
| Negotiated latency | `SRTO_PEERLATENCY` | Once at connect |

Algorithms may maintain their own internal state (smoothed values, timers, etc.).

## Algorithm Comparison Matrix

| Algorithm | Complexity | Latency Focus | Bandwidth Efficiency | Stability | Best For |
|-----------|------------|---------------|---------------------|-----------|----------|
| RTT+Buffer (current) | Low | Medium | Medium | High | General use |
| AIMD | Low | Low | Medium | Medium | Fair sharing |
| BBR-style | High | High | High | Medium | Variable networks |
| Latency-Target | Medium | High | Low-Medium | High | Ultra-low latency |
| PID | Medium | Configurable | Configurable | Varies | Control nerds |
| Fixed/Ladder | Minimal | N/A | Low | Very High | Stable networks |
| Hybrid | High | Configurable | High | Medium | Advanced users |

## Recommendation

For initial multi-algorithm support:

1. **Keep current as default** (`rtt_buffer`) - proven in production
2. **Add `fixed` mode** - trivial to implement, useful for testing
3. **Add `aimd` mode** - simple alternative with different characteristics
4. **Add `latency_target` mode** - directly optimizes for low latency

More advanced algorithms (BBR-style, PID, hybrid) can be added later based on user feedback.

## See Also

- [Bitrate Control](bitrate-control.md) – Current algorithm specification
- [Architecture](architecture.md) – System overview
- [Code Quality and Risks](code-quality-and-risks.md) – Refactoring opportunities
