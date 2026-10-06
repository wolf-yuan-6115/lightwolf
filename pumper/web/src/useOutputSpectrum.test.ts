import { act, cleanup, renderHook } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { PumperHidTransport, SPECTRUM_REPORT_EVENT, SPECTRUM_STARTED_EVENT } from "./hidTransport";
import { useOutputSpectrum } from "./useOutputSpectrum";

let hidden = false;
beforeEach(() => {
  hidden = false;
  vi.useFakeTimers();
  vi.spyOn(performance, "now").mockImplementation(() => Date.now());
  vi.spyOn(document, "hidden", "get").mockImplementation(() => hidden);
});
afterEach(() => { cleanup(); vi.useRealTimers(); vi.restoreAllMocks(); });
function makeTransport() {
  const transport = new EventTarget() as PumperHidTransport;
  transport.request = vi.fn().mockResolvedValue({});
  return transport;
}
async function flush() { await act(async () => { await Promise.resolve(); }); }
function emit(transport: PumperHidTransport, sequence = 1, indices = [0, 1, 2, 3, 4, 5], level = 180) {
  act(() => {
    for (const i of indices) {
      const count = i === 5 ? 36 : 44;
      const payload = new Uint8Array(12 + count);
      payload.set([1, i, 6, count]);
      const view = new DataView(payload.buffer);
      view.setUint32(4, sequence, true); view.setUint32(8, 48000, true);
      payload.fill(level, 12);
      transport.dispatchEvent(new CustomEvent(SPECTRUM_REPORT_EVENT, { detail: { payload } }));
    }
  });
}
function animate(milliseconds = 16) { act(() => { vi.advanceTimersByTime(milliseconds); }); }
it("renders complete frames and clears stale output without managing subscriptions", async () => {
  const transport = makeTransport();
  const { result, unmount } = renderHook(() => useOutputSpectrum(transport, true));
  await flush();
  expect(transport.request).not.toHaveBeenCalled();
  emit(transport);
  expect(result.current).toBeNull();
  animate();
  expect(result.current?.[0]).toBeGreaterThan(0);
  await act(async () => { vi.advanceTimersByTime(400); });
  expect(transport.request).not.toHaveBeenCalled();
  act(() => { vi.advanceTimersByTime(100); });
  expect(result.current).toBeNull();
  unmount();
  expect(transport.request).not.toHaveBeenCalled();
});
it("pauses animation while hidden and clears partial frames on return", async () => {
  const transport = makeTransport();
  const { result } = renderHook(() => useOutputSpectrum(transport, true));
  await flush();
  emit(transport, 90, [0, 1, 2]);
  act(() => { hidden = true; document.dispatchEvent(new Event("visibilitychange")); });
  expect(transport.request).not.toHaveBeenCalled();
  emit(transport, 90, [3, 4, 5]);
  expect(result.current).toBeNull();
  act(() => { hidden = false; document.dispatchEvent(new Event("visibilitychange")); });
  await flush();
  emit(transport, 1);
  animate();
  expect(result.current).not.toBeNull();
});
it("discards old device data across disconnect, reconnect and unsupported firmware", async () => {
  const first = makeTransport(), next = makeTransport();
  const { result, rerender } = renderHook(({ transport, enabled }) => useOutputSpectrum(transport, enabled),
    { initialProps: { transport: first, enabled: true } });
  await flush();
  emit(first, 20, [0, 1, 2]);
  act(() => { first.dispatchEvent(new Event("disconnect")); });
  emit(first, 20, [3, 4, 5]);
  expect(result.current).toBeNull();
  rerender({ transport: next, enabled: true });
  await flush();
  emit(first); expect(result.current).toBeNull();
  emit(next); animate(); expect(result.current).not.toBeNull();
  rerender({ transport: next, enabled: false });
  expect(result.current).toBeNull();
  expect(next.request).not.toHaveBeenCalled();
});
it("ignores malformed telemetry and accepts reports without sending control requests", () => {
  const transport = makeTransport();
  const { result } = renderHook(() => useOutputSpectrum(transport, true));
  act(() => { transport.dispatchEvent(new CustomEvent(SPECTRUM_REPORT_EVENT, { detail: { payload: new Uint8Array() } })); });
  expect(result.current).toBeNull();
  emit(transport); animate(); expect(result.current).not.toBeNull();
  expect(transport.request).not.toHaveBeenCalled();
});

