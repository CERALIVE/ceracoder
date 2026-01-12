import { describe, it, expect } from "bun:test";
import { PipelineBuilder } from "./index.js";

describe("PipelineBuilder", () => {
	describe("listHardwareTypes", () => {
		it("returns all hardware types", () => {
			const types = PipelineBuilder.listHardwareTypes();
			expect(types).toContain("jetson");
			expect(types).toContain("rk3588");
			expect(types).toContain("n100");
			expect(types).toContain("generic");
		});
	});

	describe("listSources", () => {
		it("returns sources for jetson", () => {
			const sources = PipelineBuilder.listSources("jetson");
			expect(sources.length).toBeGreaterThan(0);
			expect(sources.some((s) => s.source === "camlink")).toBe(true);
			expect(sources.some((s) => s.source === "libuvch264")).toBe(true);
			expect(sources.some((s) => s.source === "rtmp")).toBe(true);
		});

		it("returns sources for rk3588", () => {
			const sources = PipelineBuilder.listSources("rk3588");
			expect(sources.some((s) => s.source === "hdmi")).toBe(true);
			expect(sources.some((s) => s.source === "usb_mjpeg")).toBe(true);
		});

		it("returns sources for n100", () => {
			const sources = PipelineBuilder.listSources("n100");
			expect(sources.some((s) => s.source === "decklink")).toBe(true);
			expect(sources.some((s) => s.source === "libuvch264")).toBe(true);
		});

		it("returns sources for generic", () => {
			const sources = PipelineBuilder.listSources("generic");
			expect(sources.some((s) => s.source === "camlink")).toBe(true);
			expect(sources.some((s) => s.source === "v4l_mjpeg")).toBe(true);
		});
	});

	describe("supportsSource", () => {
		it("returns true for supported source", () => {
			expect(PipelineBuilder.supportsSource("jetson", "camlink")).toBe(true);
			expect(PipelineBuilder.supportsSource("rk3588", "hdmi")).toBe(true);
		});

		it("returns false for unsupported source", () => {
			expect(PipelineBuilder.supportsSource("jetson", "decklink")).toBe(false);
			expect(PipelineBuilder.supportsSource("generic", "hdmi")).toBe(false);
		});
	});

	describe("buildPipeline", () => {
		describe("Jetson", () => {
			it("builds camlink pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
				});
				expect(result.pipeline).toContain("v4l2src");
				expect(result.pipeline).toContain("nvv4l2h265enc");
				expect(result.pipeline).toContain("name=venc_bps");
				expect(result.pipeline).toContain("name=appsink");
				expect(result.hardware).toBe("jetson");
				expect(result.source).toBe("camlink");
			});

			it("builds libuvch264 pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "libuvch264",
				});
				expect(result.pipeline).toContain("libuvch264src");
				expect(result.pipeline).toContain("nvv4l2decoder");
				expect(result.pipeline).toContain("nvv4l2h265enc");
			});

			it("builds rtmp pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "rtmp",
				});
				expect(result.pipeline).toContain("rtmpsrc");
				expect(result.pipeline).toContain("flvdemux");
			});

			it("builds test pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "test",
				});
				expect(result.pipeline).toContain("videotestsrc");
				expect(result.pipeline).toContain("audiotestsrc");
			});
		});

		describe("RK3588", () => {
			it("builds hdmi pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "rk3588",
					source: "hdmi",
				});
				expect(result.pipeline).toContain("v4l2src device=/dev/hdmirx");
				expect(result.pipeline).toContain("mpph265enc");
				expect(result.pipeline).toContain("name=venc_bps");
			});

			it("builds usb_mjpeg pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "rk3588",
					source: "usb_mjpeg",
				});
				expect(result.pipeline).toContain("image/jpeg");
				expect(result.pipeline).toContain("jpegdec");
				expect(result.pipeline).toContain("mpph265enc");
			});
		});

		describe("N100", () => {
			it("builds libuvch264 pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "n100",
					source: "libuvch264",
				});
				expect(result.pipeline).toContain("libuvch264src");
				expect(result.pipeline).toContain("qsvh264dec");
				expect(result.pipeline).toContain("qsvh265enc");
				expect(result.pipeline).toContain("name=venc_kbps");
			});

			it("builds decklink pipeline", () => {
				const result = PipelineBuilder.build({
					hardware: "n100",
					source: "decklink",
				});
				expect(result.pipeline).toContain("decklinkvideosrc");
				expect(result.pipeline).toContain("vapostproc");
				expect(result.pipeline).toContain("qsvh265enc");
			});
		});

		describe("Generic", () => {
			it("builds camlink pipeline with x264", () => {
				const result = PipelineBuilder.build({
					hardware: "generic",
					source: "camlink",
				});
				expect(result.pipeline).toContain("v4l2src");
				expect(result.pipeline).toContain("x264enc");
				expect(result.pipeline).toContain("name=venc_kbps");
			});

			it("applies x264 preset override", () => {
				const result = PipelineBuilder.build({
					hardware: "generic",
					source: "camlink",
					overrides: { x264Preset: "veryfast" },
				});
				expect(result.pipeline).toContain("speed-preset=3");
			});
		});

		describe("Overrides", () => {
			it("applies resolution override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { resolution: "720p" },
				});
				expect(result.pipeline).toContain("width=1280,height=720");
			});

			it("applies framerate override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { framerate: 60 },
				});
				expect(result.pipeline).toContain("framerate=60/1");
			});

			it("disables overlay when requested", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { bitrateOverlay: false },
				});
				expect(result.pipeline).not.toContain("textoverlay");
			});

			it("applies audio codec override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { audioCodec: "opus" },
				});
				expect(result.pipeline).toContain("opusenc");
			});

			it("applies audio device override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { audioDevice: "hw:5" },
				});
				expect(result.pipeline).toContain("alsasrc device=hw:5");
			});

			it("applies volume override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					overrides: { volume: 0.5 },
				});
				expect(result.pipeline).toContain("volume volume=0.5");
			});

			it("applies rtmp url override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "rtmp",
					overrides: { rtmpUrl: "rtmp://test.example.com/live" },
				});
				expect(result.pipeline).toContain("rtmp://test.example.com/live");
			});

			it("applies srt port override", () => {
				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "srt",
					overrides: { srtPort: 5000 },
				});
				expect(result.pipeline).toContain("srt://:5000");
			});
		});

		describe("Validation", () => {
			it("throws for unsupported source", () => {
				expect(() =>
					PipelineBuilder.build({
						hardware: "generic",
						source: "hdmi",
					}),
				).toThrow("does not support source");
			});

			it("throws for unknown hardware", () => {
				expect(() =>
					PipelineBuilder.build({
						hardware: "unknown" as any,
						source: "camlink",
					}),
				).toThrow();
			});
		});

		describe("File output", () => {
			it("writes to file when specified", () => {
				const fs = require("node:fs");
				const tmpPath = `/tmp/test_pipeline_${Date.now()}.txt`;

				const result = PipelineBuilder.build({
					hardware: "jetson",
					source: "camlink",
					writeTo: tmpPath,
				});

				expect(result.path).toBe(tmpPath);
				expect(fs.existsSync(tmpPath)).toBe(true);
				expect(fs.readFileSync(tmpPath, "utf-8")).toBe(result.pipeline);

				// Cleanup
				fs.unlinkSync(tmpPath);
			});
		});
	});
});
