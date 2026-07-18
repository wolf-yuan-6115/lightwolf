import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { AlertTriangle, Check, Copy, RotateCcw, Save, Trash2, Usb, X } from "lucide-react";
import { bandColors, EqGraph } from "./EqGraph";
import { calculateAutoPreamp } from "./eqMath";
import { METER_REPORT_EVENT, PumperHidTransport } from "./hidTransport";
import { LevelMeter } from "./LevelMeter";
import { PUMPER_PERSISTENT_UDEV_COMMAND, PUMPER_TEMPORARY_UDEV_COMMAND } from "./linuxAccess";
import {
  decodeBand,
  decodeGlobal,
  decodeMeterLevel,
  decodeProfileState,
  decodeStatus,
  defaultConfig,
  DeviceStatus,
  encodeBand,
  encodeGlobal,
  encodeMeterConfig,
  EqBand,
  EqConfig,
  FilterType,
  METER_HEARTBEAT_INTERVAL_MS,
  METER_REPORT_INTERVAL_MS,
  METER_TIMEOUT_MS,
  MeterLevel,
  Opcode,
  ProfileState,
  ResponsePacket,
  WidthMode,
} from "./protocol";

type Dialog = "delete" | "flash" | "defaults" | "set-default" | "switch" | null;

const buttonBase = "inline-flex min-h-9 items-center justify-center gap-2 whitespace-nowrap rounded-md border px-3 py-2 text-sm font-bold transition-colors duration-150 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-emerald-400 disabled:opacity-40";
const primaryButton = `${buttonBase} border-emerald-400 bg-emerald-400 text-stone-950 hover:border-emerald-300 hover:bg-emerald-300`;
const secondaryButton = `${buttonBase} border-stone-700 bg-stone-900 text-stone-100 hover:border-stone-500 hover:bg-stone-800`;
const iconButton = `${secondaryButton} size-9 shrink-0 p-0`;
const inputClass = "h-9 rounded border border-stone-700 bg-stone-950 px-2 text-sm text-stone-100 outline-none transition-colors focus:border-emerald-500 focus:ring-1 focus:ring-emerald-500 disabled:opacity-45";
const rangeClass = "h-1 min-w-18 accent-emerald-400 disabled:opacity-45";
const eyebrowClass = "mb-1 text-[10px] font-extrabold uppercase text-emerald-400";

function cloneConfig(config: EqConfig): EqConfig {
  return { ...config, bands: config.bands.map((band) => ({ ...band })) };
}

function filterName(type: FilterType): string {
  if (type === FilterType.LowShelf) return "Low shelf";
  if (type === FilterType.HighShelf) return "High shelf";
  return "Peaking";
}

