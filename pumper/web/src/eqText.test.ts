import { describe, expect, it } from "vitest";
import { parseEqText, serializeEqText } from "./eqText";
import { defaultConfig, FilterType, WidthMode } from "./protocol";

describe("parametric EQ text", () => {
  it("round-trips every Pumper filter field", () => {
    const config = {
      ...defaultConfig,
      enabled: false,
      preampDb: -4.25,
      bands: defaultConfig.bands.map((band, index) => ({
        ...band,
        enabled: index !== 8,
        type: index === 0 ? FilterType.LowPass : band.type,
        widthMode: index === 0 || index === 2 ? WidthMode.Q : band.widthMode,
        q: index === 0 ? 1.25 : band.q,
      })),
    };
    expect(parseEqText(serializeEqText(config))).toEqual(config);
  });

  it("imports common aliases and disables omitted filters", () => {
    const config = parseEqText("Preamp: -6 dB\nFilter 1: ON PEQ Fc 100 Hz Gain 3 dB Q 1.4\nFilter 3: OFF HSC Fc 8000 Hz Gain -2 dB Q 0.7");
    expect(config.preampDb).toBe(-6);
    expect(config.bands[0]).toMatchObject({ enabled: true, type: FilterType.Peaking, frequencyHz: 100, gainDb: 3, q: 1.4 });
    expect(config.bands[1].enabled).toBe(false);
    expect(config.bands[2]).toMatchObject({ enabled: false, type: FilterType.HighShelf });
  });

  it("reports malformed and out-of-range lines", () => {
    expect(() => parseEqText("Filter 1: ON PK Fc 10 Hz Gain 0 dB Q 1")).toThrow("Filter 1 frequency");
    expect(() => parseEqText("Filter 1: ON PK Fc 100 Hz Gain 0 dB Q 1\nwat")).toThrow("Line 2");
    expect(() => parseEqText("Preamp: 0 dB")).toThrow("Filter line");
  });
});
