import type { HardwareBuilder, PipelineOverrides, SourceMeta, VideoSource } from "./types.js";
import {
	buildAudioPipeline,
	buildIdentityChain,
	buildMuxAndSink,
	buildOverlay,
	buildTestAudioPipeline,
	buildVideoQueue,
	buildVideoRateFilter,
	calculateGop,
	getResolutionDims,
} from "./common.js";

const SUPPORTED_SOURCES: SourceMeta[] = [
	{
		source: "libuvch264",
		description: "UVC H264 camera (hardware compressed)",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: true,
		supportsFramerateOverride: true,
	},
	{
		source: "v4l_mjpeg",
		description: "USB MJPEG capture card",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: true,
		supportsFramerateOverride: true,
	},
	{
		source: "decklink",
		description: "Blackmagic Decklink SDI capture",
		defaultResolution: "1080p",
		defaultFramerate: 50,
		supportsAudio: true,
		supportsResolutionOverride: false,
		supportsFramerateOverride: true,
	},
	{
		source: "rtmp",
		description: "RTMP ingest from local server",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: false,
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

function buildN100Encoder(opts: PipelineOverrides): string {
	const gop = calculateGop(opts.framerate || 30);
	return `qsvh265enc gop-size=${gop} rate-control=1 target-usage=7 low-latency=true name=venc_kbps ! h265parse config-interval=-1 ! ${buildVideoQueue()}`;
}

function buildLibuvch264Pipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const framerate = opts.framerate || 30;
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `libuvch264src ! video/x-h264,width=${dims.width},height=${dims.height},framerate=${framerate}/1,profile=high ! `;
	pipeline += "identity name=ptsfixup signal-handoffs=TRUE ! ";
	pipeline += "queue max-size-time=10000000000 max-size-buffers=1000 max-size-bytes=41943040 ! h264parse ! qsvh264dec ! ";
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += "video/x-raw,format=NV12 ! videoconvert ! ";
	pipeline += overlay;
	pipeline += buildN100Encoder(opts);
	pipeline += buildAudioPipeline("n100", "libuvch264", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildV4lMjpegPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src ! image/jpeg,width=${dims.width},height=${dims.height} ! `;
	pipeline += "jpegdec ! identity name=ptsfixup signal-handoffs=TRUE ! ";
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += fps;
	pipeline += "videoconvert ! video/x-raw,format=NV12 ! ";
	pipeline += overlay;
	pipeline += buildN100Encoder(opts);
	pipeline += buildAudioPipeline("n100", "v4l_mjpeg", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildDecklinkPipeline(opts: PipelineOverrides): string {
	const framerate = opts.framerate || 50;
	const overlay = buildOverlay(opts.bitrateOverlay);

	// Decklink mode 14 = 1080p50
	let mode = 14;
	if (framerate === 25) mode = 13;
	if (framerate === 30) mode = 11;
	if (framerate === 60) mode = 15;

	let pipeline = `decklinkvideosrc device-number=0 connection=sdi mode=${mode} video-format=8bit-yuv ! `;
	pipeline += buildIdentityChain();
	pipeline += "videoconvert ! video/x-raw,format=I420 ! vapostproc ! video/x-raw,format=NV12 ! ";
	pipeline += overlay;
	pipeline += buildN100Encoder(opts);
	pipeline += buildAudioPipeline("n100", "decklink", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildRtmpPipeline(opts: PipelineOverrides): string {
	const url = opts.rtmpUrl || "rtmp://127.0.0.1/publish/live";
	const fps = buildVideoRateFilter(opts.framerate || 30);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `rtmpsrc location=${url} ! flvdemux name=demux `;
	pipeline += "demux.video ! identity name=v_delay signal-handoffs=TRUE ! h264parse ! qsvh264dec ! ";
	pipeline += fps;
	pipeline += "videoconvert ! video/x-raw,format=NV12 ! ";
	pipeline += overlay;
	pipeline += buildN100Encoder(opts);
	// RTMP audio comes from demuxer
	const volume = opts.volume ?? 1.0;
	pipeline += `demux.audio ! aacparse ! avdec_aac ! identity name=a_delay signal-handoffs=TRUE ! volume volume=${volume} ! audioconvert ! voaacenc bitrate=128000 ! aacparse ! queue max-size-time=10000000000 max-size-buffers=1000 ! mux. `;
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildTestPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const framerate = opts.framerate || 30;
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `videotestsrc ! video/x-raw,width=${dims.width},height=${dims.height},framerate=${framerate}/1 ! queue ! `;
	pipeline += "videoconvert ! video/x-raw,format=NV12 ! ";
	pipeline += overlay;
	pipeline += buildN100Encoder(opts);
	pipeline += buildTestAudioPipeline(opts.audioCodec, opts.audioBitrate);
	pipeline += buildMuxAndSink();

	return pipeline;
}

export const n100Builder: HardwareBuilder = {
	hardware: "n100",

	getSupportedSources(): SourceMeta[] {
		return SUPPORTED_SOURCES;
	},

	buildPipeline(source: VideoSource, overrides: PipelineOverrides): string {
		switch (source) {
			case "libuvch264":
				return buildLibuvch264Pipeline(overrides);
			case "v4l_mjpeg":
				return buildV4lMjpegPipeline(overrides);
			case "decklink":
				return buildDecklinkPipeline(overrides);
			case "rtmp":
				return buildRtmpPipeline(overrides);
			case "test":
				return buildTestPipeline(overrides);
			default:
				throw new Error(`N100 does not support source: ${source}`);
		}
	},
};
