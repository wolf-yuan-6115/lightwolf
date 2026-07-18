import { EqBand, EqConfig, FilterType, WidthMode } from "./protocol";

interface Coefficients {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

export interface ResponsePoint {
  frequencyHz: number;
  gainDb: number;
}

const identity: Coefficients = { b0: 1, b1: 0, b2: 0, a1: 0, a2: 0 };

export function buildCoefficients(band: EqBand, sampleRateHz: number): Coefficients {
  if (!band.enabled || Math.abs(band.gainDb) < 0.0001 || band.frequencyHz >= sampleRateHz / 2) return identity;
  const w0 = (2 * Math.PI * band.frequencyHz) / sampleRateHz;
  const sinW0 = Math.sin(w0);
  const cosW0 = Math.cos(w0);
  const a = 10 ** (band.gainDb / 40);
  let alpha: number;
  let b0: number;
  let b1: number;
  let b2: number;
  let a0: number;
  let a1: number;
  let a2: number;

  if (band.type === FilterType.Peaking) {
    alpha =
      band.widthMode === WidthMode.Bandwidth
        ? sinW0 * Math.sinh((Math.log(2) / 2) * band.bandwidthOctaves * (w0 / sinW0))
        : sinW0 / (2 * band.q);
    b0 = 1 + alpha * a;
    b1 = -2 * cosW0;
    b2 = 1 - alpha * a;
    a0 = 1 + alpha / a;
    a1 = -2 * cosW0;
    a2 = 1 - alpha / a;
  } else {
    alpha = (sinW0 / 2) * Math.sqrt((a + 1 / a) * (1 / band.q - 1) + 2);
    const term = 2 * Math.sqrt(a) * alpha;
    if (band.type === FilterType.LowShelf) {
      b0 = a * (a + 1 - (a - 1) * cosW0 + term);
      b1 = 2 * a * (a - 1 - (a + 1) * cosW0);
      b2 = a * (a + 1 - (a - 1) * cosW0 - term);
      a0 = a + 1 + (a - 1) * cosW0 + term;
      a1 = -2 * (a - 1 + (a + 1) * cosW0);
      a2 = a + 1 + (a - 1) * cosW0 - term;
    } else {
      b0 = a * (a + 1 + (a - 1) * cosW0 + term);
      b1 = -2 * a * (a - 1 + (a + 1) * cosW0);
      b2 = a * (a + 1 + (a - 1) * cosW0 - term);
      a0 = a + 1 - (a - 1) * cosW0 + term;
      a1 = 2 * (a - 1 - (a + 1) * cosW0);
      a2 = a + 1 - (a - 1) * cosW0 - term;
    }
  }
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

function coefficientGainDb(coefficients: Coefficients, frequencyHz: number, sampleRateHz: number): number {
  const w = (2 * Math.PI * frequencyHz) / sampleRateHz;
  const cos1 = Math.cos(w);
  const sin1 = -Math.sin(w);
  const cos2 = Math.cos(2 * w);
  const sin2 = -Math.sin(2 * w);
  const nr = coefficients.b0 + coefficients.b1 * cos1 + coefficients.b2 * cos2;
  const ni = coefficients.b1 * sin1 + coefficients.b2 * sin2;
  const dr = 1 + coefficients.a1 * cos1 + coefficients.a2 * cos2;
  const di = coefficients.a1 * sin1 + coefficients.a2 * sin2;
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

function buildChain(config: EqConfig, sampleRateHz: number): Coefficients[] {
  return config.bands.map((band) => buildCoefficients(band, sampleRateHz));
}

function chainGainDb(chain: Coefficients[], frequencyHz: number, sampleRateHz: number): number {
  return chain.reduce(
    (gain, coefficients) => gain + coefficientGainDb(coefficients, frequencyHz, sampleRateHz),
    0,
  );
}

export function compositeGainDb(config: EqConfig, sampleRateHz: number, frequencyHz: number): number {
  if (!config.enabled) return 0;
  return config.bands.reduce(
    (gain, band) => gain + coefficientGainDb(buildCoefficients(band, sampleRateHz), frequencyHz, sampleRateHz),
    0,
  );
}

function logFrequency(index: number, count: number): number {
  return 20 * (1000 ** (index / (count - 1)));
}

export function responseCurve(config: EqConfig, sampleRateHz: number, count = 512): ResponsePoint[] {
  const chain = buildChain(config, sampleRateHz);
  return Array.from({ length: count }, (_, index) => {
    const frequencyHz = logFrequency(index, count);
    const filterGain = config.enabled ? chainGainDb(chain, frequencyHz, sampleRateHz) : 0;
    return { frequencyHz, gainDb: config.enabled ? filterGain + config.preampDb : 0 };
  });
}

export function calculateAutoPreamp(config: EqConfig, sampleRateHz: number): { preampDb: number; peakDb: number } {
  if (!config.enabled) return { preampDb: -0.5, peakDb: 0 };
  const chain = buildChain(config, sampleRateHz);
  const count = 4096;
  const frequencies = Array.from({ length: count }, (_, index) => logFrequency(index, count));
  config.bands.filter((band) => band.enabled).forEach((band) => frequencies.push(band.frequencyHz));
  frequencies.sort((a, b) => a - b);

  let peakDb = -Infinity;
  let peakIndex = 0;
  frequencies.forEach((frequency, index) => {
    const gain = chainGainDb(chain, frequency, sampleRateHz);
    if (gain > peakDb) {
      peakDb = gain;
      peakIndex = index;
    }
  });

  const lower = frequencies[Math.max(0, peakIndex - 1)];
  const upper = frequencies[Math.min(frequencies.length - 1, peakIndex + 1)];
  for (let i = 0; i <= 256; i++) {
    const frequency = lower * (upper / lower) ** (i / 256);
    peakDb = Math.max(peakDb, chainGainDb(chain, frequency, sampleRateHz));
  }
  const preampDb = Math.round(Math.min(0, -0.5 - peakDb) * 1000) / 1000;
  return { preampDb, peakDb };
}
