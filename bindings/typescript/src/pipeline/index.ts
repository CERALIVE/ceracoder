import fs from "node:fs";
import type {
	BuildPipelineRequest,
	BuildPipelineResult,
	HardwareBuilder,
	HardwareType,
	PipelineOverrides,
	SourceMeta,
	VideoSource,
} from "./types.js";
import { jetsonBuilder } from "./jetson-builder.js";
import { rk3588Builder } from "./rk3588-builder.js";
import { n100Builder } from "./n100-builder.js";
import { genericBuilder } from "./generic-builder.js";

// Re-export types
export * from "./types.js";

// Registry of all hardware builders
const BUILDERS: Record<HardwareType, HardwareBuilder> = {
	jetson: jetsonBuilder,
	rk3588: rk3588Builder,
	n100: n100Builder,
	generic: genericBuilder,
};

/**
 * Pipeline Builder - single entry point for building GStreamer pipelines
 *
 * Supports multiple hardware platforms with hardware-specific encoders:
 * - Jetson: NVIDIA nvv4l2h265enc
 * - RK3588: Rockchip mpph265enc
 * - N100: Intel QuickSync qsvh265enc
 * - Generic: Software x264enc
 */
export class PipelineBuilder {
	/**
	 * List all supported hardware types
	 */
	static listHardwareTypes(): HardwareType[] {
		return Object.keys(BUILDERS) as HardwareType[];
	}

	/**
	 * List available video sources for a specific hardware type
	 */
	static listSources(hardware: HardwareType): SourceMeta[] {
		const builder = BUILDERS[hardware];
		if (!builder) {
			throw new Error(`Unknown hardware type: ${hardware}`);
		}
		return builder.getSupportedSources();
	}

	/**
	 * Check if a hardware type supports a specific source
	 */
	static supportsSource(hardware: HardwareType, source: VideoSource): boolean {
		const sources = PipelineBuilder.listSources(hardware);
		return sources.some((s) => s.source === source);
	}

	/**
	 * Get metadata for a specific source on a hardware type
	 */
	static getSourceMeta(hardware: HardwareType, source: VideoSource): SourceMeta | undefined {
		const sources = PipelineBuilder.listSources(hardware);
		return sources.find((s) => s.source === source);
	}

	/**
	 * Build a GStreamer pipeline
	 *
	 * @param request - Pipeline build request with hardware, source, and optional overrides
	 * @returns Pipeline string, optional file path, and metadata
	 * @throws Error if hardware or source is not supported
	 *
	 * @example
	 * ```typescript
	 * const result = PipelineBuilder.build({
	 *   hardware: "jetson",
	 *   source: "camlink",
	 *   overrides: { resolution: "1080p", framerate: 30 }
	 * });
	 * console.log(result.pipeline);
	 * ```
	 */
	static build(request: BuildPipelineRequest): BuildPipelineResult {
		const { hardware, source, overrides = {}, writeTo } = request;

		// Validate hardware and source
		if (!PipelineBuilder.supportsSource(hardware, source)) {
			const supportedSources = PipelineBuilder.listSources(hardware).map((s) => s.source);
			throw new Error(
				`Hardware '${hardware}' does not support source '${source}'. ` +
				`Supported sources: ${supportedSources.join(", ")}`,
			);
		}

		const builder = BUILDERS[hardware];
		const meta = PipelineBuilder.getSourceMeta(hardware, source);
		if (!meta) {
			throw new Error(`Source metadata not found for ${hardware}/${source}`);
		}

		// Apply default values from metadata if not specified
		const effectiveOverrides: PipelineOverrides = {
			...overrides,
			resolution: overrides.resolution ?? meta.defaultResolution,
			framerate: overrides.framerate ?? meta.defaultFramerate,
			bitrateOverlay: overrides.bitrateOverlay ?? true,
		};

		// Build the pipeline
		const pipeline = builder.buildPipeline(source, effectiveOverrides);

		// Validate required elements
		PipelineBuilder.validate(pipeline);

		// Optionally write to file
		let path: string | undefined;
		if (writeTo) {
			fs.writeFileSync(writeTo, pipeline);
			path = writeTo;
		}

		return {
			pipeline,
			path,
			hardware,
			source,
			meta,
		};
	}

	/**
	 * Validate that a pipeline contains required elements
	 */
	private static validate(pipeline: string): void {
		if (!pipeline.includes("name=appsink")) {
			throw new Error("Pipeline must contain appsink with name=appsink");
		}
		if (!pipeline.includes("name=venc_bps") && !pipeline.includes("name=venc_kbps")) {
			throw new Error("Pipeline must contain encoder with name=venc_bps or name=venc_kbps");
		}
		if (!pipeline.includes("mux")) {
			throw new Error("Pipeline must contain mpegtsmux with name=mux");
		}
	}
}

// Re-export individual builders for advanced use cases
export { jetsonBuilder } from "./jetson-builder.js";
export { rk3588Builder } from "./rk3588-builder.js";
export { n100Builder } from "./n100-builder.js";
export { genericBuilder } from "./generic-builder.js";
