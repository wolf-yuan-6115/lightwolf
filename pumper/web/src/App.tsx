import { useCallback, useEffect, useRef, useState } from "react";
import { AlertTriangle, Check, Info, Moon, Power, RotateCcw, Save, Sun, Trash2, Upload, Usb, X } from "lucide-react";
import { bandColors, EqGraph } from "./EqGraph";
import { calculateAutoPreamp } from "./eqMath";
import { METER_REPORT_EVENT, PumperHidTransport } from "./hidTransport";
import { LevelMeter } from "./LevelMeter";
import { NumericInput } from "./NumericInput";
import { SelectMenu, type SelectMenuOption } from "./SelectMenu";
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

type Dialog = "clear" | "flash" | "defaults" | "set-default" | "switch" | null;
type DeviceDialog = "info" | "restart" | "bootsel" | "bootsel-ready" | null;
type Theme = "light" | "dark";

const buttonBase = "btn btn-sm";
const primaryButton = "btn btn-primary btn-sm";
const secondaryButton = "btn btn-sm";
const clearButton = "btn btn-square btn-error btn-soft btn-sm shrink-0";
const rangeClass = "range range-xs min-w-18";
const themeStorageKey = "pumper-theme";
const filterOptions: readonly SelectMenuOption<FilterType>[] = [
  { value: FilterType.LowShelf, label: "Low shelf" },
  { value: FilterType.Peaking, label: "Peaking" },
  { value: FilterType.HighShelf, label: "High shelf" },
];
const widthOptions: readonly SelectMenuOption<WidthMode>[] = [
  { value: WidthMode.Bandwidth, label: "Bandwidth" },
  { value: WidthMode.Q, label: "Q" },
];

function DiagnosticLabel({ label, help }: { label: string; help: string }) {
  return (
    <span className="flex items-center gap-1.5 text-sm text-base-content/65">
      {label}
      <span
        className="tooltip tooltip-top inline-flex size-5 cursor-help items-center justify-center rounded-full text-base-content/50 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-base-content"
        tabIndex={0}
        aria-label={`About ${label}: ${help}`}
      >
        <span className="tooltip-content z-10 w-64 max-w-[calc(100vw-2rem)] whitespace-normal text-left text-xs leading-relaxed">{help}</span>
        <Info size={14} aria-hidden="true" />
      </span>
    </span>
  );
}

