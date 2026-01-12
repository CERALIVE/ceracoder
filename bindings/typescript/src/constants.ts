export const DEFAULT_MIN_BITRATE = 300; // Kbps
export const DEFAULT_MAX_BITRATE = 6000; // Kbps
export const DEFAULT_SRT_LATENCY = 2000; // ms
export const DEFAULT_BALANCER = "adaptive" as const;

export const DEFAULT_ADAPTIVE = {
	incr_step: 30,
	decr_step: 100,
	incr_interval: 500,
	decr_interval: 200,
	loss_threshold: 0.5,
} as const;

export const DEFAULT_AIMD = {
	incr_step: 50,
	decr_mult: 0.75,
	incr_interval: 500,
	decr_interval: 200,
} as const;

export const DEFAULT_PIPELINE_ROOT = "/usr/share/ceracoder/pipelines";
export const TEMP_PIPELINE_PATH = "/tmp/ceracoder_pipeline";
export const DEFAULT_CONFIG_PATH = "/tmp/ceracoder.conf";
