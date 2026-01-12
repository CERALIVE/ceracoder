# @ceralive/ceracoder (TypeScript bindings)

Type-safe helpers for ceracoder integration:

- Zod v4 schemas for config and CLI options
- Defaults aligned with the ceracoder C implementation
- Config generator (`buildCeracoderConfig`, `serializeCeracoderConfig`)
- CLI args builder (`buildCeracoderArgs`) that always prefers `-c <config>` (legacy `-b` removed)
- Pipeline builder (`PipelineBuilder`) to generate hardware-specific GStreamer launch strings
- Process helpers (`spawnCeracoder`, `sendHup`, `sendTerm`, `writeConfig`, `writePipeline`)

## Pipeline Builder

```ts
import { PipelineBuilder } from "@ceralive/ceracoder";

const result = PipelineBuilder.build({
  hardware: "rk3588",
  source: "hdmi",
  overrides: { resolution: "1080p", framerate: 30 },
});

console.log(result.pipeline); // GStreamer launch string
```

Helpers:
- `PipelineBuilder.listHardwareTypes()` → `["jetson","rk3588","n100","generic"]`
- `PipelineBuilder.listSources(hardware)` → per-hardware sources
- `PipelineBuilder.build({ hardware, source, overrides, writeTo? })` → pipeline string and optional file path

Notes:
- Pipelines are validated to contain `appsink` and encoder elements (`venc_bps`/`venc_kbps`)
- Resolution/framerate defaults come from per-source metadata
- `writeTo` writes the pipeline string to disk (for ceracoder `-p <file>`)

## Usage

```ts
import {
  buildCeracoderArgs,
  buildCeracoderConfig,
  serializeCeracoderConfig,
} from "@ceralive/ceracoder";

const { config, ini } = buildCeracoderConfig({
  general: { max_bitrate: 6000 },
  srt: { latency: 2000 },
});

// Write ini to ceracoder.conf, then run ceracoder
const args = buildCeracoderArgs({
  pipelineFile: "/usr/share/ceracoder/pipelines/generic/h264_camlink_1080p",
  host: "127.0.0.1",
  port: 9000,
  configFile: "/tmp/ceracoder.conf",
  latencyMs: config.srt.latency,
  algorithm: config.general.balancer,
});
```
