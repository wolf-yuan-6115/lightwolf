import type { SpectrumChunk } from "./protocol";

export class SpectrumAssembler {
  private sequence: number | null = null;
  private started = 0;
  private rate = 0;
  private mask = 0;
  private levels = new Uint8Array(256);

  push(chunk: SpectrumChunk, now: number): Uint8Array | null {
    if (this.sequence !== null && chunk.sequence !== this.sequence) {
      const delta = (chunk.sequence - this.sequence) >>> 0;
      if (delta >= 0x80000000) return null;
    }
    if (this.sequence !== chunk.sequence) {
      this.sequence = chunk.sequence;
      this.started = now;
      this.rate = chunk.sampleRateHz;
      this.mask = 0;
    }
    if (now - this.started > 250 || chunk.sampleRateHz !== this.rate || this.mask === 63) return null;
    // A conflicting duplicate invalidates this frame instead of mixing data.
    const offset = chunk.index * 44;
    if (this.mask & (1 << chunk.index)) {
      if (chunk.levels.some((value, i) => value !== this.levels[offset + i])) this.started = -Infinity;
      return null;
    }
    this.levels.set(chunk.levels, offset);
    this.mask |= 1 << chunk.index;
    return this.mask === 63 ? this.levels.slice() : null;
  }
}

// Animate the plotted dB height, rather than linear power. Power-space decay
// takes seconds to traverse a large dB range, even with a short time constant.
const spectrumFloorDb = -96;
export const spectrumAttackMs = 20;
export const spectrumReleaseMs = 100;
const spectrumSnapDb = 0.05;

function spectrumDb(power: number): number {
  return power > 0 ? Math.max(spectrumFloorDb, 10 * Math.log10(power)) : spectrumFloorDb;
}

export function smoothSpectrum(previous: Float32Array | null, levels: Uint8Array, elapsedMs: number): Float32Array {
  const result = new Float32Array(256);
  for (let i = 0; i < 256; i++) {
    const targetDb = levels[i] / 2 + spectrumFloorDb;
    const oldDb = spectrumDb(previous?.[i] ?? 0);
    const alpha = 1 - Math.exp(-Math.max(0, elapsedMs) / (targetDb > oldDb ? spectrumAttackMs : spectrumReleaseMs));
    const interpolated = oldDb + (targetDb - oldDb) * alpha;
    const db = Math.abs(targetDb - interpolated) < spectrumSnapDb ? targetDb : interpolated;
    result[i] = db <= spectrumFloorDb ? 0 : 10 ** (db / 10);
  }
  return result;
}

export function spectrumSettled(power: Float32Array, levels: Uint8Array): boolean {
  return power.every((value, i) => Math.abs(spectrumDb(value) - (levels[i] / 2 + spectrumFloorDb)) < spectrumSnapDb);
}
