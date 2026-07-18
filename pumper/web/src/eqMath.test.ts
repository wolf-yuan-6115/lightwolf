import { describe, expect, it } from "vitest";
import { calculateAutoPreamp, compositeGainDb, responseCurve } from "./eqMath";
import { defaultConfig, EqConfig, FilterType, WidthMode } from "./protocol";

function flatConfig(): EqConfig {
  return {
    enabled: true,
    preampDb: 0,
    bands: defaultConfig.bands.map((band) => ({ ...band, enabled: false })),
  };
}

describe("EQ response math", () => {
  it("keeps a flat response at zero decibels", () => {
    const config = flatConfig();
    expect(compositeGainDb(config, 48000, 1000)).toBeCloseTo(0, 8);
    expect(responseCurve(config, 48000, 8).every((point) => Math.abs(point.gainDb) < 1e-8)).toBe(true);
  });

  it("reserves 0.5 dB for a flat auto preamp", () => {
    expect(calculateAutoPreamp(flatConfig(), 48000)).toEqual({ preampDb: -0.5, peakDb: 0 });
  });

  it("accounts for overlapping filter gains", () => {
    const config = flatConfig();
    config.bands[0] = {
      enabled: true,
      type: FilterType.Peaking,
      widthMode: WidthMode.Q,
      frequencyHz: 1000,
      gainDb: 6,
      q: 2,
      bandwidthOctaves: 1,
    };
    config.bands[1] = { ...config.bands[0] };
    const result = calculateAutoPreamp(config, 48000);
    expect(result.peakDb).toBeCloseTo(12, 2);
    expect(result.preampDb).toBeCloseTo(-12.5, 2);
  });

  it("does not add positive gain when every enabled filter cuts", () => {
    const config = flatConfig();
    config.bands[0] = { ...defaultConfig.bands[0], gainDb: -6 };
    expect(calculateAutoPreamp(config, 48000).preampDb).toBeLessThanOrEqual(0);
  });
});
