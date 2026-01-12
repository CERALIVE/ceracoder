import { z } from "zod";

// Hardware types
export const hardwareTypeSchema = z.enum(["jetson", "n100", "rk3588", "generic"]);
export type HardwareType = z.infer<typeof hardwareTypeSchema>;

// Video source types - all possible sources across all hardware
export const videoSourceSchema = z.enum([
	"camlink",        // Elgato Cam Link 4K (uncompressed YUY2)
	"libuvch264",     // UVC H264 camera (hardware compressed)
	"hdmi",           // HDMI capture
	"usb_mjpeg",      // USB MJPEG capture card
	"v4l_mjpeg",      // V4L2 MJPEG capture
	"rtmp",           // RTMP ingest
	"srt",            // SRT ingest
	"test",           // Test pattern
	"decklink",       // Blackmagic Decklink SDI
]);
export type VideoSource = z.infer<typeof videoSourceSchema>;

// Video codec types
export const videoCodecSchema = z.enum(["h264", "h265", "x264"]);
export type VideoCodec = z.infer<typeof videoCodecSchema>;

// Audio codec types
export const audioCodecSchema = z.enum(["aac", "opus"]);
export type AudioCodec = z.infer<typeof audioCodecSchema>;

// Audio codec lookup for validation
export const AUDIO_CODECS: Record<AudioCodec, { name: string }> = {
	aac: { name: "AAC" },
	opus: { name: "Opus" },
};

// Video source labels (for UI display when translations not available)
export const VIDEO_SOURCE_LABELS: Record<VideoSource, string> = {
	camlink: "Cam Link 4K",
	libuvch264: "UVC H264 Camera",
	hdmi: "HDMI Capture",
	usb_mjpeg: "USB MJPEG",
	v4l_mjpeg: "V4L2 MJPEG",
	rtmp: "RTMP Ingest",
	srt: "SRT Ingest",
	test: "Test Pattern",
	decklink: "Decklink SDI",
};

// Hardware type labels (for UI display when translations not available)
export const HARDWARE_LABELS: Record<HardwareType, string> = {
	jetson: "NVIDIA Jetson",
	rk3588: "Rockchip RK3588",
	n100: "Intel N100",
	generic: "Generic (Software)",
};

// Resolution presets
export const resolutionSchema = z.enum(["480p", "720p", "1080p", "1440p", "2160p", "4k"]);
export type Resolution = z.infer<typeof resolutionSchema>;

// Frame rate values
export const framerateSchema = z.union([
	z.literal(25),
	z.literal(29.97),
	z.literal(30),
	z.literal(50),
	z.literal(59.94),
	z.literal(60),
]);
export type Framerate = z.infer<typeof framerateSchema>;

// x264 preset types
export const x264PresetSchema = z.enum(["superfast", "veryfast", "fast", "medium"]);
export type X264Preset = z.infer<typeof x264PresetSchema>;

// Resolution to dimensions mapping
export const RESOLUTION_DIMS: Record<Resolution, { width: number; height: number }> = {
	"480p": { width: 854, height: 480 },
	"720p": { width: 1280, height: 720 },
	"1080p": { width: 1920, height: 1080 },
	"1440p": { width: 2560, height: 1440 },
	"2160p": { width: 3840, height: 2160 },
	"4k": { width: 3840, height: 2160 },
};

// Pipeline override options
export const pipelineOverridesSchema = z.object({
	resolution: resolutionSchema.optional(),
	framerate: framerateSchema.optional(),
	audioDevice: z.string().optional(),
	audioCodec: audioCodecSchema.optional(),
	audioBitrate: z.number().optional(),
	bitrateOverlay: z.boolean().optional(),
	videoDevice: z.string().optional(),
	volume: z.number().optional(),
	x264Preset: x264PresetSchema.optional(),
	rtmpUrl: z.string().optional(),
	srtPort: z.number().optional(),
});
export type PipelineOverrides = z.infer<typeof pipelineOverridesSchema>;

// Build pipeline request
export const buildPipelineRequestSchema = z.object({
	hardware: hardwareTypeSchema,
	source: videoSourceSchema,
	overrides: pipelineOverridesSchema.optional(),
	writeTo: z.string().optional(),
});
export type BuildPipelineRequest = z.infer<typeof buildPipelineRequestSchema>;

// Source metadata
export interface SourceMeta {
	source: VideoSource;
	description: string;
	defaultResolution?: Resolution;
	defaultFramerate?: Framerate;
	supportsAudio: boolean;
	supportsResolutionOverride: boolean;
	supportsFramerateOverride: boolean;
}

// Build pipeline result
export interface BuildPipelineResult {
	pipeline: string;
	path?: string;
	hardware: HardwareType;
	source: VideoSource;
	meta: SourceMeta;
}

// Hardware builder interface
export interface HardwareBuilder {
	readonly hardware: HardwareType;
	getSupportedSources(): SourceMeta[];
	buildPipeline(source: VideoSource, overrides: PipelineOverrides): string;
}
