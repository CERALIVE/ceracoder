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
		source: "camlink",
		description: "Elgato Cam Link 4K (uncompressed YUY2)",
		defaultResolution: "1080p",
		defaultFramerate: 30,
		supportsAudio: true,
		supportsResolutionOverride: true,
		supportsFramerateOverride: true,
	},
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
		source: "rtmp",
		description: "RTMP ingest from local server",
		defaultResolution: "1080p",
		defaultFramerate: 25,
		supportsAudio: true,
		supportsResolutionOverride: false,
		supportsFramerateOverride: true,
	},
	{
		source: "srt",
		description: "SRT ingest on port 4000",
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

function buildJetsonEncoder(opts: PipelineOverrides): string {
	const gop = calculateGop(opts.framerate || 30);
	return `nvv4l2h265enc control-rate=1 qp-range="28,50:0,36:0,50" iframeinterval=${gop} preset-level=4 maxperf-enable=true EnableTwopassCBR=true insert-sps-pps=true name=venc_bps ! h265parse config-interval=-1 ! ${buildVideoQueue()}`;
}

function buildCamlinkPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src ! ${buildIdentityChain()}`;
	pipeline += fps;
	pipeline += overlay;
	pipeline += `nvvidconv interpolation-method=5 ! video/x-raw(memory:NVMM),width=${dims.width},height=${dims.height} ! `;
	pipeline += buildJetsonEncoder(opts);
	pipeline += buildAudioPipeline("jetson", "camlink", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildLibuvch264Pipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const framerate = opts.framerate || 30;
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `libuvch264src ! video/x-h264,width=${dims.width},height=${dims.height},framerate=${framerate}/1 ! `;
	pipeline += "queue max-size-time=10000000000 max-size-buffers=1000 max-size-bytes=41943040 ! nvv4l2decoder ! nvvidconv interpolation-method=5 ! ";
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += overlay;
	pipeline += "nvvidconv interpolation-method=5 ! ";
	pipeline += buildJetsonEncoder(opts);
	pipeline += buildAudioPipeline("jetson", "libuvch264", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildV4lMjpegPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src ! image/jpeg,width=${dims.width},height=${dims.height} ! `;
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += "nvv4l2decoder mjpeg=1 enable-max-performance=true ! nvvidconv ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "nvvidconv interpolation-method=5 ! ";
	pipeline += buildJetsonEncoder(opts);
	pipeline += buildAudioPipeline("jetson", "v4l_mjpeg", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildRtmpPipeline(opts: PipelineOverrides): string {
	const url = opts.rtmpUrl || "rtmp://127.0.0.1/publish/live";
	const fps = buildVideoRateFilter(opts.framerate || 25);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `rtmpsrc location=${url} ! flvdemux name=demux `;
	pipeline += "demux.video ! identity name=v_delay signal-handoffs=TRUE ! h264parse ! nvv4l2decoder ! nvvidconv ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "nvvidconv interpolation-method=5 ! ";
	pipeline += buildJetsonEncoder(opts);
	// RTMP audio comes from demuxer
	const volume = opts.volume ?? 1.0;
	pipeline += `demux.audio ! aacparse ! avdec_aac ! identity name=a_delay signal-handoffs=TRUE ! volume volume=${volume} ! audioconvert ! voaacenc bitrate=128000 ! aacparse ! queue max-size-time=10000000000 max-size-buffers=1000 ! mux. `;
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildSrtPipeline(opts: PipelineOverrides): string {
	const port = opts.srtPort || 4000;
	const fps = buildVideoRateFilter(opts.framerate || 30);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `srtsrc uri=srt://:${port} ! tsdemux name=demux `;
	pipeline += "demux.video ! identity name=v_delay signal-handoffs=TRUE ! h264parse ! nvv4l2decoder ! nvvidconv ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "nvvidconv interpolation-method=5 ! ";
	pipeline += buildJetsonEncoder(opts);
	// SRT audio comes from demuxer
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
	pipeline += overlay;
	pipeline += "nvvidconv interpolation-method=5 ! ";
	pipeline += buildJetsonEncoder(opts);
	pipeline += buildTestAudioPipeline(opts.audioCodec, opts.audioBitrate);
	pipeline += buildMuxAndSink();

	return pipeline;
}

export const jetsonBuilder: HardwareBuilder = {
	hardware: "jetson",

	getSupportedSources(): SourceMeta[] {
		return SUPPORTED_SOURCES;
	},

	buildPipeline(source: VideoSource, overrides: PipelineOverrides): string {
		switch (source) {
			case "camlink":
				return buildCamlinkPipeline(overrides);
			case "libuvch264":
				return buildLibuvch264Pipeline(overrides);
			case "v4l_mjpeg":
				return buildV4lMjpegPipeline(overrides);
			case "rtmp":
				return buildRtmpPipeline(overrides);
			case "srt":
				return buildSrtPipeline(overrides);
			case "test":
				return buildTestPipeline(overrides);
			default:
				throw new Error(`Jetson does not support source: ${source}`);
		}
	},
};
