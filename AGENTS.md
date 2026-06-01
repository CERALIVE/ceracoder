# ceracoder

Parent: [../AGENTS.md](../AGENTS.md)

## ROLE IN THE GROUP

GStreamer-based hardware video encoder. Reads a pipeline from file, streams output over SRT with dynamic bitrate control. Fork of [irlserver/belacoder](https://github.com/irlserver/belacoder) (itself from BELABOX/belacoder).

**Depends on:** `libsrt` (compile-time, `pkg-config srt`), GStreamer 1.0  
**Consumed by:** CeraUI backend (TypeScript bindings at `bindings/typescript`), device image (packaged as `belacoder` .deb)

## STRUCTURE

```
ceracoder/
├── src/
│   ├── ceracoder.c          # entry point
│   ├── core/                # bitrate control, balancers, config
│   ├── gst/                 # encoder_control, overlay_ui
│   ├── io/                  # cli_options, pipeline_loader
│   └── net/                 # srt_client
├── camlink_workaround/      # submodule → BELABOX/camlink.git
├── bindings/typescript/     # TS bindings consumed by CeraUI backend
├── docs/                    # architecture, bitrate-control, dependencies, versioning
├── tests/                   # cmocka unit tests
└── Makefile                 # primary build entry
```

## BUILD

```bash
# Requires: libsrt >= 1.4.0, gstreamer-1.0, gstreamer-app-1.0, cmocka (tests)
make              # builds ceracoder binary; inits camlink_workaround submodule
make test         # runs cmocka tests
```

`VERSION` = `git rev-parse --short HEAD` (short SHA, not semver). Baked in at compile time via `-DVERSION`.

**pkg-config deps:** `gstreamer-1.0 gstreamer-app-1.0 srt` — all must be present before `make`.

## SUBMODULE

`camlink_workaround/` → `https://github.com/BELABOX/camlink.git`  
Run `git submodule init && git submodule update` if missing. `make submodule` does this automatically.

## TYPESCRIPT BINDINGS

`bindings/typescript/` — consumed by CeraUI backend via `link:../../../ceracoder/bindings/typescript` in its `package.json`. Built with `bun`. Don't edit `dist/` directly; source is in `bindings/typescript/src/`.

## WHERE TO LOOK

| Task | Location |
|------|----------|
| Bitrate algorithm | `src/core/bitrate_control.c` + [docs/bitrate-control.md](docs/bitrate-control.md) |
| Architecture overview | [docs/architecture.md](docs/architecture.md) |
| Runtime dependencies | [docs/dependencies.md](docs/dependencies.md) |
| Versioning scheme | [docs/versioning.md](docs/versioning.md) |
| SRT send logic | `src/net/srt_client.c` |
| Pipeline loading | `src/io/pipeline_loader.c` |
| TS bindings source | `bindings/typescript/src/` |

## ANTI-PATTERNS

- Don't hardcode a semver string for VERSION — it's always the git short-SHA.
- Don't modify `camlink_workaround/` directly; it's a submodule.
- Don't duplicate docs content here — link to `docs/` instead.
- Don't run `make` without `libsrt >= 1.4.0` installed; pkg-config will fail silently on some systems.
