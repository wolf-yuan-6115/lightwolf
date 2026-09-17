import { act, cleanup, renderHook, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { PumperHidTransport } from "./hidTransport";
import { CrossfeedMode, defaultCrossfeed, Opcode, type ResponsePacket } from "./protocol";
import { audioSettingsMock, crossfeedPayload } from "./test/audioSettingsMock";
import { useAudioSettings } from "./useAudioSettings";

afterEach(() => { cleanup(); vi.useRealTimers(); });

async function setup() {
  const mock = audioSettingsMock();
  const onError = vi.fn();
  const onNotice = vi.fn();
  const transport = { request: mock.handle } as unknown as PumperHidTransport;
  const hook = renderHook(({ connected }) => useAudioSettings(transport, connected, "2.2", onError, onNotice), { initialProps: { connected: true } });
  await waitFor(() => expect(hook.result.current.crossfeed).not.toBeNull());
  mock.handle.mockClear();
  return { ...hook, mock, onError, onNotice };
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: Error) => void;
  const promise = new Promise<T>((res, rej) => { resolve = res; reject = rej; });
  return { promise, resolve, reject };
}

describe("independent audio settings", () => {
  it("coalesces previews and compares reverted settings with saved state", async () => {
    const { result, mock } = await setup();
    vi.useFakeTimers();
    act(() => {
      result.current.update({ mode: CrossfeedMode.Custom });
      result.current.update({ strengthPercent: 30 });
      result.current.update({ strengthPercent: 35 });
    });
    await act(() => vi.advanceTimersByTimeAsync(54));
    expect(mock.handle).not.toHaveBeenCalled();
    await act(() => vi.advanceTimersByTimeAsync(1));
    expect(mock.handle).toHaveBeenCalledOnce();
    expect(mock.state.live.strengthPercent).toBe(35);
    expect(result.current.crossfeed?.dirty).toBe(true);
    act(() => result.current.update({ ...defaultCrossfeed }));
    expect(result.current.crossfeed?.dirty).toBe(false);
    await act(() => vi.advanceTimersByTimeAsync(55));
    expect(mock.state.dirty).toBe(false);
  });

  it("does not overwrite a newer draft with an older polling response", async () => {
    const { result, mock } = await setup();
    const old = await mock.handle(Opcode.GetCrossfeed);
    mock.handle.mockClear();
    const poll = deferred<ResponsePacket>();
    mock.handle.mockImplementationOnce(() => poll.promise);
    let refresh!: Promise<void>;
    act(() => { refresh = result.current.refresh(); });
    await act(async () => { await Promise.resolve(); });
    act(() => result.current.update({ mode: CrossfeedMode.High }));
    await act(async () => { poll.resolve({ ...old, payload: new Uint8Array(9) }); await refresh; });
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.High);
    expect(result.current.crossfeed?.dirty).toBe(true);
  });

  it("keeps edits made while a preview acknowledgement is pending", async () => {
    const { result, mock } = await setup();
    vi.useFakeTimers();
    const acknowledgement = deferred<ResponsePacket>();
    mock.handle.mockImplementationOnce(() => acknowledgement.promise);
    act(() => result.current.update({ mode: CrossfeedMode.Low }));
    await act(() => vi.advanceTimersByTimeAsync(55));
    act(() => result.current.update({ mode: CrossfeedMode.High }));
    await act(async () => {
      acknowledgement.resolve({ opcode: Opcode.SetCrossfeed | 0x80, requestId: 1, status: 0, payload: crossfeedPayload({ live: { ...defaultCrossfeed, mode: CrossfeedMode.Low }, saved: defaultCrossfeed, dirty: true }) });
      await Promise.resolve();
    });
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.High);
    await act(() => vi.advanceTimersByTimeAsync(55));
    expect(mock.state.live.mode).toBe(CrossfeedMode.High);
  });

  it("re-reads device state after a rejected update", async () => {
    const { result, mock, onError } = await setup();
    vi.useFakeTimers();
    mock.handle.mockRejectedValueOnce(new Error("Invalid settings"));
    act(() => result.current.update({ mode: CrossfeedMode.High }));
    await act(() => vi.advanceTimersByTimeAsync(55));
    expect(onError).toHaveBeenCalledWith("Crossfeed: Invalid settings");
    expect(mock.handle.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.SetCrossfeed, Opcode.GetCrossfeed]);
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.Off);
  });

  it("flushes the latest preview before saving and prevents duplicate saves", async () => {
    const { result, mock, onNotice } = await setup();
    act(() => result.current.update({ mode: CrossfeedMode.Custom, strengthPercent: 40 }));
    await act(async () => {
      const first = result.current.save();
      const second = result.current.save();
      await Promise.all([first, second]);
    });
    expect(mock.handle.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.SetCrossfeed, Opcode.SaveCrossfeed]);
    expect(mock.state.saved.strengthPercent).toBe(40);
    expect(result.current.crossfeed?.dirty).toBe(false);
    expect(onNotice).toHaveBeenCalledOnce();
  });

  it("refreshes externally changed host controls and crossfeed", async () => {
    const { result, mock } = await setup();
    mock.audio[6] = 1;
    mock.state = { live: { ...defaultCrossfeed, mode: CrossfeedMode.Low }, saved: defaultCrossfeed, dirty: true };
    await act(() => result.current.refresh());
    expect(result.current.audio?.master.muted).toBe(true);
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.Low);
  });

  it("cancels unsent edits on disconnect and reads state on reconnect", async () => {
    const { result, mock, rerender } = await setup();
    vi.useFakeTimers();
    act(() => result.current.update({ mode: CrossfeedMode.High }));
    rerender({ connected: false });
    await act(() => vi.advanceTimersByTimeAsync(55));
    expect(mock.handle).not.toHaveBeenCalled();
    expect(result.current.audio).toBeNull();
    expect(result.current.crossfeed).toBeNull();
    await act(async () => { rerender({ connected: true }); });
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.Off);
    expect(mock.handle.mock.calls.map(([opcode]) => opcode)).toEqual([Opcode.GetAudioControls, Opcode.GetCrossfeed]);
  });

  it("does not commit an old session's save after reconnecting", async () => {
    const { result, mock, rerender } = await setup();
    const acknowledgement = deferred<ResponsePacket>();
    mock.handle.mockImplementationOnce(() => acknowledgement.promise);
    act(() => result.current.update({ mode: CrossfeedMode.High }));
    let save!: Promise<void>;
    act(() => { save = result.current.save(); });
    await act(async () => { await Promise.resolve(); });
    rerender({ connected: false });
    rerender({ connected: true });
    await act(async () => { acknowledgement.resolve(await mock.handle(Opcode.GetCrossfeed)); await save; });
    expect(mock.handle.mock.calls.some(([opcode]) => opcode === Opcode.SaveCrossfeed)).toBe(false);
    expect(result.current.crossfeed?.live.mode).toBe(CrossfeedMode.Off);
  });
});
