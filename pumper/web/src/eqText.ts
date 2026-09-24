import { defaultConfig, type EqBand, type EqConfig, FilterType, WidthMode } from "./protocol";

const filterCodes: Record<FilterType, string> = {
  [FilterType.LowShelf]: "LS",
  [FilterType.Peaking]: "PK",
  [FilterType.HighShelf]: "HS",
  [FilterType.LowPass]: "LP",
  [FilterType.HighPass]: "HP",
  [FilterType.Notch]: "NO",
  [FilterType.BandPass]: "BP",
};

const filterTypes: Record<string, FilterType> = {
  LS: FilterType.LowShelf,
  LSC: FilterType.LowShelf,
  PK: FilterType.Peaking,
  PEQ: FilterType.Peaking,
  HS: FilterType.HighShelf,
  HSC: FilterType.HighShelf,
  LP: FilterType.LowPass,
  LPQ: FilterType.LowPass,
  HP: FilterType.HighPass,
  HPQ: FilterType.HighPass,
  NO: FilterType.Notch,
  NOTCH: FilterType.Notch,
  BP: FilterType.BandPass,
};

function numberText(value: number, decimals: number): string {
  return value.toFixed(decimals).replace(/\.0+$|(?<=\.[0-9]*[1-9])0+$/g, "");
}

export function serializeEqText(config: EqConfig): string {
  const lines = [
    "# Pumper Parametric EQ",
    `Equalizer: ${config.enabled ? "ON" : "OFF"}`,
    `Preamp: ${numberText(config.preampDb, 3)} dB`,
  ];
  config.bands.forEach((band, index) => {
    const parts = [
      `Filter ${index + 1}:`,
      band.enabled ? "ON" : "OFF",
      filterCodes[band.type],
      "Fc",
      numberText(band.frequencyHz, 3),
      "Hz",
    ];
    if (band.type <= FilterType.HighShelf) parts.push("Gain", numberText(band.gainDb, 3), "dB");
    if (band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth) {
      parts.push("BW", numberText(band.bandwidthOctaves, 3), "oct");
    } else {
      parts.push("Q", numberText(band.q, 3));
    }
    lines.push(parts.join(" "));
  });
  return `${lines.join("\n")}\n`;
}

function parseNumber(value: string, label: string, minimum: number, maximum: number): number {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed < minimum || parsed > maximum) {
    throw new Error(`${label} must be between ${minimum} and ${maximum}.`);
  }
  return parsed;
}

export function parseEqText(text: string, bandCount = 10): EqConfig {
  const config: EqConfig = {
    enabled: true,
    preampDb: 0,
    bands: defaultConfig.bands.slice(0, bandCount).map((band) => ({ ...band, enabled: false })),
  };
  const seenBands = new Set<number>();
  let recognized = 0;

  text.split(/\r?\n/).forEach((source, lineIndex) => {
    const line = source.trim();
    if (!line || line.startsWith("#") || line.startsWith(";") || /^Pumper Parametric EQ$/i.test(line)) return;

    const equalizer = line.match(/^Equalizer\s*:\s*(ON|OFF)$/i);
    if (equalizer) {
      config.enabled = equalizer[1].toUpperCase() === "ON";
      recognized++;
      return;
    }

    const preamp = line.match(/^Preamp\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*dB$/i);
    if (preamp) {
      config.preampDb = parseNumber(preamp[1], "Preamp", -241, 12);
      recognized++;
      return;
    }

    const filter = line.match(/^Filter\s+(\d+)\s*:\s*(ON|OFF)\s+([A-Z]+)\s+Fc\s+([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*Hz(?:\s+Gain\s+([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*dB)?\s+(Q|BW)\s+([+-]?(?:\d+(?:\.\d*)?|\.\d+))(?:\s+oct)?$/i);
    if (!filter) throw new Error(`Line ${lineIndex + 1} is not a supported EQ setting.`);

    const bandNumber = Number(filter[1]);
    if (!Number.isInteger(bandNumber) || bandNumber < 1 || bandNumber > bandCount) {
      throw new Error(`Line ${lineIndex + 1} uses filter ${bandNumber}; Pumper supports filters 1-${bandCount}.`);
    }
    const bandIndex = bandNumber - 1;
    if (seenBands.has(bandIndex)) throw new Error(`Filter ${bandNumber} is listed more than once.`);
    const type = filterTypes[filter[3].toUpperCase()];
    if (type === undefined) throw new Error(`Line ${lineIndex + 1} uses an unsupported filter type (${filter[3]}).`);
    const usesGain = type <= FilterType.HighShelf;
    if (usesGain && filter[5] === undefined) throw new Error(`Filter ${bandNumber} requires a Gain value.`);
    const widthMode = filter[6].toUpperCase() === "BW" ? WidthMode.Bandwidth : WidthMode.Q;
    if (type !== FilterType.Peaking && widthMode === WidthMode.Bandwidth) {
      throw new Error(`Filter ${bandNumber} only supports Q.`);
    }

    const qMaximum = type === FilterType.LowShelf || type === FilterType.HighShelf ? 1 : 20;
    const width = parseNumber(filter[7], `Filter ${bandNumber} ${widthMode === WidthMode.Q ? "Q" : "bandwidth"}`, 0.1, widthMode === WidthMode.Q ? qMaximum : 4);
    const band: EqBand = {
      ...config.bands[bandIndex],
      enabled: filter[2].toUpperCase() === "ON",
      type,
      frequencyHz: parseNumber(filter[4], `Filter ${bandNumber} frequency`, 20, 20000),
      gainDb: filter[5] === undefined ? 0 : parseNumber(filter[5], `Filter ${bandNumber} gain`, -24, 24),
      widthMode,
      q: widthMode === WidthMode.Q ? width : config.bands[bandIndex].q,
      bandwidthOctaves: widthMode === WidthMode.Bandwidth ? width : config.bands[bandIndex].bandwidthOctaves,
    };
    config.bands[bandIndex] = band;
    seenBands.add(bandIndex);
    recognized++;
  });

  if (recognized === 0 || seenBands.size === 0) throw new Error("Paste at least one supported Filter line.");
  return config;
}
