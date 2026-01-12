import { cliOptionsSchema, type CeracoderCliOptions } from "./types.js";

export function buildCeracoderArgs(options: CeracoderCliOptions): Array<string> {
	const opts = cliOptionsSchema.parse(options);

	const args: Array<string> = [
		opts.pipelineFile,
		opts.host,
		String(opts.port),
		"-c",
		opts.configFile,
	];

	if (opts.delayMs !== undefined) {
		args.push("-d", String(opts.delayMs));
	}

	if (opts.streamId) {
		args.push("-s", opts.streamId);
	}

	if (opts.latencyMs !== undefined) {
		args.push("-l", String(opts.latencyMs));
	}

	if (opts.reducedPacketSize) {
		args.push("-r");
	}

	if (opts.algorithm) {
		args.push("-a", opts.algorithm);
	}

	return args;
}
