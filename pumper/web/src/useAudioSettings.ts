import { useCallback, useEffect, useRef, useState } from "react";
import { PumperHidTransport } from "./hidTransport";
import {
  type AudioControls,
  type CrossfeedConfig,
  type CrossfeedState,
  crossfeedConfigsEqual,
  decodeAudioControls,
  decodeCrossfeed,
  decodeCrossfeedState,
  encodeCrossfeed,
  Opcode,
  supportsAudioControls,
} from "./protocol";

const previewIntervalMs = 55;

// Keep crossfeed transactions separate from EQ profile state. The transport
// serializes individual reports; this chain also serializes multi-report work.
export function useAudioSettings(
  transport: PumperHidTransport,
  connected: boolean,
  firmwareVersion: string | undefined,
  onError: (message: string) => void,
  onNotice: (message: string) => void,
) {
  const supported = firmwareVersion !== undefined && supportsAudioControls(firmwareVersion);
  const [audio, setAudio] = useState<AudioControls | null>(null);
  const [crossfeed, setCrossfeed] = useState<CrossfeedState | null>(null);
  const [saving, setSaving] = useState(false);
  const session = useRef(0);
  const active = useRef(false);
  const revision = useRef(0);
  const current = useRef<CrossfeedState | null>(null);
  const pending = useRef<CrossfeedConfig | null>(null);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const chain = useRef<Promise<unknown>>(Promise.resolve());
  const savingRef = useRef(false);
  const refreshing = useRef(false);

  const applyState = useCallback((next: CrossfeedState) => {
    current.current = next;
    setCrossfeed(next);
  }, []);

  const enqueue = useCallback((operation: (isCurrent: () => boolean) => Promise<void>) => {
    const token = session.current;
    const isCurrent = () => active.current && session.current === token;
    const result = chain.current.then(async () => {
      if (!isCurrent()) return;
      await operation(isCurrent);
    });
    chain.current = result.catch(() => undefined);
    return result;
  }, []);

  const refresh = useCallback(() => {
    if (!active.current || refreshing.current || pending.current || savingRef.current) return Promise.resolve();
    refreshing.current = true;
    const token = session.current;
    const readRevision = revision.current;
    return enqueue(async (isCurrent) => {
      try {
        const nextAudio = decodeAudioControls((await transport.request(Opcode.GetAudioControls)).payload);
        if (!isCurrent()) return;
        setAudio(nextAudio);
        const nextCrossfeed = decodeCrossfeedState((await transport.request(Opcode.GetCrossfeed)).payload);
        if (isCurrent() && readRevision === revision.current) applyState(nextCrossfeed);
      } catch (reason) {
        if (isCurrent()) onError(`Audio settings: ${reason instanceof Error ? reason.message : "Unable to read device state"}`);
      }
    }).finally(() => {
      if (session.current === token) refreshing.current = false;
    });
  }, [applyState, enqueue, onError, transport]);

  useEffect(() => {
    session.current++;
    active.current = connected && supported;
    revision.current++;
    pending.current = null;
    current.current = null;
    savingRef.current = false;
    refreshing.current = false;
    setAudio(null);
    setCrossfeed(null);
    setSaving(false);
    void refresh();
    return () => {
      active.current = false;
      session.current++;
      if (timer.current !== null) clearTimeout(timer.current);
      timer.current = null;
      pending.current = null;
    };
  }, [connected, supported, refresh]);

  const flush = useCallback(() => {
    if (timer.current !== null) clearTimeout(timer.current);
    timer.current = null;
    const next = pending.current;
    if (!next) return chain.current.then(() => undefined);
    pending.current = null;
    const editRevision = revision.current;
    return enqueue(async (isCurrent) => {
      try {
        const state = decodeCrossfeedState((await transport.request(Opcode.SetCrossfeed, encodeCrossfeed(next))).payload);
        if (isCurrent() && editRevision === revision.current) applyState(state);
      } catch (reason) {
        if (!isCurrent()) return;
        onError(`Crossfeed: ${reason instanceof Error ? reason.message : "Unable to update device"}`);
        try {
          const state = decodeCrossfeedState((await transport.request(Opcode.GetCrossfeed)).payload);
          if (isCurrent() && editRevision === revision.current) applyState(state);
        } catch {
          // Retain the unsaved draft; the next refresh will retry the read.
        }
        throw reason;
      }
    });
  }, [applyState, enqueue, onError, transport]);

  const update = useCallback((patch: Partial<CrossfeedConfig>) => {
    if (!active.current || !current.current || savingRef.current) return;
    try {
      // Normalize values to their wire precision before dirty comparison.
      const live = decodeCrossfeed(encodeCrossfeed({ ...current.current.live, ...patch }));
      revision.current++;
      applyState({ ...current.current, live, dirty: !crossfeedConfigsEqual(live, current.current.saved) });
      pending.current = live;
      if (timer.current === null) timer.current = setTimeout(() => { void flush().catch(() => undefined); }, previewIntervalMs);
    } catch (reason) {
      onError(reason instanceof Error ? reason.message : "Invalid crossfeed settings");
    }
  }, [applyState, flush, onError]);

  const save = useCallback(async () => {
    if (!active.current || !current.current?.dirty || savingRef.current) return;
    const token = session.current;
    savingRef.current = true;
    setSaving(true);
    try {
      // Obtain an acknowledgement for the latest draft before committing,
      // including when an earlier preview failed or is still in flight.
      pending.current = { ...current.current.live };
      await flush();
      if (!active.current || session.current !== token) return;
      await enqueue(async (isCurrent) => {
        const state = decodeCrossfeedState((await transport.request(Opcode.SaveCrossfeed, new Uint8Array(), 8000)).payload);
        if (!isCurrent()) return;
        applyState(state);
        onNotice("Crossfeed saved for power-on");
      });
    } catch (reason) {
      if (active.current && session.current === token) {
        onError(`Save crossfeed: ${reason instanceof Error ? reason.message : "Unable to save device settings"}`);
      }
    } finally {
      if (session.current === token) {
        savingRef.current = false;
        setSaving(false);
      }
    }
  }, [applyState, enqueue, flush, onError, onNotice, transport]);

  return { supported, audio, crossfeed, saving, refresh, update, save };
}
