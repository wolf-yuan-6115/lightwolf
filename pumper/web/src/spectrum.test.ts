import { describe, expect, it } from "vitest";
import { decodeSpectrumChunk, supportsSpectrum, type SpectrumChunk } from "./protocol";
import { SpectrumAssembler, smoothSpectrum, spectrumSettled } from "./spectrum";

function payload(index = 0, sequence = 1, rate = 48000) {
  const count = index === 5 ? 36 : 44;
  const bytes = new Uint8Array(12 + count);
  bytes.set([1, index, 6, count]);
  const view = new DataView(bytes.buffer);
  view.setUint32(4, sequence, true);
  view.setUint32(8, rate, true);
  bytes.fill(180, 12);
  return bytes;
}
function chunk(index: number, sequence = 1): SpectrumChunk {
  return decodeSpectrumChunk(payload(index, sequence));
}
function complete(assembler: SpectrumAssembler, sequence: number, time = 0) {
  let result: Uint8Array | null = null;
  for (let i = 0; i < 6; i++) result = assembler.push(chunk(i, sequence), time);
  return result;
}

describe("output spectrum codec and assembly", () => {
  it("gates firmware 3.5 without affecting older firmware", () => {
    for (const version of ["2.9", "3.4", "invalid"]) expect(supportsSpectrum(version)).toBe(false);
    for (const version of ["3.5", "3.10", "4.0"]) expect(supportsSpectrum(version)).toBe(true);
  });
  it("validates schema, rates, counts, length and levels", () => {
    expect(decodeSpectrumChunk(payload(5)).levels).toHaveLength(36);
    expect(decodeSpectrumChunk(payload(0, 1, 44100)).sampleRateHz).toBe(44100);
    for (const [offset, value] of [[0, 2], [1, 6], [2, 5], [3, 43], [12, 193]]) {
      const bytes = payload(); bytes[offset] = value;
      expect(() => decodeSpectrumChunk(bytes)).toThrow("Invalid spectrum report");
    }
    expect(() => decodeSpectrumChunk(payload(0, 1, 96000))).toThrow();
    expect(() => decodeSpectrumChunk(payload().slice(1))).toThrow();
    expect(() => decodeSpectrumChunk(new Uint8Array())).toThrow();
  });
  it("publishes only a complete frame, including reordered chunks", () => {
    const assembler = new SpectrumAssembler();
    for (const index of [5, 3, 1, 0, 4]) expect(assembler.push(chunk(index), 0)).toBeNull();
    expect(assembler.push(chunk(2), 10)).toEqual(new Uint8Array(256).fill(180));
    expect(assembler.push(chunk(2), 20)).toBeNull();
  });
  it("discards expired, superseded, mixed-rate and conflicting frames", () => {
    const assembler = new SpectrumAssembler();
    assembler.push(chunk(0), 0);
    expect(complete(assembler, 1, 251)).toBeNull();
    assembler.push(chunk(0, 2), 252);
    expect(assembler.push(chunk(1, 1), 252)).toBeNull();
    expect(complete(assembler, 2, 253)).not.toBeNull();
    assembler.push(chunk(0, 3), 260);
    expect(assembler.push({ ...chunk(1, 3), sampleRateHz: 44100 }, 260)).toBeNull();
    const duplicate = chunk(0, 3); duplicate.levels[0] = 1;
    assembler.push(duplicate, 261);
    expect(complete(assembler, 3, 262)).toBeNull();
  });
  it("handles sequence wrap and a new session with reset sequence", () => {
    const assembler = new SpectrumAssembler();
    expect(complete(assembler, 0xffffffff)).not.toBeNull();
    expect(complete(assembler, 0, 1)).not.toBeNull();
    expect(complete(assembler, 0xffffffff, 2)).toBeNull();
    expect(complete(new SpectrumAssembler(), 1)).not.toBeNull();
  });
  it("animates dB height with fast attack and short release, independent of frame rate", () => {
    const loud = new Uint8Array(256).fill(192);
    const quiet = new Uint8Array(256);
    const zero = new Float32Array(256);
    const attack = smoothSpectrum(zero, loud, 16);
    expect(10 * Math.log10(attack[0])).toBeCloseTo(-96 * Math.exp(-2));
    const release = smoothSpectrum(new Float32Array(256).fill(1), quiet, 70);
    expect(10 * Math.log10(release[0])).toBeCloseTo(-96 * (1 - Math.exp(-1)));
    const twoSteps = smoothSpectrum(smoothSpectrum(zero, loud, 8), loud, 8);
    expect(10 * Math.log10(twoSteps[0])).toBeCloseTo(10 * Math.log10(attack[0]), 4);
    const after300Ms = smoothSpectrum(new Float32Array(256).fill(1), quiet, 300);
    expect(10 * Math.log10(after300Ms[0])).toBeLessThan(-91);
    expect(spectrumSettled(smoothSpectrum(zero, loud, 200), loud)).toBe(true);
    expect(spectrumSettled(smoothSpectrum(new Float32Array(256).fill(1), quiet, 1000), quiet)).toBe(true);
  });
});
