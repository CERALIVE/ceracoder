import { describe, it, expect } from "bun:test";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

import { buildCeracoderRunArtifacts } from "./run.js";
import { serializeCeracoderConfig } from "./config.js";
import { DEFAULT_ADAPTIVE, DEFAULT_AIMD } from "./constants.js";

function tmpFile(contents: string) {
	const p = path.join(os.tmpdir(), `ceracoder_test_${Date.now()}_${Math.random()}.conf`);
	fs.writeFileSync(p, contents);
	return p;
}

describe("buildCeracoderRunArtifacts", () => {
	it("merges with existing config when fullOverride=false", () => {
		const existingIni = serializeCeracoderConfig({
			general: { min_bitrate: 400, max_bitrate: 5000, balancer: "adaptive" },
			srt: { latency: 1500 },
			adaptive: { ...DEFAULT_ADAPTIVE, incr_step: 10 },
			aimd: undefined,
		});
		const cfgPath = tmpFile(existingIni);

		const { config } = buildCeracoderRunArtifacts({
			pipelineFile: "p",
			host: "h",
			port: 1,
			configFile: cfgPath,
			config: { general: { max_bitrate: 6000 } },
			fullOverride: false,
		});

		expect(config.general.max_bitrate).toBe(6000);
		expect(config.adaptive?.incr_step).toBe(10); // preserved from disk
	});

	it("requires adaptive when balancer=adaptive in full override", () => {
		expect(() =>
			buildCeracoderRunArtifacts({
				pipelineFile: "p",
				host: "h",
				port: 1,
				configFile: "/tmp/none",
				config: {
					general: { min_bitrate: 300, max_bitrate: 4000, balancer: "adaptive" },
					srt: { latency: 2000 },
				},
				fullOverride: true,
			}),
		).toThrow();
	});

	it("requires aimd when balancer=aimd in full override", () => {
		expect(() =>
			buildCeracoderRunArtifacts({
				pipelineFile: "p",
				host: "h",
				port: 1,
				configFile: "/tmp/none",
				config: {
					general: { min_bitrate: 300, max_bitrate: 4000, balancer: "aimd" },
					srt: { latency: 2000 },
				},
				fullOverride: true,
			}),
		).toThrow();
	});

	it("succeeds in full override when required sections provided", () => {
		const { config } = buildCeracoderRunArtifacts({
			pipelineFile: "p",
			host: "h",
			port: 1,
			configFile: "/tmp/none",
			config: {
				general: { min_bitrate: 300, max_bitrate: 4000, balancer: "aimd" },
				srt: { latency: 2000 },
				aimd: { ...DEFAULT_AIMD },
			},
			fullOverride: true,
		});
		expect(config.general.max_bitrate).toBe(4000);
		expect(config.aimd?.decr_mult).toBe(DEFAULT_AIMD.decr_mult);
	});
});
