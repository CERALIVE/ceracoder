import { z } from "zod";
import {
	DEFAULT_ADAPTIVE,
	DEFAULT_AIMD,
	DEFAULT_BALANCER,
	DEFAULT_MAX_BITRATE,
	DEFAULT_MIN_BITRATE,
	DEFAULT_SRT_LATENCY,
} from "./constants.js";

export const balancerAlgorithmSchema = z.enum(["adaptive", "fixed", "aimd"]);
export type BalancerAlgorithm = z.infer<typeof balancerAlgorithmSchema>;

export const adaptiveSchema = z
	.object({
		incr_step: z.number().int().positive(),
		decr_step: z.number().int().positive(),
		incr_interval: z.number().int().positive(),
		decr_interval: z.number().int().positive(),
		loss_threshold: z.number().positive(),
	})
	.optional();

export const aimdSchema = z
	.object({
		incr_step: z.number().int().positive(),
		decr_mult: z.number().positive(),
		incr_interval: z.number().int().positive(),
		decr_interval: z.number().int().positive(),
	})
	.optional();

export const ceracoderConfigSchema = z.object({
	general: z.object({
		min_bitrate: z.number().int().min(1).default(DEFAULT_MIN_BITRATE),
		max_bitrate: z.number().int().min(1).default(DEFAULT_MAX_BITRATE),
		balancer: balancerAlgorithmSchema.default(DEFAULT_BALANCER),
	}),
	srt: z
		.object({
			latency: z
				.number()
				.int()
				.min(100)
				.max(10_000)
				.default(DEFAULT_SRT_LATENCY),
		})
		.default({ latency: DEFAULT_SRT_LATENCY }),
	adaptive: adaptiveSchema,
	aimd: aimdSchema,
});

export type CeracoderConfig = z.infer<typeof ceracoderConfigSchema>;
export type PartialCeracoderConfig = z.input<typeof ceracoderConfigSchema>;

// CLI options
export const cliOptionsSchema = z.object({
	pipelineFile: z.string().min(1),
	host: z.string().min(1),
	port: z.number().int().min(1).max(65535),
	configFile: z.string().min(1),
	delayMs: z.number().int().min(-10_000).max(10_000).optional(),
	streamId: z.string().optional(),
	latencyMs: z.number().int().min(100).max(10_000).optional(),
	reducedPacketSize: z.boolean().optional(),
	algorithm: balancerAlgorithmSchema.optional(),
});

export type CeracoderCliOptions = z.infer<typeof cliOptionsSchema>;
