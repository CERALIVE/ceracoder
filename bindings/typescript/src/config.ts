import { z } from "zod";
import {
	DEFAULT_ADAPTIVE,
	DEFAULT_AIMD,
	DEFAULT_BALANCER,
	DEFAULT_MAX_BITRATE,
	DEFAULT_MIN_BITRATE,
	DEFAULT_SRT_LATENCY,
} from "./constants.js";
import {
	CeracoderConfig,
	ceracoderConfigSchema,
	PartialCeracoderConfig,
} from "./types.js";

type MutableConfig = CeracoderConfig & {
	general: CeracoderConfig["general"];
};

function applyDefaults(input?: PartialCeracoderConfig): CeracoderConfig {
	const parsed = ceracoderConfigSchema.parse(input ?? {});
	const merged: MutableConfig = {
		general: {
			min_bitrate:
				input?.general?.min_bitrate ?? parsed.general.min_bitrate ?? DEFAULT_MIN_BITRATE,
			max_bitrate:
				input?.general?.max_bitrate ?? parsed.general.max_bitrate ?? DEFAULT_MAX_BITRATE,
			balancer: input?.general?.balancer ?? parsed.general.balancer ?? DEFAULT_BALANCER,
		},
		srt: {
			latency: input?.srt?.latency ?? parsed.srt.latency ?? DEFAULT_SRT_LATENCY,
		},
		adaptive:
			parsed.general.balancer === "adaptive"
				? {
						...DEFAULT_ADAPTIVE,
						...(parsed.adaptive ?? {}),
					}
				: undefined,
		aimd:
			parsed.general.balancer === "aimd"
				? {
						...DEFAULT_AIMD,
						...(parsed.aimd ?? {}),
					}
				: undefined,
	};
	return merged;
}

export function createCeracoderConfig(
	input?: PartialCeracoderConfig,
): CeracoderConfig {
	return applyDefaults(input);
}

function formatSection(name: string, kv: Record<string, string | number | undefined>) {
	const lines = Object.entries(kv)
		.filter(([, v]) => v !== undefined)
		.map(([k, v]) => `${k} = ${v}`);
	if (!lines.length) return "";
	return `[${name}]\n${lines.join("\n")}\n\n`;
}

export function serializeCeracoderConfig(config: CeracoderConfig): string {
	const general = formatSection("general", {
		min_bitrate: config.general.min_bitrate,
		max_bitrate: config.general.max_bitrate,
		balancer: config.general.balancer,
	});

	const srt = formatSection("srt", {
		latency: config.srt.latency,
	});

	const adaptive = formatSection("adaptive", {
		incr_step: config.adaptive?.incr_step,
		decr_step: config.adaptive?.decr_step,
		incr_interval: config.adaptive?.incr_interval,
		decr_interval: config.adaptive?.decr_interval,
		loss_threshold: config.adaptive?.loss_threshold,
	});

	const aimd = formatSection("aimd", {
		incr_step: config.aimd?.incr_step,
		decr_mult: config.aimd?.decr_mult,
		incr_interval: config.aimd?.incr_interval,
		decr_interval: config.aimd?.decr_interval,
	});

	return `${general}${srt}${adaptive}${aimd}`.trimEnd() + "\n";
}

function parseSection(lines: Array<string>): Record<string, string> {
	return lines.reduce<Record<string, string>>((acc, line) => {
		const trimmed = line.trim();
		if (!trimmed || trimmed.startsWith("#") || trimmed.startsWith(";")) return acc;
		const [key, ...rest] = trimmed.split("=");
		if (!key || rest.length === 0) return acc;
		acc[key.trim()] = rest.join("=").trim();
		return acc;
	}, {});
}

export function parseCeracoderConfig(ini: string): CeracoderConfig {
	const sections: Record<string, Array<string>> = {};
	let current: string | null = null;
	ini.split(/\r?\n/).forEach((line) => {
		const trimmed = line.trim();
		const sectionMatch = trimmed.match(/^\[(.+)]$/);
		if (sectionMatch) {
			current = sectionMatch[1].toLowerCase();
			sections[current] = [];
			return;
		}
		if (current) {
			sections[current].push(line);
		}
	});

	const generalRaw = parseSection(sections.general ?? []);
	const srtRaw = parseSection(sections.srt ?? []);
	const adaptiveRaw = parseSection(sections.adaptive ?? []);
	const aimdRaw = parseSection(sections.aimd ?? []);

	const parsed = ceracoderConfigSchema.parse({
		general: {
			min_bitrate: generalRaw.min_bitrate ? Number(generalRaw.min_bitrate) : undefined,
			max_bitrate: generalRaw.max_bitrate ? Number(generalRaw.max_bitrate) : undefined,
			balancer: generalRaw.balancer as z.infer<typeof ceracoderConfigSchema>["general"]["balancer"],
		},
		srt: {
			latency: srtRaw.latency ? Number(srtRaw.latency) : undefined,
		},
		adaptive: Object.keys(adaptiveRaw).length
			? {
					incr_step: adaptiveRaw.incr_step ? Number(adaptiveRaw.incr_step) : undefined,
					decr_step: adaptiveRaw.decr_step ? Number(adaptiveRaw.decr_step) : undefined,
					incr_interval: adaptiveRaw.incr_interval ? Number(adaptiveRaw.incr_interval) : undefined,
					decr_interval: adaptiveRaw.decr_interval ? Number(adaptiveRaw.decr_interval) : undefined,
					loss_threshold: adaptiveRaw.loss_threshold
						? Number(adaptiveRaw.loss_threshold)
						: undefined,
				}
			: undefined,
		aimd: Object.keys(aimdRaw).length
			? {
					incr_step: aimdRaw.incr_step ? Number(aimdRaw.incr_step) : undefined,
					decr_mult: aimdRaw.decr_mult ? Number(aimdRaw.decr_mult) : undefined,
					incr_interval: aimdRaw.incr_interval ? Number(aimdRaw.incr_interval) : undefined,
					decr_interval: aimdRaw.decr_interval ? Number(aimdRaw.decr_interval) : undefined,
				}
			: undefined,
	});

	return applyDefaults(parsed);
}

export function buildCeracoderConfig(
	input?: PartialCeracoderConfig,
): { config: CeracoderConfig; ini: string } {
	const config = createCeracoderConfig(input);
	const ini = serializeCeracoderConfig(config);
	return { config, ini };
}