export default function App() {
  const transport = useRef(new PumperHidTransport());
  const configRef = useRef<EqConfig>(cloneConfig(defaultConfig));
  const autoRef = useRef(false);
  const bandTimers = useRef(new Map<number, number>());
  const globalTimer = useRef<number | null>(null);
  const mounted = useRef(true);

  const [config, setConfig] = useState<EqConfig>(() => cloneConfig(defaultConfig));
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [meterLevel, setMeterLevel] = useState<MeterLevel | null>(null);
  const [profileState, setProfileState] = useState<ProfileState>({
    count: 10,
    activeProfile: 0,
    persistedProfile: 0,
    presentMask: 0,
    bankGeneration: 0,
  });
  const [selectedProfile, setSelectedProfile] = useState(0);
  const [pendingProfile, setPendingProfile] = useState<number | null>(null);
  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [selectedBand, setSelectedBand] = useState(0);
  const [autoPreamp, setAutoPreamp] = useState(false);
  const [localDirty, setLocalDirty] = useState(false);
  const [dialog, setDialog] = useState<Dialog>(null);
  const [writing, setWriting] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [copiedAccess, setCopiedAccess] = useState<"persistent" | "temporary" | null>(null);

  const supported = PumperHidTransport.supported();
  const isLinux = typeof navigator !== "undefined" && navigator.userAgent.includes("Linux");
  const sampleRateHz = status?.sampleRateHz ?? 48000;
  const autoResult = useMemo(() => calculateAutoPreamp(config, sampleRateHz), [config, sampleRateHz]);
  const selectedProfileEmpty = (profileState.presentMask & (1 << selectedProfile)) === 0;
  const selectedProfileIsDefault = !selectedProfileEmpty && profileState.persistedProfile === selectedProfile;
  const hasUnsavedEdits = localDirty;
  const needsSave = hasUnsavedEdits || (connected && selectedProfileEmpty);

  const clearTimers = () => {
    bandTimers.current.forEach((timer) => window.clearTimeout(timer));
    bandTimers.current.clear();
    if (globalTimer.current !== null) window.clearTimeout(globalTimer.current);
    globalTimer.current = null;
  };

  const sendGlobalSoon = (next: EqConfig) => {
    if (!connected) return;
    if (globalTimer.current !== null) window.clearTimeout(globalTimer.current);
    globalTimer.current = window.setTimeout(() => {
      transport.current.request(Opcode.SetGlobal, encodeGlobal(next)).catch((reason: unknown) => {
        setError(reason instanceof Error ? reason.message : "Unable to update preamp");
      });
    }, 55);
  };

  const sendBandSoon = (index: number, band: EqBand) => {
    if (!connected) return;
    const previous = bandTimers.current.get(index);
    if (previous !== undefined) window.clearTimeout(previous);
    bandTimers.current.set(
      index,
      window.setTimeout(() => {
        bandTimers.current.delete(index);
        transport.current.request(Opcode.SetBand, encodeBand(index, band)).catch((reason: unknown) => {
          setError(reason instanceof Error ? reason.message : `Unable to update band ${index + 1}`);
        });
      }, 55),
    );
  };

  const commitConfig = (next: EqConfig, bandIndex?: number, globalChanged = false) => {
    if (autoRef.current) {
      next.preampDb = calculateAutoPreamp(next, sampleRateHz).preampDb;
      globalChanged = true;
    }
    configRef.current = next;
    setConfig(next);
    setLocalDirty(true);
    setNotice(null);
    if (bandIndex !== undefined) sendBandSoon(bandIndex, next.bands[bandIndex]);
    if (globalChanged) sendGlobalSoon(next);
  };

  const updateBand = (index: number, patch: Partial<EqBand>) => {
    const next = cloneConfig(configRef.current);
    next.bands[index] = { ...next.bands[index], ...patch };
    commitConfig(next, index);
  };

  const updateGlobal = (patch: Partial<Pick<EqConfig, "enabled" | "preampDb">>) => {
    const next = { ...cloneConfig(configRef.current), ...patch };
    commitConfig(next, undefined, true);
  };

  const readDevice = useCallback(async (knownStoredProfile = false) => {
    const hello = await transport.current.request(Opcode.Hello);
    const nextStatus = decodeStatus(hello.payload);
    const global = decodeGlobal((await transport.current.request(Opcode.GetGlobal)).payload);
    const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
    const bands: EqBand[] = [];
    for (let index = 0; index < nextStatus.bandCount; index++) {
      const response = await transport.current.request(Opcode.GetBand, new Uint8Array([index]));
      bands.push(decodeBand(response.payload).band);
    }
    let next: EqConfig = { ...global, bands };
    let autoChanged = false;
    if (autoRef.current) {
      const preampDb = calculateAutoPreamp(next, nextStatus.sampleRateHz).preampDb;
      autoChanged = preampDb !== next.preampDb;
      next = { ...next, preampDb };
      if (autoChanged) await transport.current.request(Opcode.SetGlobal, encodeGlobal(next));
    }
    configRef.current = next;
    setConfig(next);
    setStatus(nextStatus);
    setProfileState(nextProfiles);
    setSelectedProfile(nextProfiles.activeProfile);
    setLocalDirty((!knownStoredProfile && nextStatus.dirty) || autoChanged);
  }, []);

  const openDevice = useCallback(
    async (device: HIDDevice) => {
      setConnecting(true);
      setError(null);
      try {
        await transport.current.open(device);
        transport.current.addEventListener("disconnect", () => {
          if (!mounted.current) return;
          clearTimers();
          setConnected(false);
          setStatus(null);
          setMeterLevel(null);
          setProfileState((current) => ({ ...current, presentMask: 0, activeProfile: 0, persistedProfile: 0 }));
          setSelectedProfile(0);
          setError("Pumper disconnected");
        }, { once: true });
        setConnected(true);
        await readDevice();
      } catch (reason) {
        await transport.current.close().catch(() => undefined);
        setConnected(false);
        setError(reason instanceof Error ? reason.message : "Unable to connect to Pumper");
      } finally {
        setConnecting(false);
      }
    },
    [readDevice],
  );

  useEffect(() => {
    mounted.current = true;
    if (supported) {
      PumperHidTransport.grantedDevice().then((device) => {
        if (device && mounted.current) void openDevice(device);
      });
    }
    return () => {
      mounted.current = false;
      clearTimers();
      void transport.current.close();
    };
  }, [openDevice, supported]);

  useEffect(() => {
    if (!connected) return;
    const timer = window.setInterval(() => {
      transport.current
        .request(Opcode.GetStatus)
        .then((response) => setStatus(decodeStatus(response.payload)))
        .catch(() => undefined);
    }, 1000);
    return () => window.clearInterval(timer);
  }, [connected]);

  useEffect(() => {
    if (!connected) return;

    const handleMeterReport = (event: Event) => {
      const response = (event as CustomEvent<ResponsePacket>).detail;
      setMeterLevel(decodeMeterLevel(response.payload));
    };
    const heartbeat = () => transport.current.request(Opcode.MeterKeepalive).catch(() => undefined);

    transport.current.addEventListener(METER_REPORT_EVENT, handleMeterReport);
    transport.current
      .request(Opcode.MeterStart, encodeMeterConfig(METER_REPORT_INTERVAL_MS, METER_TIMEOUT_MS))
      .catch((reason: unknown) => setError(reason instanceof Error ? reason.message : "Unable to start output metering"));
    const heartbeatTimer = window.setInterval(heartbeat, METER_HEARTBEAT_INTERVAL_MS);
    const handleVisibility = () => {
      if (!document.hidden) void heartbeat();
    };
    document.addEventListener("visibilitychange", handleVisibility);

    return () => {
      window.clearInterval(heartbeatTimer);
      document.removeEventListener("visibilitychange", handleVisibility);
      transport.current.removeEventListener(METER_REPORT_EVENT, handleMeterReport);
      setMeterLevel(null);
      void transport.current.request(Opcode.MeterStop).catch(() => undefined);
    };
  }, [connected]);

  useEffect(() => {
    if (!autoRef.current) return;
    const next = cloneConfig(configRef.current);
    const calculated = calculateAutoPreamp(next, sampleRateHz).preampDb;
    if (next.preampDb !== calculated) {
      next.preampDb = calculated;
      configRef.current = next;
      setConfig(next);
      setLocalDirty(true);
      sendGlobalSoon(next);
    }
  }, [sampleRateHz]);

  const requestConnection = async () => {
    setConnecting(true);
    setError(null);
    try {
      const device = await PumperHidTransport.requestDevice();
      if (device) {
        await openDevice(device);
      } else {
        setError("No Pumper DAC was selected");
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Device permission was not granted");
    } finally {
      setConnecting(false);
    }
  };

  const copyLinuxAccess = async (value: string, kind: "persistent" | "temporary") => {
    try {
      await navigator.clipboard.writeText(value);
      setCopiedAccess(kind);
    } catch {
      setError("Unable to copy the Linux access command");
    }
  };

  const toggleAuto = (enabled: boolean) => {
    autoRef.current = enabled;
    setAutoPreamp(enabled);
    if (enabled) {
      const next = cloneConfig(configRef.current);
      next.preampDb = calculateAutoPreamp(next, sampleRateHz).preampDb;
      commitConfig(next, undefined, true);
    }
  };

  const loadProfile = async (index: number) => {
    clearTimers();
    await transport.current.request(Opcode.LoadProfile, new Uint8Array([index]));
    await readDevice(true);
    setNotice(`Profile ${index + 1} loaded into live preview`);
  };

  const selectProfile = async (index: number) => {
    if (index === selectedProfile) return;
    if ((profileState.presentMask & (1 << index)) === 0) {
      setSelectedProfile(index);
      setNotice(`Profile ${index + 1} is empty; save to store the current EQ there`);
      return;
    }
    if (hasUnsavedEdits) {
      setPendingProfile(index);
      setDialog("switch");
      return;
    }
    setWriting(true);
    setError(null);
    try {
      await loadProfile(index);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Unable to load profile");
    } finally {
      setWriting(false);
    }
  };

  const confirmDialog = async () => {
    const action = dialog;
    setDialog(null);
    setWriting(true);
    setError(null);
    try {
      if (action === "flash") {
        clearTimers();
        const current = cloneConfig(configRef.current);
        await transport.current.request(Opcode.SetGlobal, encodeGlobal(current));
        for (let index = 0; index < current.bands.length; index++) {
          await transport.current.request(Opcode.SetBand, encodeBand(index, current.bands[index]));
        }
        await transport.current.request(Opcode.SaveProfile, new Uint8Array([selectedProfile]), 8000);
        await readDevice(true);
        setLocalDirty(false);
        setNotice(`Profile ${selectedProfile + 1} saved to flash`);
      } else if (action === "set-default") {
        await transport.current.request(Opcode.SetDefaultProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        setNotice(`Profile ${selectedProfile + 1} will load at power-on`);
      } else if (action === "delete") {
        await transport.current.request(Opcode.DeleteProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        setLocalDirty(false);
        setNotice(`Profile ${selectedProfile + 1} deleted`);
      } else if (action === "defaults") {
        clearTimers();
        await transport.current.request(Opcode.RestoreDefaults);
        await readDevice();
        setLocalDirty(true);
        setNotice("Factory EQ loaded into live preview");
      } else if (action === "switch" && pendingProfile !== null) {
        await loadProfile(pendingProfile);
      }
      if (action === "defaults") {
        const response = await transport.current.request(Opcode.GetStatus);
        setStatus(decodeStatus(response.payload));
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "The operation failed");
    } finally {
      setPendingProfile(null);
      setWriting(false);
    }
  };

  const closeDialog = () => {
    setDialog(null);
    setPendingProfile(null);
  };

  const selected = config.bands[selectedBand] ?? config.bands[0];
  const deletingOnlyProfile = profileState.presentMask === (1 << selectedProfile);
  return (
    <main className="min-h-screen w-full bg-stone-950 text-stone-100">
      <header className="sticky top-0 z-20 flex min-h-18 items-center justify-between gap-5 border-b border-stone-800 bg-stone-950/95 px-[clamp(18px,3vw,48px)] py-3 backdrop-blur max-[900px]:items-start max-sm:min-h-0 max-sm:px-3.5 max-sm:py-2.5">
        <div className="flex min-w-0 items-baseline gap-3 max-sm:block">
          <span className="text-xs font-extrabold uppercase text-emerald-400 max-sm:mb-0.5 max-sm:block max-sm:text-[9px]">LightWolf</span>
          <h1 className="text-[22px] leading-none font-bold max-sm:text-[19px]">Pumper EQ</h1>
        </div>
        <div className="flex items-center justify-end gap-2.5 max-[900px]:flex-wrap">
          <div className={`inline-flex h-9 max-w-55 items-center gap-2 overflow-hidden border-r border-stone-800 px-3 text-[13px] font-semibold text-ellipsis whitespace-nowrap max-[900px]:hidden ${connected ? "text-stone-200" : "text-stone-500"}`}>
            <span className={`size-2 shrink-0 rounded-full ${connected ? "bg-emerald-400 shadow-[0_0_0_3px_rgba(52,211,153,0.14)]" : "bg-stone-600"}`} />
            {connected ? transport.current.productName : "Not connected"}
          </div>
          <label className="grid h-10 content-center gap-0.5 max-sm:h-9">
            <span className="text-[9px] font-extrabold uppercase text-stone-500 max-sm:hidden">Profile</span>
            <select className="h-7 w-34 rounded border border-stone-700 bg-stone-900 px-2 text-xs font-bold text-stone-100 outline-none focus:border-emerald-500 max-sm:h-9 max-sm:w-28" value={selectedProfile} onChange={(event) => void selectProfile(Number(event.target.value))} disabled={!connected || writing}>
              {Array.from({ length: profileState.count }, (_, index) => (
                <option value={index} key={index}>Profile {index + 1}{(profileState.presentMask & (1 << index)) === 0 ? " (empty)" : index === profileState.persistedProfile ? " (default)" : ""}</option>
              ))}
            </select>
          </label>
          <button
            className={`${iconButton} text-stone-400 hover:border-red-900 hover:bg-red-950/40 hover:text-red-300`}
            onClick={() => setDialog("delete")}
            disabled={!connected || writing || hasUnsavedEdits || selectedProfileEmpty}
            title={`Delete Profile ${selectedProfile + 1}`}
            aria-label={`Delete Profile ${selectedProfile + 1}`}
          >
            <Trash2 size={17} />
          </button>
          <button
            className={`${secondaryButton} min-w-28 max-sm:size-9 max-sm:min-w-9 max-sm:p-0 max-sm:text-[0px]`}
            onClick={() => setDialog("set-default")}
            disabled={!connected || writing || hasUnsavedEdits || selectedProfileEmpty || selectedProfileIsDefault}
            title={selectedProfileIsDefault ? "Current power-on profile" : "Load this profile automatically at power-on"}
          >
            <Check size={17} />
            {selectedProfileIsDefault ? "Default" : "Make default"}
          </button>
          <button className={`${secondaryButton} max-sm:size-9 max-sm:p-0 max-sm:text-[0px]`} onClick={() => setDialog("defaults")} disabled={!connected || writing} title="Restore compiled EQ defaults">
            <RotateCcw size={17} />
            Defaults
          </button>
          <button className={`${primaryButton} max-sm:min-h-9 max-sm:px-2 max-sm:text-xs`} onClick={() => setDialog("flash")} disabled={!connected || !needsSave || writing}>
            <Save size={17} />
            {writing ? "Working..." : "Save profile"}
          </button>
        </div>
      </header>

      {!supported && <div className="flex min-h-11 items-center gap-2.5 border-b border-red-950 bg-stone-950 px-[clamp(18px,3vw,48px)] py-2.5 text-sm text-red-300"><AlertTriangle size={18} />WebHID is unavailable. Open this page in Chrome or Edge.</div>}
      {error && <div className="flex min-h-11 items-center gap-2.5 border-b border-red-950 bg-stone-950 px-[clamp(18px,3vw,48px)] py-2.5 text-sm text-red-300"><AlertTriangle size={18} />{error}<button className="ml-auto grid place-items-center p-1" onClick={() => setError(null)} aria-label="Dismiss error"><X size={16} /></button></div>}
      {notice && <div className="flex min-h-11 items-center gap-2.5 border-b border-emerald-950 bg-stone-950 px-[clamp(18px,3vw,48px)] py-2.5 text-sm text-emerald-300"><Check size={18} />{notice}<button className="ml-auto grid place-items-center p-1" onClick={() => setNotice(null)} aria-label="Dismiss message"><X size={16} /></button></div>}

      {!connected && supported && (
        <section className="flex min-h-42 items-center justify-between gap-6 border-b border-stone-800 bg-stone-900 px-[clamp(18px,3vw,48px)] py-6 max-sm:min-h-36 max-sm:px-3.5 max-sm:py-5">
          <div>
            <p className={eyebrowClass}>USB DAC CONTROL</p>
            <h2 className="mt-1 text-[34px] leading-none font-bold">Pumper</h2>
          </div>
          <button className={`${primaryButton} min-w-38`} onClick={requestConnection} disabled={connecting}>
            <Usb size={19} />
            {connecting ? "Connecting..." : "Connect DAC"}
          </button>
        </section>
      )}

      {!connected && supported && isLinux && (
        <section className="grid grid-cols-[minmax(240px,0.7fr)_minmax(420px,1.3fr)] items-center gap-6 border-b border-stone-800 bg-stone-900 px-[clamp(18px,3vw,48px)] py-4.5 max-[900px]:grid-cols-1 max-[900px]:gap-3 max-sm:px-3.5 max-sm:py-4">
          <div>
            <p className={eyebrowClass}>LINUX DEVICE ACCESS</p>
            <h2 className="mb-1 text-[17px] font-bold">WebHID permission</h2>
            <p className="text-xs leading-relaxed text-stone-400">Copy and run one command block. Both apply immediately; choose whether access should survive a reboot.</p>
          </div>
          <div className="min-w-0">
            <div>
              <div className="mb-1.5 flex items-baseline justify-between gap-3 text-xs"><strong>Permanent</strong><span className="text-[11px] text-stone-500">Survives reboot</span></div>
              <div className="flex min-h-14 min-w-0 items-center gap-3 rounded bg-stone-950 py-2.5 pr-2.5 pl-3 text-stone-200 ring-1 ring-stone-800">
                <code className="min-w-0 flex-1 [overflow-wrap:anywhere] whitespace-pre-wrap text-[11px] leading-relaxed">{PUMPER_PERSISTENT_UDEV_COMMAND}</code>
                <button className="grid size-9 shrink-0 place-items-center rounded border border-stone-700 bg-stone-900 text-stone-300 hover:bg-stone-800" type="button" onClick={() => copyLinuxAccess(PUMPER_PERSISTENT_UDEV_COMMAND, "persistent")} title="Copy permanent access command" aria-label="Copy permanent access command">
                  {copiedAccess === "persistent" ? <Check size={17} /> : <Copy size={17} />}
                </button>
              </div>
            </div>
            <div className="mt-3 border-t border-stone-800 pt-3">
              <div className="mb-1.5 flex items-baseline justify-between gap-3 text-xs"><strong>This boot</strong><span className="text-[11px] text-stone-500">Cleared after reboot</span></div>
              <div className="flex min-h-14 min-w-0 items-center gap-3 rounded bg-stone-950 py-2.5 pr-2.5 pl-3 text-stone-200 ring-1 ring-stone-800">
                <code className="min-w-0 flex-1 [overflow-wrap:anywhere] whitespace-pre-wrap text-[11px] leading-relaxed">{PUMPER_TEMPORARY_UDEV_COMMAND}</code>
                <button className="grid size-9 shrink-0 place-items-center rounded border border-stone-700 bg-stone-900 text-stone-300 hover:bg-stone-800" type="button" onClick={() => copyLinuxAccess(PUMPER_TEMPORARY_UDEV_COMMAND, "temporary")} title="Copy temporary access command" aria-label="Copy temporary access command">
                  {copiedAccess === "temporary" ? <Check size={17} /> : <Copy size={17} />}
                </button>
              </div>
            </div>
          </div>
        </section>
      )}

      <section className={`w-full ${connected && !writing ? "" : "pointer-events-none opacity-45 grayscale"}`} aria-disabled={!connected || writing}>
        <div className="grid grid-cols-[minmax(150px,0.6fr)_minmax(330px,1.4fr)_minmax(350px,1fr)] items-center gap-7 border-b border-stone-800 bg-stone-900 px-[clamp(18px,3vw,48px)] py-4 max-[900px]:grid-cols-[1fr_2fr] max-sm:grid-cols-1 max-sm:gap-4 max-sm:px-3.5 max-sm:py-3.5">
          <label className="inline-flex items-center gap-2.5 text-sm font-bold">
            <input className="peer sr-only" type="checkbox" checked={config.enabled} onChange={(event) => updateGlobal({ enabled: event.target.checked })} disabled={!connected} />
            <span className="relative h-6 w-10 shrink-0 rounded-full bg-stone-700 p-1 transition-colors after:block after:size-4 after:rounded-full after:bg-stone-200 after:shadow after:transition-transform peer-checked:bg-emerald-500 peer-checked:after:translate-x-4 peer-focus-visible:ring-2 peer-focus-visible:ring-emerald-400" />
            EQ enabled
          </label>

          <div className="min-w-0">
            <div className="mb-1.5 flex items-center justify-between text-[13px] font-bold">
              <label htmlFor="preamp">Preamp</label>
              <div className="inline-flex h-8 rounded bg-stone-950 p-1 ring-1 ring-stone-700" role="group" aria-label="Preamp mode">
                <button type="button" className={`min-w-14 rounded-sm px-2 text-[11px] font-bold transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-emerald-400 ${autoPreamp ? "text-stone-500" : "bg-stone-700 text-stone-100 shadow"}`} onClick={() => toggleAuto(false)} disabled={!connected}>Manual</button>
                <button type="button" className={`min-w-14 rounded-sm px-2 text-[11px] font-bold transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-emerald-400 ${autoPreamp ? "bg-emerald-400 text-stone-950 shadow" : "text-stone-500"}`} onClick={() => toggleAuto(true)} disabled={!connected}>Auto</button>
              </div>
            </div>
            <div className="flex items-center gap-2">
              <input className={`${rangeClass} flex-1`} id="preamp" type="range" min="-24" max="12" step="0.1" value={Math.max(-24, config.preampDb)} onChange={(event) => updateGlobal({ preampDb: Number(event.target.value) })} disabled={!connected || autoPreamp} />
              <input className={`${inputClass} w-20`} type="number" min="-241" max="12" step="0.1" value={config.preampDb} onChange={(event) => updateGlobal({ preampDb: Number(event.target.value) })} disabled={!connected || autoPreamp} />
              <span className="min-w-6 text-xs text-stone-500">dB</span>
            </div>
            <div className="mt-1.5 text-[11px] text-stone-500">Peak {autoResult.peakDb >= 0 ? "+" : ""}{autoResult.peakDb.toFixed(2)} dB · 0.5 dB reserve</div>
          </div>

          <div className="grid grid-cols-3 gap-2 max-[900px]:col-span-full max-sm:col-span-1">
            <div className="min-w-0 border-l border-stone-700 pl-3"><span className="block truncate text-[11px] text-stone-500">Rate</span><strong className="mt-0.5 block truncate text-sm">{(sampleRateHz / 1000).toFixed(sampleRateHz % 1000 ? 1 : 0)} kHz</strong></div>
            <div className="min-w-0 border-l border-stone-700 pl-3"><span className="block truncate text-[11px] text-stone-500">State</span><strong className="mt-0.5 block truncate text-sm">{status?.streaming ? "Streaming" : "Idle"}</strong></div>
            <div className="min-w-0 border-l border-stone-700 pl-3"><span className="block truncate text-[11px] text-stone-500">Flash</span><strong className={`mt-0.5 block truncate text-sm ${needsSave ? "text-amber-400" : ""}`}>{selectedProfileEmpty && !hasUnsavedEdits ? "Empty slot" : needsSave ? "Unsaved" : "Saved"}</strong></div>
          </div>
        </div>

        <LevelMeter level={meterLevel} />

        <section className="border-b border-stone-800 bg-stone-950 px-[clamp(18px,3vw,48px)] py-6 max-sm:px-3.5 max-sm:py-5">
          <div className="mb-4 flex items-end justify-between gap-4 max-sm:flex-col max-sm:items-start">
            <div><p className={eyebrowClass}>LIVE PREVIEW</p><h2 className="text-[19px] font-bold">Frequency response</h2></div>
            <div className="flex items-center gap-2 text-[13px] text-stone-400 max-sm:w-full"><span className="inline-grid size-6 place-items-center rounded-full text-[11px] font-extrabold text-stone-950" style={{ backgroundColor: bandColors[selectedBand] }}>{selectedBand + 1}</span>{filterName(selected.type)} · {selected.frequencyHz.toLocaleString()} Hz · {selected.gainDb > 0 ? "+" : ""}{selected.gainDb.toFixed(1)} dB</div>
          </div>
          <EqGraph config={config} sampleRateHz={sampleRateHz} selectedBand={selectedBand} onSelectBand={setSelectedBand} onChangeBand={updateBand} />
        </section>

        <section className="overflow-x-auto border-b border-stone-800 bg-stone-900 px-[clamp(18px,3vw,48px)] py-6 max-sm:px-3.5 max-sm:py-5">
          <div className="mb-4"><p className={eyebrowClass}>TEN BANDS</p><h2 className="text-[19px] font-bold">Filter configuration</h2></div>
          <div className="min-w-[930px] w-full">
            <div className="grid min-h-8 grid-cols-[92px_minmax(120px,0.9fr)_minmax(130px,0.8fr)_minmax(250px,1.5fr)_minmax(120px,0.8fr)_minmax(130px,0.8fr)] items-center gap-3 px-2.5 text-[10px] font-extrabold uppercase text-stone-500"><span>Band</span><span>Filter</span><span>Frequency</span><span>Gain</span><span>Width</span><span>Value</span></div>
            {config.bands.map((band, index) => {
              const widthLabel = band.type === FilterType.Peaking ? (band.widthMode === WidthMode.Q ? "Q" : "Bandwidth") : "Slope";
              const widthValue = band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? band.bandwidthOctaves : band.q;
              return (
                <div className={`grid min-h-14 grid-cols-[92px_minmax(120px,0.9fr)_minmax(130px,0.8fr)_minmax(250px,1.5fr)_minmax(120px,0.8fr)_minmax(130px,0.8fr)] items-center gap-3 border-t border-stone-800 px-2.5 py-2 transition-colors ${selectedBand === index ? "bg-stone-800 shadow-[inset_3px_0_#34d399]" : "hover:bg-stone-800/60"} ${band.enabled ? "" : "text-stone-500"}`} key={index} onClick={() => setSelectedBand(index)}>
                  <div className="flex items-center gap-3"><input className="size-4 accent-emerald-400" type="checkbox" checked={band.enabled} onChange={(event) => updateBand(index, { enabled: event.target.checked })} aria-label={`Enable band ${index + 1}`} /><span className="inline-grid size-6 place-items-center rounded-full text-[11px] font-extrabold text-stone-950" style={{ backgroundColor: bandColors[index] }}>{index + 1}</span></div>
                  <select className={`${inputClass} w-full`} value={band.type} onChange={(event) => updateBand(index, { type: Number(event.target.value) as FilterType })} aria-label={`Band ${index + 1} filter type`}>
                    <option value={FilterType.LowShelf}>Low shelf</option><option value={FilterType.Peaking}>Peaking</option><option value={FilterType.HighShelf}>High shelf</option>
                  </select>
                  <div className="flex items-center gap-2"><input className={`${inputClass} w-20`} type="number" min="20" max="20000" step="1" value={band.frequencyHz} onChange={(event) => updateBand(index, { frequencyHz: Number(event.target.value) })} /><span className="min-w-6 text-xs text-stone-500">Hz</span></div>
                  <div className="flex items-center gap-2"><input className={`${rangeClass} flex-1`} type="range" min="-24" max="24" step="0.1" value={band.gainDb} onChange={(event) => updateBand(index, { gainDb: Number(event.target.value) })} /><div className="flex items-center gap-2"><input className={`${inputClass} w-17`} type="number" min="-24" max="24" step="0.1" value={band.gainDb} onChange={(event) => updateBand(index, { gainDb: Number(event.target.value) })} /><span className="min-w-6 text-xs text-stone-500">dB</span></div></div>
                  {band.type === FilterType.Peaking ? <select className={`${inputClass} w-full`} value={band.widthMode} onChange={(event) => updateBand(index, { widthMode: Number(event.target.value) as WidthMode })}><option value={WidthMode.Bandwidth}>Bandwidth</option><option value={WidthMode.Q}>Q</option></select> : <span className="text-[13px] text-stone-500">Slope</span>}
                  <div className="flex items-center gap-2"><input className={`${inputClass} w-20`} type="number" min="0.1" max={band.type === FilterType.Peaking && band.widthMode === WidthMode.Q ? 20 : band.type === FilterType.Peaking ? 4 : 1} step="0.01" value={widthValue} aria-label={`${widthLabel} for band ${index + 1}`} onChange={(event) => updateBand(index, band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? { bandwidthOctaves: Number(event.target.value) } : { q: Number(event.target.value) })} /><span className="min-w-6 text-xs text-stone-500">{band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? "oct" : widthLabel}</span></div>
                </div>
              );
            })}
          </div>
        </section>

        <footer className="flex min-h-13 flex-wrap items-center gap-x-6 gap-y-2 bg-stone-950 px-[clamp(18px,3vw,48px)] py-3 text-[11px] text-stone-500 max-sm:gap-x-4 max-sm:px-3.5">
          <span>Firmware {status?.firmwareVersion ?? "-"}</span><span>Applied generation {status?.appliedGeneration ?? "-"}</span><span>Underrun frames {status?.underrunFrames.toLocaleString() ?? "-"}</span><span>Queue pressure {status?.droppedBlocks.toLocaleString() ?? "-"}</span>
        </footer>
      </section>

      {dialog && (
        <div className="fixed inset-0 z-50 grid place-items-center bg-black/75 p-5 backdrop-blur-sm" role="presentation" onMouseDown={closeDialog}>
          <div className="w-full max-w-108 rounded-lg border border-stone-700 bg-stone-900 p-6 shadow-2xl" role="dialog" aria-modal="true" aria-labelledby="dialog-title" onMouseDown={(event) => event.stopPropagation()}>
            <div className="mb-4 grid size-10 place-items-center rounded-full bg-amber-400/15 text-amber-300"><AlertTriangle size={22} /></div>
            <h2 className="text-[19px] font-bold" id="dialog-title">{dialog === "flash" ? `Save Profile ${selectedProfile + 1}?` : dialog === "delete" ? `Delete Profile ${selectedProfile + 1}?` : dialog === "set-default" ? `Make Profile ${selectedProfile + 1} default?` : dialog === "switch" ? "Discard unsaved changes?" : "Restore factory EQ?"}</h2>
            <p className="mt-2 mb-6 leading-relaxed text-stone-400">{dialog === "flash" ? "Audio may briefly pause while the DAC writes and verifies the profile bank." : dialog === "delete" ? deletingOnlyProfile ? "This is the last stored profile. All slots will be empty and the flat factory EQ will load at the next power-on." : selectedProfileIsDefault ? "This is the power-on default. The lowest-numbered remaining profile will become the new default." : "This stored profile will become empty. The current live EQ will keep playing until another profile is loaded." : dialog === "set-default" ? "Audio may briefly pause while the DAC updates flash. This profile will load automatically at power-on." : dialog === "switch" ? `The live edits will be replaced by Profile ${(pendingProfile ?? 0) + 1}.` : "The flat factory EQ will replace the live preview. It will remain unsaved until you save the profile."}</p>
            <div className="flex items-center justify-end gap-2.5"><button className={secondaryButton} onClick={closeDialog}>Cancel</button><button className={dialog === "delete" ? `${buttonBase} border-red-500 bg-red-500 text-white hover:bg-red-400` : primaryButton} onClick={confirmDialog}>{dialog === "flash" ? "Save profile" : dialog === "delete" ? "Delete profile" : dialog === "set-default" ? "Make default" : dialog === "switch" ? "Load profile" : "Restore defaults"}</button></div>
          </div>
        </div>
      )}
    </main>
  );
}
