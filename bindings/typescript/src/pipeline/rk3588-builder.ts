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
		source: "hdmi",
		description: "HDMI capture via /dev/hdmirx",
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
		source: "usb_mjpeg",
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
		defaultFramerate: 30,
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

function buildRk3588Encoder(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const gop = calculateGop(opts.framerate || 30);
	return `mpph265enc zero-copy-pkt=0 qp-max=51 gop=${gop} width=${dims.width} height=${dims.height} name=venc_bps ! h265parse config-interval=-1 ! ${buildVideoQueue()}`;
}

function buildHdmiPipeline(opts: PipelineOverrides): string {
	const device = opts.videoDevice || "/dev/hdmirx";
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src device=${device} ! ${buildIdentityChain()}`;
	pipeline += fps;
	pipeline += overlay;
	pipeline += "queue ! ";
	pipeline += buildRk3588Encoder(opts);
	pipeline += buildAudioPipeline("rk3588", "hdmi", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildLibuvch264Pipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const framerate = opts.framerate || 30;
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `libuvch264src ! video/x-h264,width=${dims.width},height=${dims.height},framerate=${framerate}/1 ! `;
	pipeline += "identity name=ptsfixup signal-handoffs=TRUE ! ";
	pipeline += "queue max-size-time=10000000000 max-size-buffers=1000 max-size-bytes=41943040 ! h264parse ! mppvideodec ! ";
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += buildRk3588Encoder(opts);
	pipeline += buildAudioPipeline("rk3588", "libuvch264", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildUsbMjpegPipeline(opts: PipelineOverrides): string {
	const resolution = opts.resolution || "1080p";
	const dims = getResolutionDims(resolution);
	const fps = buildVideoRateFilter(opts.framerate);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `v4l2src ! image/jpeg,width=${dims.width},height=${dims.height} ! `;
	pipeline += "jpegdec ! identity name=ptsfixup signal-handoffs=TRUE ! ";
	pipeline += "identity name=v_delay signal-handoffs=TRUE ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "queue ! ";
	pipeline += buildRk3588Encoder(opts);
	pipeline += buildAudioPipeline("rk3588", "usb_mjpeg", opts);
	pipeline += buildMuxAndSink();

	return pipeline;
}

function buildRtmpPipeline(opts: PipelineOverrides): string {
	const url = opts.rtmpUrl || "rtmp://127.0.0.1/publish/live";
	const fps = buildVideoRateFilter(opts.framerate || 30);
	const overlay = buildOverlay(opts.bitrateOverlay);

	let pipeline = `rtmpsrc location=${url} ! flvdemux name=demux `;
	pipeline += "demux.video ! identity name=v_delay signal-handoffs=TRUE ! h264parse ! mppvideodec ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "queue ! ";
	pipeline += buildRk3588Encoder(opts);
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
	pipeline += "demux.video ! identity name=v_delay signal-handoffs=TRUE ! h264parse ! mppvideodec ! ";
	pipeline += fps;
	pipeline += overlay;
	pipeline += "queue ! ";
	pipeline += buildRk3588Encoder(opts);
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
	pipeline += buildRk3588Encoder(opts);
	pipeline += buildTestAudioPipeline(opts.audioCodec, opts.audioBitrate);
	pipeline += buildMuxAndSink();

	return pipeline;
}

export const rk3588Builder: HardwareBuilder = {
	hardware: "rk3588",

	getSupportedSources(): SourceMeta[] {
		return SUPPORTED_SOURCES;
	},

	buildPipeline(source: VideoSource, overrides: PipelineOverrides): string {
		switch (source) {
			case "hdmi":
				return buildHdmiPipeline(overrides);
			case "libuvch264":
				return buildLibuvch264Pipeline(overrides);
			case "usb_mjpeg":
				return buildUsbMjpegPipeline(overrides);
			case "rtmp":
				return buildRtmpPipeline(overrides);
			case "srt":
				return buildSrtPipeline(overrides);
			case "test":
				return buildTestPipeline(overrides);
			default:
				throw new Error(`RK3588 does not support source: ${source}`);
		}
	},
};
