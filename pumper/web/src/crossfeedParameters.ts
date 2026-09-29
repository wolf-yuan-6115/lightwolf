import { CrossfeedMode, type CrossfeedConfig } from "./protocol";

const presets = {
  [CrossfeedMode.Off]: { strengthPercent: 0, cutoffHz: 700, delayMs: 0 },
  [CrossfeedMode.Low]: { strengthPercent: 10, cutoffHz: 700, delayMs: 0.2 },
  [CrossfeedMode.Medium]: { strengthPercent: 20, cutoffHz: 700, delayMs: 0.25 },
  [CrossfeedMode.High]: { strengthPercent: 30, cutoffHz: 700, delayMs: 0.3 },
};

// Preset modes retain the custom settings in the record but do not apply them.
export function crossfeedParameters(config: CrossfeedConfig) {
  return config.mode === CrossfeedMode.Custom ? config : presets[config.mode];
}
