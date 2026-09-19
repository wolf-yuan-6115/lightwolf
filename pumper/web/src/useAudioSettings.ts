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
  decodeOutputProcessing,
  decodeOutputProcessingState,
  encodeCrossfeed,
  encodeOutputProcessing,
  Opcode,
  type OutputProcessingConfig,
  type OutputProcessingState,
  outputProcessingConfigsEqual,
  supportsAudioControls,
  supportsFirmware3Controls,
} from "./protocol";

const previewIntervalMs = 55;

export function useAudioSettings(
  transport: PumperHidTransport,
  connected: boolean,
  firmwareVersion: string | undefined,
  onError: (message: string) => void,
  onNotice: (message: string) => void,
) {
  const supported = firmwareVersion !== undefined && supportsAudioControls(firmwareVersion);
  const outputSupported = firmwareVersion !== undefined && supportsFirmware3Controls(firmwareVersion);
  const [audio, setAudio] = useState<AudioControls | null>(null);
  const [crossfeed, setCrossfeed] = useState<CrossfeedState | null>(null);
  const [outputProcessing, setOutputProcessing] = useState<OutputProcessingState | null>(null);
  const [savingCrossfeed, setSavingCrossfeed] = useState(false);
  const [savingOutput, setSavingOutput] = useState(false);
  const session = useRef(0);
  const active = useRef(false);
  const outputActive = useRef(false);
  const crossfeedRevision = useRef(0);
  const outputRevision = useRef(0);
  const currentCrossfeed = useRef<CrossfeedState | null>(null);
  const currentOutput = useRef<OutputProcessingState | null>(null);
  const pendingCrossfeed = useRef<CrossfeedConfig | null>(null);
  const pendingOutput = useRef<OutputProcessingConfig | null>(null);
  const crossfeedTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const outputTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const chain = useRef<Promise<unknown>>(Promise.resolve());
  const savingCrossfeedRef = useRef(false);
  const savingOutputRef = useRef(false);
  const refreshing = useRef(false);

  const applyCrossfeedState = useCallback((next: CrossfeedState) => {
    currentCrossfeed.current = next;
    setCrossfeed(next);
  }, []);

  const applyOutputState = useCallback((next: OutputProcessingState) => {
    currentOutput.current = next;
    setOutputProcessing(next);
  }, []);

  const enqueue = useCallback((operation: (isCurrent: () => boolean) => Promise<void>) => {
    const token = session.current;
    const isCurrent = () => active.current && session.current === token;
    const result = chain.current.then(async () => {
      if (isCurrent()) await operation(isCurrent);
    });
    chain.current = result.catch(() => undefined);
    return result;
  }, []);

  const refresh = useCallback(() => {
    if (!active.current || refreshing.current || pendingCrossfeed.current || pendingOutput.current ||
        savingCrossfeedRef.current || savingOutputRef.current) return Promise.resolve();
    refreshing.current = true;
    const token = session.current;
    const crossfeedReadRevision = crossfeedRevision.current;
    const outputReadRevision = outputRevision.current;
    return enqueue(async (isCurrent) => {
      try {
        const nextAudio = decodeAudioControls((await transport.request(Opcode.GetAudioControls)).payload);
        if (!isCurrent()) return;
        setAudio(nextAudio);
        const nextCrossfeed = decodeCrossfeedState((await transport.request(Opcode.GetCrossfeed)).payload);
        if (isCurrent() && crossfeedReadRevision === crossfeedRevision.current) applyCrossfeedState(nextCrossfeed);
        if (outputActive.current) {
          const nextOutput = decodeOutputProcessingState((await transport.request(Opcode.GetOutputProcessing)).payload);
          if (isCurrent() && outputReadRevision === outputRevision.current) applyOutputState(nextOutput);
        }
      } catch (reason) {
        if (isCurrent()) onError(`Audio settings: ${reason instanceof Error ? reason.message : "Unable to read device state"}`);
      }
    }).finally(() => {
      if (session.current === token) refreshing.current = false;
    });
  }, [applyCrossfeedState, applyOutputState, enqueue, onError, transport]);

  useEffect(() => {
    session.current++;
    active.current = connected && supported;
    outputActive.current = connected && outputSupported;
    crossfeedRevision.current++;
    outputRevision.current++;
    pendingCrossfeed.current = null;
    pendingOutput.current = null;
    currentCrossfeed.current = null;
    currentOutput.current = null;
    savingCrossfeedRef.current = false;
    savingOutputRef.current = false;
    refreshing.current = false;
    setAudio(null);
    setCrossfeed(null);
    setOutputProcessing(null);
    setSavingCrossfeed(false);
    setSavingOutput(false);
    void refresh();
    return () => {
      active.current = false;
      outputActive.current = false;
      session.current++;
      if (crossfeedTimer.current !== null) clearTimeout(crossfeedTimer.current);
      if (outputTimer.current !== null) clearTimeout(outputTimer.current);
      crossfeedTimer.current = null;
      outputTimer.current = null;
      pendingCrossfeed.current = null;
      pendingOutput.current = null;
    };
  }, [connected, outputSupported, refresh, supported]);

  const flushCrossfeed = useCallback(() => {
    if (crossfeedTimer.current !== null) clearTimeout(crossfeedTimer.current);
    crossfeedTimer.current = null;
    const next = pendingCrossfeed.current;
    if (!next) return chain.current.then(() => undefined);
    pendingCrossfeed.current = null;
    const editRevision = crossfeedRevision.current;
    return enqueue(async (isCurrent) => {
      try {
        const state = decodeCrossfeedState((await transport.request(Opcode.SetCrossfeed, encodeCrossfeed(next))).payload);
        if (isCurrent() && editRevision === crossfeedRevision.current) applyCrossfeedState(state);
      } catch (reason) {
        if (!isCurrent()) return;
        onError(`Crossfeed: ${reason instanceof Error ? reason.message : "Unable to update device"}`);
        try {
          const state = decodeCrossfeedState((await transport.request(Opcode.GetCrossfeed)).payload);
          if (isCurrent() && editRevision === crossfeedRevision.current) applyCrossfeedState(state);
        } catch {
          // Keep the draft until a later refresh can recover device state.
        }
        throw reason;
      }
    });
  }, [applyCrossfeedState, enqueue, onError, transport]);

  const update = useCallback((patch: Partial<CrossfeedConfig>) => {
    if (!active.current || !currentCrossfeed.current || savingCrossfeedRef.current) return;
    try {
      const live = decodeCrossfeed(encodeCrossfeed({ ...currentCrossfeed.current.live, ...patch }));
      crossfeedRevision.current++;
      applyCrossfeedState({ ...currentCrossfeed.current, live, dirty: !crossfeedConfigsEqual(live, currentCrossfeed.current.saved) });
      pendingCrossfeed.current = live;
      if (crossfeedTimer.current === null) {
        crossfeedTimer.current = setTimeout(() => { void flushCrossfeed().catch(() => undefined); }, previewIntervalMs);
      }
    } catch (reason) {
      onError(reason instanceof Error ? reason.message : "Invalid crossfeed settings");
    }
  }, [applyCrossfeedState, flushCrossfeed, onError]);

  const save = useCallback(async () => {
    if (!active.current || !currentCrossfeed.current?.dirty || savingCrossfeedRef.current) return;
    const token = session.current;
    savingCrossfeedRef.current = true;
    setSavingCrossfeed(true);
    try {
      pendingCrossfeed.current = { ...currentCrossfeed.current.live };
      await flushCrossfeed();
      if (!active.current || session.current !== token) return;
      await enqueue(async (isCurrent) => {
        const state = decodeCrossfeedState((await transport.request(Opcode.SaveCrossfeed, new Uint8Array(), 8000)).payload);
        if (!isCurrent()) return;
        applyCrossfeedState(state);
        onNotice("Crossfeed saved for power-on");
      });
    } catch (reason) {
      if (active.current && session.current === token) {
        onError(`Save crossfeed: ${reason instanceof Error ? reason.message : "Unable to save device settings"}`);
      }
    } finally {
      if (session.current === token) {
        savingCrossfeedRef.current = false;
        setSavingCrossfeed(false);
      }
    }
  }, [applyCrossfeedState, enqueue, flushCrossfeed, onError, onNotice, transport]);

  const flushOutput = useCallback(() => {
    if (outputTimer.current !== null) clearTimeout(outputTimer.current);
    outputTimer.current = null;
    const next = pendingOutput.current;
    if (!next) return chain.current.then(() => undefined);
    pendingOutput.current = null;
    const editRevision = outputRevision.current;
    return enqueue(async (isCurrent) => {
      try {
        const state = decodeOutputProcessingState((await transport.request(Opcode.SetOutputProcessing, encodeOutputProcessing(next))).payload);
        if (isCurrent() && editRevision === outputRevision.current) applyOutputState(state);
      } catch (reason) {
        if (!isCurrent()) return;
        onError(`Output processing: ${reason instanceof Error ? reason.message : "Unable to update device"}`);
        try {
          const state = decodeOutputProcessingState((await transport.request(Opcode.GetOutputProcessing)).payload);
          if (isCurrent() && editRevision === outputRevision.current) applyOutputState(state);
        } catch {
          // Keep the draft until a later refresh can recover device state.
        }
        throw reason;
      }
    });
  }, [applyOutputState, enqueue, onError, transport]);

  const updateOutput = useCallback((patch: Partial<OutputProcessingConfig>) => {
    if (!outputActive.current || !currentOutput.current || savingOutputRef.current) return;
    try {
      const live = decodeOutputProcessing(encodeOutputProcessing({ ...currentOutput.current.live, ...patch }));
      outputRevision.current++;
      applyOutputState({ ...currentOutput.current, live, dirty: !outputProcessingConfigsEqual(live, currentOutput.current.saved) });
      pendingOutput.current = live;
      if (outputTimer.current === null) {
        outputTimer.current = setTimeout(() => { void flushOutput().catch(() => undefined); }, previewIntervalMs);
      }
    } catch (reason) {
      onError(reason instanceof Error ? reason.message : "Invalid output processing settings");
    }
  }, [applyOutputState, flushOutput, onError]);

  const saveOutput = useCallback(async () => {
    if (!outputActive.current || !currentOutput.current?.dirty || savingOutputRef.current) return;
    const token = session.current;
    savingOutputRef.current = true;
    setSavingOutput(true);
    try {
      pendingOutput.current = { ...currentOutput.current.live };
      await flushOutput();
      if (!outputActive.current || session.current !== token) return;
      await enqueue(async (isCurrent) => {
        const state = decodeOutputProcessingState((await transport.request(Opcode.SaveOutputProcessing, new Uint8Array(), 8000)).payload);
        if (!isCurrent()) return;
        applyOutputState(state);
        onNotice("Output processing saved for power-on");
      });
    } catch (reason) {
      if (outputActive.current && session.current === token) {
        onError(`Save output processing: ${reason instanceof Error ? reason.message : "Unable to save device settings"}`);
      }
    } finally {
      if (session.current === token) {
        savingOutputRef.current = false;
        setSavingOutput(false);
      }
    }
  }, [applyOutputState, enqueue, flushOutput, onError, onNotice, transport]);

  return {
    supported,
    outputSupported,
    audio,
    crossfeed,
    outputProcessing,
    saving: savingCrossfeed || savingOutput,
    savingCrossfeed,
    savingOutput,
    refresh,
    update,
    save,
    updateOutput,
    saveOutput,
  };
}