it("animates between reports and immediately follows the newest target", async () => {
  const transport = makeTransport();
  const { result } = renderHook(() => useOutputSpectrum(transport, true));
  await flush();
  emit(transport, 1, undefined, 192);
  animate();
  const first = result.current![0];
  animate(); // No additional report: the browser still advances the animation.
  const second = result.current![0];
  expect(second).toBeGreaterThan(first);
  emit(transport, 2, undefined, 0);
  animate();
  expect(result.current![0]).toBeLessThan(second);
  animate(280);
  expect(10 * Math.log10(result.current![0])).toBeLessThan(-91);
});

it("cancels pending animation on hiding, disabling and unmount", async () => {
  const transport = makeTransport();
  const cancel = vi.spyOn(window, "cancelAnimationFrame");
  const { result, rerender, unmount } = renderHook(({ enabled }) => useOutputSpectrum(transport, enabled),
    { initialProps: { enabled: true } });
  await flush();
  emit(transport);
  act(() => { hidden = true; document.dispatchEvent(new Event("visibilitychange")); });
  expect(cancel).toHaveBeenCalledTimes(1);
  animate(64);
  expect(result.current).toBeNull();
  act(() => { hidden = false; document.dispatchEvent(new Event("visibilitychange")); });
  await flush();
  emit(transport);
  rerender({ enabled: false });
  expect(cancel).toHaveBeenCalledTimes(2);
  animate(64);
  expect(result.current).toBeNull();
  rerender({ enabled: true });
  await flush();
  emit(transport);
  unmount();
  expect(cancel).toHaveBeenCalledTimes(3);
});

it("expires animation when the first report arrives at time zero", async () => {
  vi.setSystemTime(0);
  const transport = makeTransport();
  const { result } = renderHook(() => useOutputSpectrum(transport, true));
  await flush();
  emit(transport);
  animate(32);
  expect(result.current).not.toBeNull();
  animate(500);
  expect(result.current).toBeNull();
  emit(transport); // A replay of the expired frame cannot revive the graph.
  animate();
  expect(result.current).toBeNull();
});

// Expiry recovery restarts the firmware sequence at one without reconnecting.
it("recovers after an acknowledged subscription restart resets frame sequences", () => {
  const transport = makeTransport();
  const { result } = renderHook(() => useOutputSpectrum(transport, true));
  emit(transport, 5000);
  animate();
  expect(result.current).not.toBeNull();
  animate(500);
  expect(result.current).toBeNull();
  emit(transport, 1);
  animate();
  expect(result.current).toBeNull(); // Old/replayed sequences remain rejected.
  act(() => { transport.dispatchEvent(new Event(SPECTRUM_STARTED_EVENT)); });
  emit(transport, 1);
  animate();
  expect(result.current).not.toBeNull();
});

it("discards partial chunks and pending animation from the previous subscription", () => {
  const transport = makeTransport();
  const cancel = vi.spyOn(window, "cancelAnimationFrame");
  const { result, unmount } = renderHook(() => useOutputSpectrum(transport, true));
  emit(transport, 100);
  expect(result.current).toBeNull();
  emit(transport, 101, [0, 1, 2]);
  act(() => { transport.dispatchEvent(new Event(SPECTRUM_STARTED_EVENT)); });
  expect(cancel).toHaveBeenCalledOnce();
  emit(transport, 1, [3, 4, 5]);
  animate();
  expect(result.current).toBeNull();
  emit(transport, 1, [0, 1, 2]);
  animate();
  expect(result.current).not.toBeNull();
  unmount();
  act(() => { transport.dispatchEvent(new Event(SPECTRUM_STARTED_EVENT)); });
  expect(transport.request).not.toHaveBeenCalled();
});
