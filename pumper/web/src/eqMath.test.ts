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

  it("keeps a flat auto preamp at zero decibels", () => {
    expect(calculateAutoPreamp(flatConfig(), 48000)).toEqual({ preampDb: 0, peakDb: 0 });
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
    expect(result.preampDb).toBeCloseTo(-12, 2);
  });

  it("does not add positive gain when every enabled filter cuts", () => {
    const config = flatConfig();
    config.bands[0] = { ...defaultConfig.bands[0], gainDb: -6 };
    expect(calculateAutoPreamp(config, 48000).preampDb).toBeLessThanOrEqual(0);
  });

  it("matches the firmware response for gain-independent filters at every supported rate", () => {
    const rates = [44100, 48000, 88200, 96000, 176400, 192000];
    for (const sampleRate of rates) {
      const config = flatConfig();
      const base = { ...defaultConfig.bands[0], enabled: true, widthMode: WidthMode.Q, frequencyHz: 1000, gainDb: 0, q: 0.707 };

      config.bands[0] = { ...base, type: FilterType.LowPass };
      expect(compositeGainDb(config, sampleRate, 100)).toBeGreaterThan(-0.1);
      expect(compositeGainDb(config, sampleRate, 10000)).toBeLessThan(-30);

      config.bands[0] = { ...base, type: FilterType.HighPass };
      expect(compositeGainDb(config, sampleRate, 100)).toBeLessThan(-30);
      expect(compositeGainDb(config, sampleRate, 10000)).toBeGreaterThan(-0.1);

      config.bands[0] = { ...base, type: FilterType.Notch };
      expect(compositeGainDb(config, sampleRate, 1000)).toBeLessThan(-100);

      config.bands[0] = { ...base, type: FilterType.BandPass };
      expect(compositeGainDb(config, sampleRate, 1000)).toBeCloseTo(0, 6);
      expect(responseCurve(config, sampleRate, 128).every((point) => Number.isFinite(point.gainDb))).toBe(true);
    }
  });

  it("treats filters at or above Nyquist as neutral", () => {
    const config = flatConfig();
    config.bands[0] = { ...defaultConfig.bands[0], enabled: true, type: FilterType.LowPass, widthMode: WidthMode.Q, frequencyHz: 20000, gainDb: 0, q: 0.707 };
    expect(compositeGainDb(config, 32000, 1000)).toBe(0);
  });
});
