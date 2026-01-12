import type { AudioCodec, Framerate, HardwareType, Resolution, VideoSource } from "./types.js";
import { RESOLUTION_DIMS } from "./types.js";

// Default audio devices by hardware and source
const DEFAULT_AUDIO_DEVICES: Record<HardwareType, Record<string, string>> = {
	jetson: {
		default: "hw:2",
	},
	rk3588: {
		hdmi: "hw:CARD=rockchiphdmiin",
		default: "hw:CARD=rockchiphdmiin",
	},
	n100: {
		default: "hw:1",
	},
	generic: {
		default: "hw:2",
	},
};

export function getDefaultAudioDevice(hardware: HardwareType, source: VideoSource): string {
	const hwDevices = DEFAULT_AUDIO_DEVICES[hardware];
	return hwDevices[source] || hwDevices.default;
}

export function buildVideoRateFilter(framerate?: Framerate): string {
	if (!framerate) return "";
	// Handle decimal framerates
	if (framerate === 29.97) {
		return "videorate ! video/x-raw,framerate=30000/1001 ! ";
	}
	if (framerate === 59.94) {
		return "videorate ! video/x-raw,framerate=60000/1001 ! ";
	}
	return `videorate ! video/x-raw,framerate=${framerate}/1 ! `;
}

export function buildOverlay(enabled = true): string {
	if (!enabled) return "";
	return "textoverlay text='' valignment=top halignment=right font-desc=\"Monospace, 5\" name=overlay ! queue ! ";
}

export function buildIdentityChain(includePtsFix = true, includeDropFlags = true): string {
	let chain = "";
	if (includePtsFix) {
		chain += "identity name=ptsfixup signal-handoffs=TRUE ! ";
	}
	if (includeDropFlags) {
		chain += "identity drop-buffer-flags=GST_BUFFER_FLAG_DROPPABLE ! ";
	}
	chain += "identity name=v_delay signal-handoffs=TRUE ! ";
	return chain;
}

export function buildAudioPipeline(
	hardware: HardwareType,
	source: VideoSource,
	opts: {
		audioDevice?: string;
		audioCodec?: AudioCodec;
		audioBitrate?: number;
		volume?: number;
	},
): string {
	const device = opts.audioDevice || getDefaultAudioDevice(hardware, source);
	const volume = opts.volume ?? 1.0;
	const codec = opts.audioCodec || "aac";
	const bitrate = opts.audioBitrate || 128000;

	let encoderPipeline: string;
	if (codec === "opus") {
		encoderPipeline = `audioresample quality=10 sinc-filter-mode=1 ! opusenc bitrate=${bitrate} ! opusparse !`;
	} else {
		// AAC - use avenc_aac for generic, voaacenc for hardware platforms
		if (hardware === "generic") {
			encoderPipeline = `audioconvert ! avenc_aac bitrate=${bitrate + 3072} ! aacparse !`;
		} else {
			encoderPipeline = `audioconvert ! voaacenc bitrate=${bitrate} ! aacparse !`;
		}
	}

	return `alsasrc device=${device} ! identity name=a_delay signal-handoffs=TRUE ! volume volume=${volume} ! ${encoderPipeline} queue max-size-time=10000000000 max-size-buffers=1000 ! mux. `;
}

export function buildTestAudioPipeline(codec: AudioCodec = "aac", bitrate = 128000): string {
	if (codec === "opus") {
		return `audiotestsrc ! audio/x-raw,channels=2,rate=48000 ! audioresample quality=10 sinc-filter-mode=1 ! opusenc bitrate=${bitrate} ! opusparse ! queue max-size-time=10000000000 max-size-buffers=1000 ! mux. `;
	}
	return `audiotestsrc ! audio/x-raw,channels=2,rate=48000 ! voaacenc bitrate=${bitrate} ! aacparse ! queue max-size-time=10000000000 max-size-buffers=1000 ! mux. `;
}

export function buildMuxAndSink(): string {
	return "mpegtsmux name=mux ! appsink name=appsink";
}

export function buildVideoQueue(): string {
	return "queue max-size-time=10000000000 max-size-buffers=1000 max-size-bytes=41943040 ! mux. ";
}

export function getResolutionDims(resolution: Resolution): { width: number; height: number } {
	return RESOLUTION_DIMS[resolution];
}

export function calculateGop(framerate: Framerate): number {
	// GOP = 2 seconds worth of frames
	return Math.round(framerate * 2);
}