function initialTheme(): Theme {
  if (typeof window === "undefined") return "light";
  try {
    const stored = window.localStorage.getItem(themeStorageKey);
    if (stored === "light" || stored === "dark") return stored;
  } catch {
    // Theme persistence is optional when storage is unavailable.
  }
  return window.matchMedia?.("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function cloneConfig(config: EqConfig): EqConfig {
  return { ...config, bands: config.bands.map((band) => ({ ...band })) };
}

function configsEqual(left: EqConfig, right: EqConfig): boolean {
  if (left.enabled !== right.enabled || left.preampDb !== right.preampDb || left.bands.length !== right.bands.length) return false;
  return left.bands.every((band, index) => {
    const other = right.bands[index];
    return band.enabled === other.enabled &&
      band.type === other.type &&
      band.widthMode === other.widthMode &&
      band.frequencyHz === other.frequencyHz &&
      band.gainDb === other.gainDb &&
      band.q === other.q &&
      band.bandwidthOctaves === other.bandwidthOctaves;
  });
}

function rangeMessage(label: string, minimum: number, maximum: number, unit = ""): string {
  const format = (value: number) => Math.abs(value) >= 1000 ? value.toLocaleString("en-US") : String(value);
  return `${label} must be between ${format(minimum)} and ${format(maximum)}${unit ? ` ${unit}` : ""}.`;
}

function inRange(value: number, minimum: number, maximum: number): boolean {
  return Number.isFinite(value) && value >= minimum && value <= maximum;
}

function validateBand(band: EqBand, index: number): string | null {
  const label = `Band ${index + 1}`;
  if (band.type !== FilterType.LowShelf && band.type !== FilterType.Peaking && band.type !== FilterType.HighShelf) {
    return `${label} filter type is not supported.`;
  }
  if (band.widthMode !== WidthMode.Q && band.widthMode !== WidthMode.Bandwidth) {
    return `${label} width mode is not supported.`;
  }
  if (!inRange(band.frequencyHz, 20, 20000)) return rangeMessage(`${label} frequency`, 20, 20000, "Hz");
  if (!inRange(band.gainDb, -24, 24)) return rangeMessage(`${label} gain`, -24, 24, "dB");
  if (!inRange(band.bandwidthOctaves, 0.1, 4)) return rangeMessage(`${label} bandwidth`, 0.1, 4, "octaves");
  const qMaximum = band.type === FilterType.Peaking ? 20 : 1;
  const qLabel = band.type === FilterType.Peaking ? "Q" : "slope";
  if (!inRange(band.q, 0.1, qMaximum)) return rangeMessage(`${label} ${qLabel}`, 0.1, qMaximum);
  return null;
}

function validateConfig(config: EqConfig): string | null {
  if (!inRange(config.preampDb, -241, 12)) return rangeMessage("Preamp gain", -241, 12, "dB");
  for (let index = 0; index < config.bands.length; index++) {
    const error = validateBand(config.bands[index], index);
    if (error) return error;
  }
  return null;
}

function describeBandUpdate(index: number, previous: EqBand, next: EqBand): string {
  const changed = (Object.keys(next) as Array<keyof EqBand>).filter((key) => previous[key] !== next[key]);
  if (changed.length !== 1) return `Band ${index + 1} settings`;
  const field = changed[0];
  const names: Partial<Record<keyof EqBand, string>> = {
    enabled: "enabled state",
    type: "filter type",
    widthMode: "width mode",
    frequencyHz: "frequency",
    gainDb: "gain",
    bandwidthOctaves: "bandwidth",
    q: next.type === FilterType.Peaking ? "Q" : "slope",
  };
  return `Band ${index + 1} ${names[field] ?? "settings"}`;
}

function deviceError(context: string, reason: unknown): string {
  const detail = reason instanceof Error ? reason.message : "The DAC rejected the update";
  return `${context}: ${detail}`;
}

function filterName(type: FilterType): string {
  if (type === FilterType.LowShelf) return "Low shelf";
  if (type === FilterType.HighShelf) return "High shelf";
  return "Peaking";
}

export default function App() {
  const transport = useRef(new PumperHidTransport());
  const configRef = useRef<EqConfig>(cloneConfig(defaultConfig));
  const savedConfigRef = useRef<EqConfig | null>(null);
  const autoRef = useRef(false);
  const bandTimers = useRef(new Map<number, number>());
  const globalTimer = useRef<number | null>(null);
  const mounted = useRef(true);
  const expectedDisconnect = useRef(false);

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
  const [deviceDialog, setDeviceDialog] = useState<DeviceDialog>(null);
  const [deviceActionPending, setDeviceActionPending] = useState(false);
  const [writing, setWriting] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [connectionIssue, setConnectionIssue] = useState<string | null>(null);
  const [theme, setTheme] = useState<Theme>(initialTheme);

  const supported = PumperHidTransport.supported();
  const sampleRateHz = status?.sampleRateHz ?? 48000;
  const selectedProfileEmpty = (profileState.presentMask & (1 << selectedProfile)) === 0;
  const selectedProfileIsDefault = !selectedProfileEmpty && profileState.persistedProfile === selectedProfile;
  const hasUnsavedEdits = localDirty;
  const needsSave = hasUnsavedEdits || (connected && selectedProfileEmpty);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    document.documentElement.style.colorScheme = theme;
    document.querySelector<HTMLMetaElement>('meta[name="theme-color"]')?.setAttribute("content", theme === "dark" ? "#1d232a" : "#ffffff");
    try {
      window.localStorage.setItem(themeStorageKey, theme);
    } catch {
      // The selected theme still applies for the current page.
    }
  }, [theme]);

  useEffect(() => {
    if (!error) return;
    const timer = window.setTimeout(() => setError(null), 6000);
    return () => window.clearTimeout(timer);
  }, [error]);

  useEffect(() => {
    if (!notice) return;
    const timer = window.setTimeout(() => setNotice(null), 4000);
    return () => window.clearTimeout(timer);
  }, [notice]);

  const clearTimers = () => {
    bandTimers.current.forEach((timer) => window.clearTimeout(timer));
    bandTimers.current.clear();
    if (globalTimer.current !== null) window.clearTimeout(globalTimer.current);
    globalTimer.current = null;
  };

  const sendGlobalSoon = (next: EqConfig, context: string) => {
    if (!connected) return;
    if (!inRange(next.preampDb, -241, 12)) {
      setError(rangeMessage("Preamp gain", -241, 12, "dB"));
      return;
    }
    if (globalTimer.current !== null) window.clearTimeout(globalTimer.current);
    globalTimer.current = window.setTimeout(() => {
      transport.current.request(Opcode.SetGlobal, encodeGlobal(next)).catch((reason: unknown) => {
        setError(deviceError(context, reason));
      });
    }, 55);
  };

  const sendBandSoon = (index: number, band: EqBand, context: string) => {
    if (!connected) return;
    const validationError = validateBand(band, index);
    if (validationError) {
      setError(validationError);
      return;
    }
    const previous = bandTimers.current.get(index);
    if (previous !== undefined) window.clearTimeout(previous);
    bandTimers.current.set(
      index,
      window.setTimeout(() => {
        bandTimers.current.delete(index);
        transport.current.request(Opcode.SetBand, encodeBand(index, band)).catch((reason: unknown) => {
          setError(deviceError(context, reason));
        });
      }, 55),
    );
  };

  const updateDirtyState = (next: EqConfig) => {
    setLocalDirty(savedConfigRef.current === null || !configsEqual(next, savedConfigRef.current));
  };

  const commitConfig = (next: EqConfig, bandIndex?: number, globalChanged = false) => {
    const previous = configRef.current;
    if (autoRef.current) {
      next.preampDb = calculateAutoPreamp(next, sampleRateHz).preampDb;
      globalChanged = true;
    }
    const bandContext = bandIndex === undefined ? "" : describeBandUpdate(bandIndex, previous.bands[bandIndex], next.bands[bandIndex]);
    const globalContext = previous.preampDb !== next.preampDb && previous.enabled === next.enabled ? "Preamp gain" : "Global EQ";
    configRef.current = next;
    setConfig(next);
    updateDirtyState(next);
    setNotice(null);
    setError(null);
    if (bandIndex !== undefined) sendBandSoon(bandIndex, next.bands[bandIndex], bandContext);
    if (globalChanged) sendGlobalSoon(next, globalContext);
  };

  const updateBand = (index: number, patch: Partial<EqBand>) => {
    const next = cloneConfig(configRef.current);
    next.bands[index] = { ...next.bands[index], ...patch };
    commitConfig(next, index);
  };

  const updateBandType = (index: number, type: FilterType) => {
    const current = configRef.current.bands[index];
    const q = type === FilterType.Peaking ? current.q : Math.min(1, Math.max(0.1, current.q));
    updateBand(index, { type, q });
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
    const deviceConfig: EqConfig = { ...global, bands };
    let next = cloneConfig(deviceConfig);
    let autoChanged = false;
    if (autoRef.current) {
      const preampDb = calculateAutoPreamp(next, nextStatus.sampleRateHz).preampDb;
      autoChanged = preampDb !== next.preampDb;
      next = { ...next, preampDb };
      if (autoChanged) await transport.current.request(Opcode.SetGlobal, encodeGlobal(next));
    }
    configRef.current = next;
    if (knownStoredProfile || !nextStatus.dirty) savedConfigRef.current = cloneConfig(deviceConfig);
    setConfig(next);
    setStatus(nextStatus);
    setProfileState(nextProfiles);
    setSelectedProfile(nextProfiles.activeProfile);
    updateDirtyState(next);
    const validationError = validateConfig(next);
    if (validationError) setError(`The DAC returned an invalid configuration. ${validationError}`);
  }, []);

  const openDevice = useCallback(
    async (device: HIDDevice) => {
      setConnecting(true);
      setError(null);
      setConnectionIssue(null);
      try {
        savedConfigRef.current = null;
        setLocalDirty(false);
        await transport.current.open(device);
        transport.current.addEventListener("disconnect", () => {
          if (!mounted.current) return;
          const intentional = expectedDisconnect.current;
          expectedDisconnect.current = false;
          clearTimers();
          setConnected(false);
          setStatus(null);
          setMeterLevel(null);
          savedConfigRef.current = null;
          setLocalDirty(false);
          setProfileState((current) => ({ ...current, presentMask: 0, activeProfile: 0, persistedProfile: 0 }));
          setSelectedProfile(0);
          if (!intentional) setError("Pumper disconnected");
        }, { once: true });
        setConnected(true);
        await readDevice();
      } catch (reason) {
        await transport.current.close().catch(() => undefined);
        setConnected(false);
        setConnectionIssue(reason instanceof Error ? reason.message : "Unable to connect to Pumper");
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
      }).catch((reason: unknown) => {
        if (mounted.current) setConnectionIssue(reason instanceof Error ? reason.message : "Unable to access Pumper");
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
      .catch((reason: unknown) => setError(reason instanceof Error ? reason.message : "Unable to start level metering"));
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
      updateDirtyState(next);
      sendGlobalSoon(next, "Preamp gain");
    }
  }, [sampleRateHz]);

  const requestConnection = async () => {
    setConnecting(true);
    setError(null);
    setConnectionIssue(null);
    try {
      const device = await PumperHidTransport.requestDevice();
      if (device) {
        await openDevice(device);
      } else {
        setError("No Pumper DAC was selected");
      }
    } catch (reason) {
      setConnectionIssue(reason instanceof Error ? reason.message : "Device permission was not granted");
    } finally {
      setConnecting(false);
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
        const current = cloneConfig(configRef.current);
        const validationError = validateConfig(current);
        if (validationError) throw new Error(validationError);
        clearTimers();
        try {
          await transport.current.request(Opcode.SetGlobal, encodeGlobal(current));
        } catch (reason) {
          throw new Error(deviceError("Preamp gain", reason));
        }
        for (let index = 0; index < current.bands.length; index++) {
          try {
            await transport.current.request(Opcode.SetBand, encodeBand(index, current.bands[index]));
          } catch (reason) {
            throw new Error(deviceError(`Band ${index + 1}`, reason));
          }
        }
        await transport.current.request(Opcode.SaveProfile, new Uint8Array([selectedProfile]), 8000);
        await readDevice(true);
        setNotice(`Profile ${selectedProfile + 1} saved to flash`);
      } else if (action === "set-default") {
        await transport.current.request(Opcode.SetDefaultProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        setNotice(`Profile ${selectedProfile + 1} will load at power-on`);
      } else if (action === "clear") {
        await transport.current.request(Opcode.DeleteProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        savedConfigRef.current = cloneConfig(configRef.current);
        setLocalDirty(false);
        setNotice(`Profile ${selectedProfile + 1} cleared`);
      } else if (action === "defaults") {
        clearTimers();
        await transport.current.request(Opcode.RestoreDefaults);
        await readDevice();
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

  const confirmDeviceAction = async () => {
    if (deviceDialog !== "restart" && deviceDialog !== "bootsel") return;
    const action = deviceDialog;
    setDeviceActionPending(true);
    setError(null);
    clearTimers();
    expectedDisconnect.current = true;
    try {
      await transport.current.request(action === "bootsel" ? Opcode.EnterBootsel : Opcode.RestartDevice);
      window.setTimeout(() => { expectedDisconnect.current = false; }, 3000);
      if (action === "bootsel") {
        setDeviceDialog("bootsel-ready");
      } else {
        setDeviceDialog(null);
        setNotice("Pumper is restarting");
      }
    } catch (reason) {
      expectedDisconnect.current = false;
      setError(reason instanceof Error ? reason.message : "Unable to reset Pumper");
    } finally {
      setDeviceActionPending(false);
    }
  };

  const selected = config.bands[selectedBand] ?? config.bands[0];
  const clearingOnlyProfile = profileState.presentMask === (1 << selectedProfile);
  return (
    <main className="min-h-screen min-w-0 overflow-x-clip bg-base-200 text-base-content">
      <header className="sticky top-0 z-30 min-w-0 border-b border-base-300 bg-base-100/80 shadow-sm backdrop-blur-xl">
        <div className="navbar mx-auto min-h-16 min-w-0 max-w-[1600px] gap-3 px-4 lg:px-6">
          <div className="navbar-start min-w-0 w-auto flex-1">
            <div className="min-w-0">
              <p className="text-[11px] font-medium text-base-content/55">LightWolf</p>
              <h1 className="truncate text-lg leading-tight font-semibold">Pumper <span className="font-normal text-base-content/55">EQ controller</span></h1>
            </div>
          </div>
          <div className="navbar-end w-auto gap-2">
            {connected && (
              <button
                className="btn btn-sm"
                type="button"
                onClick={() => setDeviceDialog("info")}
                aria-haspopup="dialog"
                title="Device information"
              >
                DAC info
              </button>
            )}
            {!connected && supported && (
              <button className={primaryButton} onClick={requestConnection} disabled={connecting}>
                {connecting ? <span className="loading loading-spinner loading-sm" aria-hidden="true" /> : <Usb size={18} />}
                {connecting ? "Connecting..." : "Connect DAC"}
              </button>
            )}
            <div className="tooltip tooltip-bottom" data-tip={theme === "dark" ? "Use light theme" : "Use dark theme"}>
              <label className="btn btn-ghost btn-square btn-sm swap swap-rotate" title={theme === "dark" ? "Use light theme" : "Use dark theme"}>
                <input className="theme-controller" type="checkbox" value="dark" checked={theme === "dark"} onChange={(event) => setTheme(event.target.checked ? "dark" : "light")} aria-label={theme === "dark" ? "Use light theme" : "Use dark theme"} />
                <Sun className="swap-off" size={18} />
                <Moon className="swap-on" size={18} />
              </label>
            </div>
          </div>
        </div>

        <nav className="border-t border-base-200" aria-label="Profile and device actions">
          <div className="mx-auto flex max-w-[1600px] flex-wrap items-center gap-2 px-4 py-2.5 lg:px-6">
            <div className="flex w-full min-w-0 items-center gap-2 sm:w-auto">
              <span className="text-sm font-medium max-sm:sr-only">Profile</span>
              <SelectMenu
                className="w-full sm:w-48"
                label="Profile"
                value={selectedProfile}
                options={Array.from({ length: profileState.count }, (_, index) => ({
                  value: index,
                  label: `Profile ${index + 1}${(profileState.presentMask & (1 << index)) === 0 ? " (empty)" : index === profileState.persistedProfile ? " (default)" : ""}`,
                }))}
                onChange={(value) => { void selectProfile(value); }}
                disabled={!connected || writing}
              />
            </div>
            <div className="flex w-full items-center justify-between gap-2 sm:ml-auto sm:w-auto sm:justify-end">
              <div className="tooltip tooltip-bottom" data-tip="Clear profile">
                <button className={clearButton} onClick={() => setDialog("clear")} disabled={!connected || writing || hasUnsavedEdits || selectedProfileEmpty} aria-label={`Clear Profile ${selectedProfile + 1}`}>
                  <Trash2 size={17} />
                </button>
              </div>
              <button className={`${secondaryButton} max-sm:size-9 max-sm:p-0`} onClick={() => setDialog("set-default")} disabled={!connected || writing || hasUnsavedEdits || selectedProfileEmpty || selectedProfileIsDefault} title={selectedProfileIsDefault ? "Current power-on profile" : "Load this profile automatically at power-on"}>
                <Check size={17} />
                <span className="max-sm:sr-only">{selectedProfileIsDefault ? "Default" : "Make default"}</span>
              </button>
              <span className="h-6 w-px bg-base-300" aria-hidden="true" />
              <button className={`${secondaryButton} max-sm:size-9 max-sm:p-0`} onClick={() => setDialog("defaults")} disabled={!connected || writing} title="Restore compiled EQ defaults">
                <RotateCcw size={17} />
                <span className="max-sm:sr-only">Defaults</span>
              </button>
              <button className={`${connected ? primaryButton : secondaryButton} max-sm:size-9 max-sm:p-0`} onClick={() => setDialog("flash")} disabled={!connected || !needsSave || writing} title="Save profile">
                {writing ? <span className="loading loading-spinner loading-xs" aria-hidden="true" /> : <Save size={17} />}
                <span className="max-sm:sr-only">{writing ? "Working..." : "Save profile"}</span>
              </button>
            </div>
          </div>
        </nav>
      </header>

      {!supported && (
        <div className="mx-auto max-w-[1600px] px-4 pt-4 lg:px-6">
          <div className="alert alert-error" role="alert"><AlertTriangle size={19} /><span>WebHID is unavailable. Open this page in Chrome or Edge.</span></div>
        </div>
      )}

      <section className={`mx-auto grid min-w-0 max-w-[1600px] gap-4 px-4 py-4 lg:px-6 lg:py-6 ${connected && !writing ? "" : "pointer-events-none opacity-55"}`} aria-disabled={!connected || writing}>
        <div className="grid min-w-0 gap-4 xl:grid-cols-[minmax(0,1fr)_22rem]">
          <article className="card card-border min-w-0 overflow-hidden bg-base-100">
            <div className="card-body gap-3 p-4 sm:p-5">
              <div className="flex flex-wrap items-center justify-between gap-3">
                <h2 className="card-title text-base">Frequency response</h2>
                <div className="badge badge-ghost h-auto min-w-0 max-w-full gap-2 overflow-hidden py-1.5 text-xs font-medium">
                  <span className="grid size-5 shrink-0 place-items-center rounded-full text-[10px] font-bold text-black" style={{ backgroundColor: bandColors[selectedBand] }}>{selectedBand + 1}</span>
                  <span className="truncate">{filterName(selected.type)} · {selected.frequencyHz.toLocaleString()} Hz · {selected.gainDb > 0 ? "+" : ""}{selected.gainDb.toFixed(1)} dB</span>
                </div>
              </div>
              <EqGraph config={config} sampleRateHz={sampleRateHz} selectedBand={selectedBand} onSelectBand={setSelectedBand} onChangeBand={updateBand} />
            </div>
          </article>

          <aside className="card card-border min-w-0 bg-base-100">
            <div className="card-body gap-4 p-4 sm:p-5">
              <div className="flex items-center justify-between gap-3">
                <h2 className="card-title text-base">Global EQ</h2>
                <label className="label cursor-pointer gap-2 font-medium">
                  Enabled
                  <input className="toggle toggle-sm" type="checkbox" checked={config.enabled} onChange={(event) => updateGlobal({ enabled: event.target.checked })} disabled={!connected} />
                </label>
              </div>

              <div className="rounded-box bg-base-200 p-3">
                <div className="flex items-center justify-between gap-3">
                  <span className="text-sm font-semibold">Preamp</span>
                  <div className="join" role="group" aria-label="Preamp mode">
                    <button type="button" className={`btn btn-xs join-item min-w-14 ${autoPreamp ? "btn-ghost" : "btn-active"}`} onClick={() => toggleAuto(false)} disabled={!connected}>Manual</button>
                    <button type="button" className={`btn btn-xs join-item min-w-14 ${autoPreamp ? "btn-active" : "btn-ghost"}`} onClick={() => toggleAuto(true)} disabled={!connected}>Auto</button>
                  </div>
                </div>
                <div className="mt-3 flex items-center gap-3">
                  <input className={`${rangeClass} min-w-0 flex-1`} aria-label="Preamp gain slider" type="range" min="-24" max="12" step="0.1" value={Math.max(-24, config.preampDb)} onChange={(event) => updateGlobal({ preampDb: Number(event.target.value) })} disabled={!connected || autoPreamp} />
                  <label className="input input-sm flex w-24 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                    <NumericInput className="min-w-0 grow" label="Preamp gain" min={-241} max={12} step={0.1} value={config.preampDb} onChange={(preampDb) => updateGlobal({ preampDb })} onInvalid={() => setError(rangeMessage("Preamp gain", -241, 12, "dB"))} readOnly={!connected || autoPreamp} />
                    <span className="text-xs text-base-content/55">dB</span>
                  </label>
                </div>
              </div>

              <div className="stats stats-vertical w-full bg-base-200 shadow-none sm:stats-horizontal xl:stats-vertical">
                <div className="stat px-4 py-3"><span className="stat-title text-xs">Sample rate</span><strong className="stat-value text-lg">{(sampleRateHz / 1000).toFixed(sampleRateHz % 1000 ? 1 : 0)} kHz</strong></div>
                <div className="stat px-4 py-3"><span className="stat-title text-xs">Stream</span><strong className="stat-value text-lg">{status?.streaming ? "Streaming" : "Idle"}</strong></div>
                <div className="stat px-4 py-3"><span className="stat-title text-xs">Profile state</span><strong className={`stat-value text-lg ${needsSave ? "text-warning" : ""}`}>{selectedProfileEmpty && !hasUnsavedEdits ? "Empty slot" : needsSave ? "Unsaved" : "Saved"}</strong></div>
              </div>
            </div>
          </aside>
        </div>

        <LevelMeter level={meterLevel} />

        <section className="card card-border min-w-0 overflow-hidden bg-base-100">
          <div className="border-b border-base-200 p-4 sm:p-5">
            <h2 className="card-title text-base">Filter configuration</h2>
          </div>
          <div className="max-w-full overflow-x-auto">
            <table className="table table-sm w-full min-w-[930px] table-fixed">
              <colgroup>
                <col className="w-[11%]" />
                <col className="w-[16%]" />
                <col className="w-[14%]" />
                <col className="w-[28%]" />
                <col className="w-[16%]" />
                <col className="w-[15%]" />
              </colgroup>
              <thead><tr><th className="pl-4 sm:pl-5">Band</th><th>Filter</th><th>Frequency</th><th>Gain</th><th>Width</th><th className="pr-4 sm:pr-5">Value</th></tr></thead>
              <tbody>
                {config.bands.map((band, index) => {
                  const widthLabel = band.type === FilterType.Peaking ? (band.widthMode === WidthMode.Q ? "Q" : "Bandwidth") : "Slope";
                  const widthValue = band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? band.bandwidthOctaves : band.q;
                  const widthMaximum = band.type === FilterType.Peaking && band.widthMode === WidthMode.Q ? 20 : band.type === FilterType.Peaking ? 4 : 1;
                  const widthUnit = band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? "octaves" : "";
                  return (
                    <tr className={`transition-colors ${selectedBand === index ? "bg-base-200" : "hover:bg-base-200/60"} ${band.enabled ? "" : "text-base-content/40"}`} key={index} onClick={() => setSelectedBand(index)} onFocus={() => setSelectedBand(index)}>
                      <th className="pl-4 sm:pl-5" scope="row"><div className="flex items-center gap-3"><input className="toggle toggle-sm" type="checkbox" checked={band.enabled} onChange={(event) => updateBand(index, { enabled: event.target.checked })} aria-label={`Enable band ${index + 1}`} /><span className="grid size-6 place-items-center rounded-full text-[11px] font-bold text-black" style={{ backgroundColor: bandColors[index] }}>{index + 1}</span></div></th>
                      <td><SelectMenu className="w-full" value={band.type} options={filterOptions} onChange={(value) => updateBandType(index, value)} label={`Band ${index + 1} filter type`} /></td>
                      <td><label className="input input-sm flex w-full items-center gap-2 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={`Frequency for band ${index + 1}`} min={20} max={20000} step={1} value={band.frequencyHz} onChange={(frequencyHz) => updateBand(index, { frequencyHz })} onInvalid={() => setError(rangeMessage(`Band ${index + 1} frequency`, 20, 20000, "Hz"))} /><span className="text-xs text-base-content/55">Hz</span></label></td>
                      <td><div className="flex items-center gap-3"><input className={`${rangeClass} flex-1`} aria-label={`Gain slider for band ${index + 1}`} type="range" min="-24" max="24" step="0.1" value={band.gainDb} onChange={(event) => updateBand(index, { gainDb: Number(event.target.value) })} /><label className="input input-sm flex w-24 items-center gap-1 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={`Gain for band ${index + 1}`} min={-24} max={24} step={0.1} value={band.gainDb} onChange={(gainDb) => updateBand(index, { gainDb })} onInvalid={() => setError(rangeMessage(`Band ${index + 1} gain`, -24, 24, "dB"))} /><span className="text-xs text-base-content/55">dB</span></label></div></td>
                      <td>{band.type === FilterType.Peaking ? <SelectMenu className="w-full" value={band.widthMode} options={widthOptions} onChange={(value) => updateBand(index, { widthMode: value })} label={`Width mode for band ${index + 1}`} /> : <span className="text-sm text-base-content/55">Slope</span>}</td>
                      <td className="pr-4 sm:pr-5"><label className="input input-sm flex w-full items-center gap-1 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={`${widthLabel} for band ${index + 1}`} min={0.1} max={widthMaximum} step={0.01} value={widthValue} onChange={(value) => updateBand(index, band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? { bandwidthOctaves: value } : { q: value })} onInvalid={() => setError(rangeMessage(`Band ${index + 1} ${widthLabel.toLowerCase()}`, 0.1, widthMaximum, widthUnit))} /><span className="text-xs text-base-content/55">{band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? "oct" : widthLabel}</span></label></td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </section>

      </section>

      {(error || notice) && (
        <div className="toast toast-top toast-end z-[1000] mt-28 max-w-[calc(100vw-1rem)]">
          {error && <div className="alert alert-error w-96 max-w-[calc(100vw-2rem)] shadow-lg" role="alert"><AlertTriangle className="shrink-0" size={19} /><span className="min-w-0 break-words">{error}</span><button className="btn btn-ghost btn-square btn-xs ml-auto shrink-0 text-error-content" onClick={() => setError(null)} aria-label="Dismiss error"><X size={16} /></button></div>}
          {notice && <div className="alert alert-success w-96 max-w-[calc(100vw-2rem)] shadow-lg" role="status"><Check className="shrink-0" size={19} /><span className="min-w-0 break-words">{notice}</span><button className="btn btn-ghost btn-square btn-xs ml-auto shrink-0 text-success-content" onClick={() => setNotice(null)} aria-label="Dismiss message"><X size={16} /></button></div>}
        </div>
      )}

      {connectionIssue && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="connection-dialog-title">
          <div className="modal-box max-w-md">
            <div className="mb-4 grid size-10 place-items-center rounded-full bg-error/15 text-error"><AlertTriangle size={22} /></div>
            <h2 className="text-lg font-semibold" id="connection-dialog-title">Connection failed</h2>
            <p className="mt-2 text-sm leading-relaxed text-base-content/70">{connectionIssue}</p>

            <div className="modal-action">
              <button className={secondaryButton} onClick={() => setConnectionIssue(null)}>Close</button>
              <button className={primaryButton} onClick={() => { setConnectionIssue(null); void requestConnection(); }}>Try again</button>
            </div>
          </div>
          <button className="modal-backdrop" onClick={() => setConnectionIssue(null)} aria-label="Close connection dialog">close</button>
        </div>
      )}

      {deviceDialog && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="device-dialog-title">
          <div className="modal-box max-w-md">
            {deviceDialog === "info" ? (
              <>
                <div className="flex items-start justify-between gap-4">
                  <div className="min-w-0">
                    <h2 className="truncate text-lg font-semibold" id="device-dialog-title">{transport.current.productName}</h2>
                  </div>
                  <button className="btn btn-ghost btn-square btn-sm shrink-0" type="button" onClick={() => setDeviceDialog(null)} aria-label="Close device information">
                    <X size={18} />
                  </button>
                </div>

                <ul className="list mt-5 divide-y divide-base-300 rounded-box bg-base-200">
                  <li className="list-row items-center px-4 py-3"><span className="text-sm text-base-content/65">Firmware</span><strong className="text-right text-sm font-semibold">{status?.firmwareVersion ?? "-"}</strong></li>
                  <li className="list-row items-center px-4 py-3"><DiagnosticLabel label="Chip temperature" help="Approximate RP2350 junction temperature reported by its internal sensor; this is not the ambient temperature." /><strong className="text-right text-sm font-semibold">{status?.temperatureC == null ? "-" : `${status.temperatureC.toFixed(1)} °C`}</strong></li>
                  <li className="list-row items-center px-4 py-3"><DiagnosticLabel label="Active configuration version" help="The EQ settings revision currently running on the audio processor." /><strong className="text-right text-sm font-semibold">{status?.appliedGeneration ?? "-"}</strong></li>
                  <li className="list-row items-center px-4 py-3"><DiagnosticLabel label="Audio underruns" help="Audio frames replaced with silence because the output buffer ran empty." /><strong className="text-right text-sm font-semibold">{status?.underrunFrames.toLocaleString() ?? "-"}</strong></li>
                  <li className="list-row items-center px-4 py-3"><DiagnosticLabel label="Backpressure events" help="Times USB audio had to wait because every processing buffer was busy." /><strong className="text-right text-sm font-semibold">{status?.backpressureEvents.toLocaleString() ?? "-"}</strong></li>
                </ul>

                <div className="modal-action flex-col sm:flex-row">
                  <button className="btn btn-error btn-soft w-full sm:w-auto" type="button" onClick={() => setDeviceDialog("restart")}>
                    <Power size={17} /> Restart
                  </button>
                  <button className="btn btn-error btn-soft w-full sm:w-auto" type="button" onClick={() => setDeviceDialog("bootsel")}>
                    <Upload size={17} /> Firmware update
                  </button>
                </div>
              </>
            ) : (
              <>
                <div className={`mb-4 grid size-10 place-items-center rounded-full ${deviceDialog === "bootsel-ready" ? "bg-info/15 text-info" : "bg-error/15 text-error"}`}>
                  {deviceDialog === "bootsel-ready" ? <Upload size={22} /> : <AlertTriangle size={22} />}
                </div>
                <h2 className="text-lg font-semibold" id="device-dialog-title">
                  {deviceDialog === "restart" ? "Restart DAC?" : deviceDialog === "bootsel" ? "Enter BOOTSEL mode?" : "Firmware update"}
                </h2>
                <p className="mt-2 text-sm leading-relaxed text-base-content/70">
                  {deviceDialog === "restart"
                    ? "Audio and the controller will disconnect. Unsaved live EQ edits will be lost; stored profiles are unchanged."
                    : deviceDialog === "bootsel"
                      ? "Audio and the controller will disconnect, and unsaved live EQ edits will be lost. The DAC will appear as an RP2350 USB drive."
                      : <>Drag the firmware <strong>.uf2</strong> file onto the <strong>RP2350</strong> USB drive. The DAC restarts automatically after the copy completes.</>}
                </p>
                <div className="modal-action">
                  {deviceDialog === "bootsel-ready" ? (
                    <button className={secondaryButton} onClick={() => setDeviceDialog(null)}>Close</button>
                  ) : (
                    <>
                      <button className={secondaryButton} onClick={() => setDeviceDialog("info")} disabled={deviceActionPending}>Back</button>
                      <button className={`${buttonBase} btn-error`} onClick={confirmDeviceAction} disabled={deviceActionPending}>
                        {deviceActionPending && <span className="loading loading-spinner loading-xs" aria-hidden="true" />}
                        {deviceDialog === "restart" ? "Restart" : "Enter BOOTSEL"}
                      </button>
                    </>
                  )}
                </div>
              </>
            )}
          </div>
          <button className="modal-backdrop" onClick={() => { if (!deviceActionPending) setDeviceDialog(null); }} aria-label="Close device dialog">close</button>
        </div>
      )}

      {dialog && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="dialog-title">
          <div className="modal-box max-w-md">
            <div className="mb-4 grid size-10 place-items-center rounded-full bg-warning/15 text-warning"><AlertTriangle size={22} /></div>
            <h2 className="text-lg font-semibold" id="dialog-title">{dialog === "flash" ? `Save Profile ${selectedProfile + 1}?` : dialog === "clear" ? `Clear Profile ${selectedProfile + 1}?` : dialog === "set-default" ? `Make Profile ${selectedProfile + 1} default?` : dialog === "switch" ? "Discard unsaved changes?" : "Restore factory EQ?"}</h2>
            <p className="mt-2 text-sm leading-relaxed text-base-content/65">{dialog === "flash" ? "Audio may briefly pause while the DAC writes and verifies the profile bank." : dialog === "clear" ? clearingOnlyProfile ? "This is the last stored profile. All slots will be empty and the flat factory EQ will load at the next power-on." : selectedProfileIsDefault ? "This is the power-on default. The lowest-numbered remaining profile will become the new default." : "This stored profile will become empty. The current live EQ will keep playing until another profile is loaded." : dialog === "set-default" ? "Audio may briefly pause while the DAC updates flash. This profile will load automatically at power-on." : dialog === "switch" ? `The live edits will be replaced by Profile ${(pendingProfile ?? 0) + 1}.` : "The flat factory EQ will replace the live preview. It will remain unsaved until you save the profile."}</p>
            <div className="modal-action"><button className={secondaryButton} onClick={closeDialog}>Cancel</button><button className={dialog === "clear" ? `${buttonBase} btn-error` : primaryButton} onClick={confirmDialog}>{dialog === "flash" ? "Save profile" : dialog === "clear" ? "Clear profile" : dialog === "set-default" ? "Make default" : dialog === "switch" ? "Load profile" : "Restore defaults"}</button></div>
          </div>
          <button className="modal-backdrop" onClick={closeDialog} aria-label="Close dialog">close</button>
        </div>
      )}
    </main>
  );
}
