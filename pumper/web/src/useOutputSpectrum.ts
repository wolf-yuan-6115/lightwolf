import { useEffect, useState } from "react";
import { PumperHidTransport, SPECTRUM_REPORT_EVENT, SPECTRUM_STARTED_EVENT } from "./hidTransport";
import { decodeSpectrumChunk, type ResponsePacket } from "./protocol";
import { SpectrumAssembler, smoothSpectrum, spectrumSettled } from "./spectrum";

export function useOutputSpectrum(transport: PumperHidTransport | null, enabled: boolean): Float32Array | null {
  const [power, setPower] = useState<Float32Array | null>(null);
  useEffect(() => {
    setPower(null);
    if (!transport || !enabled) return;
    let alive = true;
    let connected = true;
    let assembler = new SpectrumAssembler();
    let previous: Float32Array | null = null;
    let target: Uint8Array | null = null;
    let updated: number | null = null;
    let animatedAt = 0;
    let animationFrame: number | null = null;
    const clear = (resetAssembler = true) => {
      if (resetAssembler) assembler = new SpectrumAssembler();
      previous = null;
      target = null;
      updated = null;
      if (animationFrame !== null) window.cancelAnimationFrame(animationFrame);
      animationFrame = null;
      if (alive) setPower(null);
    };
    const animate = () => {
      animationFrame = null;
      if (!alive || !connected || document.hidden || !target || updated === null) return;
      const now = performance.now();
      if (now - updated >= 500) { clear(false); return; }
      previous = smoothSpectrum(previous, target, Math.min(100, Math.max(0, now - animatedAt)));
      animatedAt = now;
      setPower(previous);
      if (!spectrumSettled(previous, target)) animationFrame = window.requestAnimationFrame(animate);
    };
    const report = (event: Event) => {
      if (!alive || !connected || document.hidden) return;
      try {
        const response = (event as CustomEvent<ResponsePacket>).detail;
        const now = performance.now();
        const levels = assembler.push(decodeSpectrumChunk(response.payload), now);
        if (!levels) return;
        target = levels;
        updated = now;
        // Retarget the existing animation immediately; never queue visual frames.
        if (animationFrame === null) {
          animatedAt = now;
          animationFrame = window.requestAnimationFrame(animate);
        }
      } catch { /* Malformed visual telemetry must not affect control requests. */ }
    };
    const started = () => clear();
    const visibility = () => clear();
    const disconnected = () => { connected = false; clear(); };
    transport.addEventListener(SPECTRUM_REPORT_EVENT, report);
    transport.addEventListener(SPECTRUM_STARTED_EVENT, started);
    transport.addEventListener("disconnect", disconnected);
    document.addEventListener("visibilitychange", visibility);
    const stale = window.setInterval(() => {
      if (updated !== null && performance.now() - updated >= 500) clear(false);
    }, 100);
    return () => {
      alive = false;
      clear();
      transport.removeEventListener(SPECTRUM_REPORT_EVENT, report);
      transport.removeEventListener(SPECTRUM_STARTED_EVENT, started);
      transport.removeEventListener("disconnect", disconnected);
      document.removeEventListener("visibilitychange", visibility);
      window.clearInterval(stale);
    };
  }, [transport, enabled]);
  return enabled ? power : null;
}
