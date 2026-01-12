import type { HardwareBuilder, PipelineOverrides, SourceMeta, VideoSource, X264Preset } from "./types.js";
import {
	buildAudioPipeline,
	buildMuxAndSink,
	buildOverlay,
	buildTestAudioPipeline,
	buildVideoRateFilter,
	calculateGop,
	getResolutionDims,
} from "./common.js";

const SUPPORTED_SOURCES: SourceMeta[] = [
	{
		source: "camlink",
		description: "Elgato Cam Link 4K (software x264 encoding)",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: false,
		supportsFramerateOverride: false,
	},
	{
		source: "v4l_mjpeg",
		description: "USB MJPEG capture card (software x264 encoding)",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: true,
		supportsFramerateOverride: true,
	},
	{
		source: "test",
		description: "Test pattern (no capture device required)",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: true,
		supportsFramerateOverride: true,
	},
];

// x264 speed preset values
const X264_PRESET_MAP: Record<X264Preset, number> = {
	superfast: 2,
	veryfast: 3,
	fast: 4,
	medium: 5,
};

function buildX264Encoder(opts: PipelineOverrides): string {
	const preset = opts.x264Preset || "superfast";
	const presetValue = X264_PRESET_MAP[preset];
	const gop = calculateGop(opts.framerate || 30);
	return `x264enc speed-preset=${presetValue} key-int-max=${gop} name=venc_kbps ! h264parse config-interval=-1 ! queue max-size-time=10000000000 max-size-buffers=1000 max-size-bytes=41943040 ! mux. `;
}

function buildCamlinkPipeline(opts: PipelineOverrides): string {
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = "v4l2src ! identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += overlay;
	pipeline += "videoconvert ! ";
	pipeline += buildX264Encoder(opts);
	pipeline += buildAudioPipeline("generic", "camlink", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildV4lMjpegPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src ! image/jpeg,width=${dims.width},height=${dims.height} ! `;
	pipeline += "jpegdec ! identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "videoconvert ! ";
	pipeline += buildX264Encoder(opts);
	pipeline += buildAudioPipeline("generic", "v4l_mjpeg", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildTestPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const framerate = opts.framerate || 30;
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `videotestsrc ! video/x-raw,width=${dims.width},height=${dims.height},framerate=${framerate}/1 ! queue ! `;
	pipeline += overlay;
	pipeline += "videoconvert ! ";
	pipeline += buildX264Encoder(opts);
	pipeline += buildTestAudioPipeline(opts.audioCodec, opts.audioBitrate);
	pipeline += buildMuxAndSink();

	return pipeline;
}

export const genericBuilder: HardwareBuilder = {
	hardware: "generic",

	getSupportedSources(): SourceMeta[] {
		return SUPPORTED_SOURCES;
	},

	buildPipeline(source: VideoSource, overrides: PipelineOverrides): string {
		switch (source) {
			case "camlink":
				return buildCamlinkPipeline(overrides);
			case "v4l_mjpeg":
				return buildV4lMjpegPipeline(overrides);
			case "test":
				return buildTestPipeline(overrides);
			default:
				throw new Error(`Generic does not support source: ${source}`);
		}
	},
};
