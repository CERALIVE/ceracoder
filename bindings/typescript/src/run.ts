import { buildCeracoderConfig, parseCeracoderConfig, serializeCeracoderConfig } from "./config.js";
import { buildCeracoderArgs } from "./cli.js";
import type { PartialCeracoderConfig, CeracoderConfig, CeracoderCliOptions } from "./types.js";
import fs from "node:fs";

export type CeracoderRunInput = {
	pipelineFile: string;
	host: string;
	port: number;
	configFile: string;
	config?: PartialCeracoderConfig;
	/**
	 * If true, ignore existing config file and require a full config payload.
	 * If false (default), merge provided fields into existing config (if present).
	 */
	fullOverride?: boolean;
	delayMs?: number;
	streamId?: string;
	latencyMs?: number;
	reducedPacketSize?: boolean;
	algorithm?: CeracoderCliOptions["algorithm"];
};

export type CeracoderRunArtifacts = {
	config: CeracoderConfig;
	ini: string;
	args: Array<string>;
};

/**
 * Build ceracoder runtime artifacts (config object, INI text, CLI args).
 * Does NOT perform any filesystem writes—callers can persist the INI as needed.
 */
export function buildCeracoderRunArtifacts(
	input: CeracoderRunInput,
): CeracoderRunArtifacts {
	let baseConfig: PartialCeracoderConfig | undefined;

	const fullOverride = input.fullOverride ?? false;

	if (!fullOverride) {
		try {
			const existing = fs.readFileSync(input.configFile, "utf8");
			baseConfig = parseCeracoderConfig(existing);
		} catch {
			baseConfig = undefined;
		}
	}

	if (fullOverride && !input.config) {
		throw new Error("Full override requested but no config provided");
	}

	let mergedConfig: PartialCeracoderConfig;

	if (fullOverride) {
		mergedConfig = input.config!;
	} else {
		mergedConfig = {
			...baseConfig,
			...input.config,
			general: {
				...(baseConfig?.general ?? {}),
				...(input.config?.general ?? {}),
			},
			srt: {
				...(baseConfig?.srt ?? {}),
				...(input.config?.srt ?? {}),
			},
			adaptive: input.config?.adaptive ?? baseConfig?.adaptive,
			aimd: input.config?.aimd ?? baseConfig?.aimd,
		};
	}

	// Validate that balancer-specific sections are present when the balancer requires them in full override mode
	if (fullOverride) {
		if (!mergedConfig.general || !mergedConfig.srt) {
			throw new Error("Full override requires general and srt sections");
		}
		const balancer = mergedConfig.general?.balancer;
		if (balancer === "adaptive" && !mergedConfig.adaptive) {
			throw new Error("Full override requires adaptive section when balancer=adaptive");
		}
		if (balancer === "aimd" && !mergedConfig.aimd) {
			throw new Error("Full override requires aimd section when balancer=aimd");
		}
	}

	const { config, ini } = buildCeracoderConfig(mergedConfig);

	const args = buildCeracoderArgs({
		pipelineFile: input.pipelineFile,
		host: input.host,
		port: input.port,
		configFile: input.configFile,
		delayMs: input.delayMs,
		streamId: input.streamId,
		latencyMs: input.latencyMs ?? config.srt.latency,
		reducedPacketSize: input.reducedPacketSize,
		algorithm: input.algorithm ?? config.general.balancer,
	});

	return { config, ini, args };
}
