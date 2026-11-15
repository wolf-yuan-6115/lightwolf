import { t, locale } from "./i18n";
import { useCallback, useEffect, useRef, useState } from "react";
import { AlertTriangle, ChartSpline, Check, ClipboardCopy, ClipboardPaste, ListFilter, SlidersHorizontal, Info, Moon, Power, RotateCcw, Save, Sun, Trash2, Upload, Usb, X } from "lucide-react";
import { bandColors, EqGraph } from "./EqGraph";
import { calculateAutoPreamp } from "./eqMath";
import { parseEqText, serializeEqText } from "./eqText";
import { METER_REPORT_EVENT, PumperHidTransport } from "./hidTransport";
import { LevelMeter } from "./LevelMeter";
import { DeviceInfoPipeline } from "./DeviceInfoPipeline";
import { SaveStateBadge } from "./SaveStateBadge";
import { MetricTooltip } from "./MetricTooltip";
import { NumericInput } from "./NumericInput";
import { SelectMenu, type SelectMenuOption } from "./SelectMenu";
import { CrossfeedSettings, OutputProcessingSettings, UsbAudioState } from "./AudioSettings";
import { useAudioSettings } from "./useAudioSettings";
import {
  decodeBand,
  decodeGlobal,
  decodeMeterLevel,
  decodeProfileState,
  decodeStatus,
  decodeTelemetry,
  defaultConfig,
  DeviceStatus,
  DeviceTelemetry,
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
  supportsTelemetry,
  supportsFactoryReset,
  supportsSpectrum,
  WidthMode,
} from "./protocol";

type Dialog = "clear" | "flash" | "defaults" | "set-default" | "switch" | null;
type DeviceDialog = "info" | "factory-reset" | "restart" | "bootsel" | "bootsel-ready" | null;
type Theme = "light" | "dark";

interface PendingPreviewUpdate {
  payload: Uint8Array<ArrayBufferLike>;
  context: string;
}

interface PendingGlobalPreviewUpdate extends PendingPreviewUpdate {
  preampDb: number;
}

const buttonBase = "btn btn-sm min-h-10 sm:min-h-8 motion-safe:transition-[background-color,color,scale] motion-safe:duration-150 motion-safe:active:scale-[0.97]";
const primaryButton = `${buttonBase} btn-primary`;
const secondaryButton = buttonBase;
const clearButton = `${buttonBase} btn-square btn-error btn-soft shrink-0`;
const rangeClass = "range range-xs min-w-18";
const themeStorageKey = "pumper-theme";
const livePreviewIntervalMs = 55;
const legacyFilterOptions: readonly SelectMenuOption<FilterType>[] = [
  { value: FilterType.Peaking, label: t("Peaking"), description: t("Boost or cut around the selected frequency.") },
  { value: FilterType.LowShelf, label: t("Low shelf"), description: t("Boost or cut the low frequencies.") },
  { value: FilterType.HighShelf, label: t("High shelf"), description: t("Boost or cut the high frequencies.") },
];
const firmware3FilterOptions: readonly SelectMenuOption<FilterType>[] = [
  { value: FilterType.Peaking, label: t("Peaking"), description: t("Boost or cut around the selected frequency.") },
  { value: FilterType.LowPass, label: t("Low-pass"), description: t("Keep lower frequencies; reduce higher frequencies.") },
  { value: FilterType.LowShelf, label: t("Low shelf"), description: t("Boost or cut the low frequencies.") },
  { value: FilterType.HighPass, label: t("High-pass"), description: t("Keep higher frequencies; reduce lower frequencies.") },
  { value: FilterType.HighShelf, label: t("High shelf"), description: t("Boost or cut the high frequencies.") },
  { value: FilterType.Notch, label: t("Notch"), description: t("Remove a narrow range around the selected frequency.") },
  { value: FilterType.BandPass, label: t("Band-pass"), description: t("Keep a range around the selected frequency.") },
];
const widthOptions: readonly SelectMenuOption<WidthMode>[] = [
  { value: WidthMode.Bandwidth, label: t("Bandwidth"), description: t("Higher bandwidth affects a wider frequency range. One octave spans a doubling of frequency, such as 500 to 1,000 Hz.") },
  { value: WidthMode.Q, label: "Q", description: t("Higher Q narrows the affected frequency range.") },
];

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
  const format = (value: number) => Math.abs(value) >= 1000 ? value.toLocaleString(locale) : String(value);
  return t("{label} must be between {min} and {max}{unit}.", { label, min: format(minimum), max: format(maximum), unit: unit ? ` ${unit}` : "" });
}

function inRange(value: number, minimum: number, maximum: number): boolean {
  return Number.isFinite(value) && value >= minimum && value <= maximum;
}

function filterUsesGain(type: FilterType): boolean {
  return type <= FilterType.HighShelf;
}

function filterUsesQOnly(type: FilterType): boolean {
  return type >= FilterType.LowPass;
}

function validateBand(band: EqBand, index: number): string | null {
  const label = t("Band {number}", { number: index + 1 });
  if (!Number.isInteger(band.type) || band.type < FilterType.LowShelf || band.type > FilterType.BandPass) {
    return t("{label} filter type is not supported.", { label });
  }
  if (band.widthMode !== WidthMode.Q && band.widthMode !== WidthMode.Bandwidth) {
    return t("{label} width mode is not supported.", { label });
  }
  if (!inRange(band.frequencyHz, 20, 20000)) return rangeMessage(t("{label} frequency", { label }), 20, 20000, "Hz");
  if (!inRange(band.gainDb, -24, 24)) return rangeMessage(t("{label} gain", { label }), -24, 24, "dB");
  if (!inRange(band.bandwidthOctaves, 0.1, 4)) return rangeMessage(t("{label} bandwidth", { label }), 0.1, 4, t("octaves"));
  if (filterUsesQOnly(band.type) && band.widthMode !== WidthMode.Q) return t("{label} width mode must be Q.", { label });
  const qMaximum = band.type === FilterType.LowShelf || band.type === FilterType.HighShelf ? 1 : 20;
  const qLabel = band.type === FilterType.LowShelf || band.type === FilterType.HighShelf ? t("slope") : "Q";
  if (!inRange(band.q, 0.1, qMaximum)) return rangeMessage(t("{label} {field}", { label, field: qLabel }), 0.1, qMaximum);
  return null;
}

function validateConfig(config: EqConfig): string | null {
  if (!inRange(config.preampDb, -241, 12)) return rangeMessage(t("Preamp gain"), -241, 12, "dB");
  for (let index = 0; index < config.bands.length; index++) {
    const error = validateBand(config.bands[index], index);
    if (error) return error;
  }
  return null;
}

function describeBandUpdate(index: number, previous: EqBand, next: EqBand): string {
  const changed = (Object.keys(next) as Array<keyof EqBand>).filter((key) => previous[key] !== next[key]);
  if (changed.length !== 1) return t("Band {number} settings", { number: index + 1 });
  const field = changed[0];
  const names: Partial<Record<keyof EqBand, string>> = {
    enabled: t("enabled state"),
    type: t("filter type"),
    widthMode: t("width mode"),
    frequencyHz: t("frequency"),
    gainDb: t("gain"),
    bandwidthOctaves: t("bandwidth"),
    q: next.type === FilterType.LowShelf || next.type === FilterType.HighShelf ? t("slope") : "Q",
  };
  return t("Band {number} {field}", { number: index + 1, field: names[field] ?? t("settings") });
}

function deviceError(context: string, reason: unknown): string {
  const detail = reason instanceof Error ? reason.message : t("The DAC rejected the update");
  return `${context}: ${detail}`;
}

function filterName(type: FilterType): string {
  if (type === FilterType.LowShelf) return t("Low shelf");
  if (type === FilterType.HighShelf) return t("High shelf");
  if (type === FilterType.LowPass) return t("Low-pass");
  if (type === FilterType.HighPass) return t("High-pass");
  if (type === FilterType.Notch) return t("Notch");
  if (type === FilterType.BandPass) return t("Band-pass");
  return t("Peaking");
}

function FirmwareUpdateInstructions() {
  // Keep filenames and the drive name emphasized without splitting the sentence
  // into English-order fragments or interpreting translated text as HTML.
  return t("Drag the firmware .uf2 file onto the RP2350 USB drive. The DAC restarts automatically after the copy completes.")
    .split(/(\.uf2|RP2350)/)
    .map((part, index) => part === ".uf2" || part === "RP2350" ? <strong key={index}>{part}</strong> : part);
}

export default function App() {
  useEffect(() => {
    document.documentElement.lang = locale;
    document.querySelector<HTMLMetaElement>('meta[name="description"]')?.setAttribute("content", t("LightWolf Pumper USB DAC equalizer controller"));
  }, []);

  const transport = useRef(new PumperHidTransport());
  const configRef = useRef<EqConfig>(cloneConfig(defaultConfig));
  const savedConfigRef = useRef<EqConfig | null>(null);
  const autoRef = useRef(false);
  const previewTimer = useRef<number | null>(null);
  const pendingGlobalPreview = useRef<PendingGlobalPreviewUpdate | null>(null);
  const pendingBandPreviews = useRef(new Map<number, PendingPreviewUpdate>());
  const submittedPreampDb = useRef(defaultConfig.preampDb);
  const mounted = useRef(true);
  const expectedDisconnect = useRef(false);

  const [config, setConfig] = useState<EqConfig>(() => cloneConfig(defaultConfig));
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [telemetry, setTelemetry] = useState<DeviceTelemetry | null>(null);
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
  const [importDialogOpen, setImportDialogOpen] = useState(false);
  const [importText, setImportText] = useState("");
  const [importError, setImportError] = useState<string | null>(null);
  const [deviceDialog, setDeviceDialog] = useState<DeviceDialog>(null);
  const [deviceActionPending, setDeviceActionPending] = useState(false);
  const [writing, setWriting] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [connectionIssue, setConnectionIssue] = useState<string | null>(null);
  const [theme, setTheme] = useState<Theme>(initialTheme);

  const audioSettings = useAudioSettings(transport.current, connected, status?.firmwareVersion, setError, setNotice);

  const supported = PumperHidTransport.supported();
  const sampleRateHz = status?.sampleRateHz ?? 48000;
  const selectedProfileEmpty = (profileState.presentMask & (1 << selectedProfile)) === 0;
  const selectedProfileIsDefault = !selectedProfileEmpty && profileState.persistedProfile === selectedProfile;
  const hasUnsavedEdits = localDirty;
  const needsSave = hasUnsavedEdits || (connected && selectedProfileEmpty);
  const filterOptions = audioSettings.outputSupported ? firmware3FilterOptions : legacyFilterOptions;

  useEffect(() => {
    document.documentElement.dataset.theme = theme === "dark" ? "mocha" : "latte";
    document.documentElement.style.colorScheme = theme;
    document.querySelector<HTMLMetaElement>('meta[name="theme-color"]')?.setAttribute("content", theme === "dark" ? "#1e1e2e" : "#eff1f5");
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
    if (previewTimer.current !== null) window.clearTimeout(previewTimer.current);
    previewTimer.current = null;
    pendingGlobalPreview.current = null;
    pendingBandPreviews.current.clear();
  };

  const schedulePreviewFlush = () => {
    if (previewTimer.current !== null) return;
    previewTimer.current = window.setTimeout(() => {
      previewTimer.current = null;
      const global = pendingGlobalPreview.current;
      const bands = Array.from(pendingBandPreviews.current.entries()).sort(([left], [right]) => left - right);
      pendingGlobalPreview.current = null;
      pendingBandPreviews.current.clear();

      const sendGlobal = () => {
        if (!global) return;
        transport.current.request(Opcode.SetGlobal, global.payload).catch((reason: unknown) => {
          setError(deviceError(global.context, reason));
        });
      };
      const sendBands = () => {
        bands.forEach(([, update]) => {
          transport.current.request(Opcode.SetBand, update.payload).catch((reason: unknown) => {
            setError(deviceError(update.context, reason));
          });
        });
      };

      const attenuating = global !== null && global.preampDb < submittedPreampDb.current;
      if (attenuating) sendGlobal();
      sendBands();
      if (!attenuating) sendGlobal();
      if (global) submittedPreampDb.current = global.preampDb;
    }, livePreviewIntervalMs);
  };

  const sendGlobalSoon = (next: EqConfig, context: string) => {
    if (!connected) return;
    if (!inRange(next.preampDb, -241, 12)) {
      setError(rangeMessage(t("Preamp gain"), -241, 12, "dB"));
      return;
    }
    pendingGlobalPreview.current = { payload: encodeGlobal(next), preampDb: next.preampDb, context };
    schedulePreviewFlush();
  };

  const sendBandSoon = (index: number, band: EqBand, context: string) => {
    if (!connected) return;
    const validationError = validateBand(band, index);
    if (validationError) {
      setError(validationError);
      return;
    }
    pendingBandPreviews.current.set(index, { payload: encodeBand(index, band), context });
    schedulePreviewFlush();
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
    const globalContext = previous.preampDb !== next.preampDb && previous.enabled === next.enabled ? t("Preamp gain") : t("Global EQ");
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
    const shelf = type === FilterType.LowShelf || type === FilterType.HighShelf;
    const q = shelf ? Math.min(1, Math.max(0.1, current.q)) : current.q;
    updateBand(index, { type, q, ...(filterUsesQOnly(type) ? { widthMode: WidthMode.Q } : {}) });
  };

  const updateGlobal = (patch: Partial<Pick<EqConfig, "enabled" | "preampDb">>) => {
    const next = { ...cloneConfig(configRef.current), ...patch };
    commitConfig(next, undefined, true);
  };

  const readDevice = useCallback(async (knownStoredProfile = false) => {
    const hello = await transport.current.request(Opcode.Hello);
    const nextStatus = decodeStatus(hello.payload);
    const nextTelemetry = supportsTelemetry(nextStatus.firmwareVersion)
      ? decodeTelemetry((await transport.current.request(Opcode.GetTelemetry)).payload)
      : null;
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
    submittedPreampDb.current = next.preampDb;
    if (knownStoredProfile || !nextStatus.dirty) savedConfigRef.current = cloneConfig(deviceConfig);
    setConfig(next);
    setStatus(nextStatus);
    setTelemetry(nextTelemetry);
    setProfileState(nextProfiles);
    setSelectedProfile(nextProfiles.activeProfile);
    updateDirtyState(next);
    const validationError = validateConfig(next);
    if (validationError) setError(t("The DAC returned an invalid configuration. {error}", { error: validationError }));
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
          setTelemetry(null);
          setMeterLevel(null);
          savedConfigRef.current = null;
          setLocalDirty(false);
          setProfileState((current) => ({ ...current, presentMask: 0, activeProfile: 0, persistedProfile: 0 }));
          setSelectedProfile(0);
          if (!intentional) setError(t("Pumper disconnected"));
        }, { once: true });
        setConnected(true);
        await readDevice();
      } catch (reason) {
        await transport.current.close().catch(() => undefined);
        setConnected(false);
        setTelemetry(null);
        setConnectionIssue(reason instanceof Error ? reason.message : t("Unable to connect to Pumper"));
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
        if (mounted.current) setConnectionIssue(reason instanceof Error ? reason.message : t("Unable to access Pumper"));
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
        .then(async (response) => {
          const nextStatus = decodeStatus(response.payload);
          setStatus(nextStatus);
          if (supportsTelemetry(nextStatus.firmwareVersion)) {
            const nextTelemetry = await transport.current.request(Opcode.GetTelemetry);
            setTelemetry(decodeTelemetry(nextTelemetry.payload));
          } else {
            setTelemetry(null);
          }
        })
        .catch(() => undefined);
      void audioSettings.refresh();
    }, 1000);
    return () => window.clearInterval(timer);
  }, [connected, audioSettings.refresh]);

  const spectrumSupported = !!status && supportsSpectrum(status.firmwareVersion);

  useEffect(() => {
    if (!connected) return;
    let alive = true;
    let heartbeatPending = false;

    const handleMeterReport = (event: Event) => {
      const response = (event as CustomEvent<ResponsePacket>).detail;
      setMeterLevel(decodeMeterLevel(response.payload));
    };
    const heartbeat = async () => {
      if (!alive || heartbeatPending) return;
      heartbeatPending = true;
      try {
        await transport.current.request(Opcode.MeterKeepalive).catch(() => undefined);
        if (alive && spectrumSupported) {
          await transport.current.request(Opcode.SpectrumKeepalive).catch(() => {
            if (alive) return transport.current.request(Opcode.SpectrumStart).catch(() => undefined);
          });
        }
      } finally { heartbeatPending = false; }
    };

    transport.current.addEventListener(METER_REPORT_EVENT, handleMeterReport);
    transport.current
      .request(Opcode.MeterStart, encodeMeterConfig(METER_REPORT_INTERVAL_MS, METER_TIMEOUT_MS))
      .catch((reason: unknown) => setError(reason instanceof Error ? reason.message : t("Unable to start level metering")));
    if (spectrumSupported) {
      void transport.current.request(Opcode.SpectrumStart).catch(() => undefined);
    }
    const heartbeatTimer = window.setInterval(heartbeat, METER_HEARTBEAT_INTERVAL_MS);
    const handleVisibility = () => {
      if (!document.hidden) void heartbeat();
    };
    document.addEventListener("visibilitychange", handleVisibility);

    return () => {
      alive = false;
      window.clearInterval(heartbeatTimer);
      document.removeEventListener("visibilitychange", handleVisibility);
      transport.current.removeEventListener(METER_REPORT_EVENT, handleMeterReport);
      setMeterLevel(null);
      void transport.current.request(Opcode.MeterStop).catch(() => undefined);
      if (spectrumSupported) void transport.current.request(Opcode.SpectrumStop).catch(() => undefined);
    };
  }, [connected, spectrumSupported]);

  useEffect(() => {
    if (!autoRef.current) return;
    const next = cloneConfig(configRef.current);
    const calculated = calculateAutoPreamp(next, sampleRateHz).preampDb;
    if (next.preampDb !== calculated) {
      next.preampDb = calculated;
      configRef.current = next;
      setConfig(next);
      updateDirtyState(next);
      sendGlobalSoon(next, t("Preamp gain"));
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
        setError(t("No Pumper DAC was selected"));
      }
    } catch (reason) {
      setConnectionIssue(reason instanceof Error ? reason.message : t("Device permission was not granted"));
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
    setNotice(t("Profile {number} loaded into live preview", { number: index + 1 }));
  };

  const selectProfile = async (index: number) => {
    if (index === selectedProfile) return;
    if ((profileState.presentMask & (1 << index)) === 0) {
      setSelectedProfile(index);
      setNotice(t("Profile {number} is empty; save to store the current EQ there", { number: index + 1 }));
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
      setError(reason instanceof Error ? reason.message : t("Unable to load profile"));
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
          throw new Error(deviceError(t("Preamp gain"), reason));
        }
        for (let index = 0; index < current.bands.length; index++) {
          try {
            await transport.current.request(Opcode.SetBand, encodeBand(index, current.bands[index]));
          } catch (reason) {
            throw new Error(deviceError(t("Band {number}", { number: index + 1 }), reason));
          }
        }
        await transport.current.request(Opcode.SaveProfile, new Uint8Array([selectedProfile]), 8000);
        await readDevice(true);
        setNotice(t("Profile {number} saved to flash", { number: selectedProfile + 1 }));
      } else if (action === "set-default") {
        await transport.current.request(Opcode.SetDefaultProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        setNotice(t("Profile {number} will load at power-on", { number: selectedProfile + 1 }));
      } else if (action === "clear") {
        await transport.current.request(Opcode.DeleteProfile, new Uint8Array([selectedProfile]), 8000);
        const nextProfiles = decodeProfileState((await transport.current.request(Opcode.GetProfiles)).payload);
        setProfileState(nextProfiles);
        savedConfigRef.current = cloneConfig(configRef.current);
        setLocalDirty(false);
        setNotice(t("Profile {number} cleared", { number: selectedProfile + 1 }));
      } else if (action === "defaults") {
        clearTimers();
        await transport.current.request(Opcode.RestoreDefaults);
        await readDevice();
        setNotice(t("Factory EQ loaded into live preview"));
      } else if (action === "switch" && pendingProfile !== null) {
        await loadProfile(pendingProfile);
      }
      if (action === "defaults") {
        const response = await transport.current.request(Opcode.GetStatus);
        setStatus(decodeStatus(response.payload));
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : t("The operation failed"));
    } finally {
      setPendingProfile(null);
      setWriting(false);
    }
  };

  const closeDialog = () => {
    setDialog(null);
    setPendingProfile(null);
  };

  const copyEqText = async () => {
    try {
      await navigator.clipboard.writeText(serializeEqText(configRef.current));
      setError(null);
      setNotice(t("Parametric EQ text copied"));
    } catch {
      setError(t("Unable to copy EQ text. Allow clipboard access and try again."));
    }
  };

  const openImportDialog = () => {
    setImportText("");
    setImportError(null);
    setImportDialogOpen(true);
  };

  const importEqText = () => {
    try {
      const next = parseEqText(importText, status?.bandCount ?? configRef.current.bands.length);
      if (!audioSettings.outputSupported && next.bands.some((band) => band.type > FilterType.HighShelf)) {
        throw new Error(t("Low-pass, high-pass, notch, and band-pass filters require firmware 3.0 or newer."));
      }
      const validationError = validateConfig(next);
      if (validationError) throw new Error(validationError);
      clearTimers();
      autoRef.current = false;
      setAutoPreamp(false);
      configRef.current = next;
      setConfig(next);
      updateDirtyState(next);
      setSelectedBand(0);
      sendGlobalSoon(next, t("Imported global EQ"));
      next.bands.forEach((band, index) => sendBandSoon(index, band, t("Imported band {number}", { number: index + 1 })));
      setImportDialogOpen(false);
      setImportError(null);
      setError(null);
      setNotice(t("Parametric EQ imported into live preview"));
    } catch (reason) {
      setImportError(reason instanceof Error ? reason.message : t("Unable to import EQ text"));
    }
  };

  const confirmDeviceAction = async () => {
    if (deviceActionPending || (deviceDialog !== "restart" && deviceDialog !== "bootsel" && deviceDialog !== "factory-reset")) return;
    if (deviceDialog === "factory-reset" && !supportsFactoryReset(status?.firmwareVersion ?? "")) return;
    const action = deviceDialog;
    setDeviceActionPending(true);
    setError(null);
    clearTimers();
    audioSettings.cancelPendingPreviews();
    expectedDisconnect.current = true;
    try {
      await transport.current.request(action === "factory-reset" ? Opcode.FactoryReset : action === "bootsel" ? Opcode.EnterBootsel : Opcode.RestartDevice);
      window.setTimeout(() => { expectedDisconnect.current = false; }, 3000);
      if (action === "bootsel") {
        setDeviceDialog("bootsel-ready");
      } else {
        setDeviceDialog(null);
        setNotice(action === "factory-reset" ? t("Factory reset complete. Pumper is restarting.") : t("Pumper is restarting"));
      }
    } catch (reason) {
      expectedDisconnect.current = false;
      setError(reason instanceof Error ? reason.message : t("Unable to reset Pumper"));
    } finally {
      setDeviceActionPending(false);
    }
  };

  const selected = config.bands[selectedBand] ?? config.bands[0];
  const clearingOnlyProfile = profileState.presentMask === (1 << selectedProfile);
  return (
    <main className="min-h-screen min-w-0 overflow-x-clip bg-base-200 text-base-content">
      <header className="sticky top-0 z-30 min-w-0 border-b border-base-300 bg-base-100">
        <div className="mx-auto grid min-h-16 min-w-0 max-w-[1440px] grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-x-5 px-4 py-3 lg:px-6">
          <h1 className="text-lg font-semibold tracking-tight">Pumper<span className="sr-only"> {t("EQ controller")}</span></h1>
          <nav className="flex min-w-0 flex-wrap items-center gap-x-4 gap-y-2 border-l border-base-300 pl-5" aria-label={t("Profile and device actions")}>
            <div className="flex shrink-0 items-center gap-2">
              <SelectMenu
                className="w-48"
                label={t("Profile")}
                value={selectedProfile}
                options={Array.from({ length: profileState.count }, (_, index) => ({
                  value: index,
                  label: t((profileState.presentMask & (1 << index)) === 0 ? "Profile {number} (empty)" : index === profileState.persistedProfile ? "Profile {number} (default)" : "Profile {number}", { number: index + 1 }),
                }))}
                onChange={(value) => { void selectProfile(value); }}
                disabled={!connected || connecting || writing || audioSettings.saving}
              />
              <SaveStateBadge state={selectedProfileEmpty && !hasUnsavedEdits ? "Empty slot" : needsSave ? "Unsaved" : "Saved"} label={t("EQ profile save state")} />
            </div>
            <div className="ml-auto flex shrink-0 items-center gap-2">
              <div className="tooltip tooltip-bottom" data-tip={t("Clear profile")}>
                <button className={clearButton} onClick={() => setDialog("clear")} disabled={!connected || connecting || writing || audioSettings.saving || hasUnsavedEdits || selectedProfileEmpty} aria-label={t("Clear Profile {number}", { number: selectedProfile + 1 })}>
                  <Trash2 size={17} />
                </button>
              </div>
              <button className={secondaryButton} onClick={() => setDialog("set-default")} disabled={!connected || connecting || writing || audioSettings.saving || hasUnsavedEdits || selectedProfileEmpty || selectedProfileIsDefault} title={selectedProfileIsDefault ? t("Current power-on profile") : t("Load this profile automatically at power-on")}>
                <Check size={17} />
                <span>{selectedProfileIsDefault ? t("Default") : t("Make default")}</span>
              </button>
              <span className="h-6 w-px bg-base-300" aria-hidden="true" />
              <button className={secondaryButton} onClick={() => setDialog("defaults")} disabled={!connected || connecting || writing || audioSettings.saving} title={t("Restore compiled EQ defaults")}>
                <RotateCcw size={17} />
                <span>{t("Defaults")}</span>
              </button>
              <button className={connected ? primaryButton : secondaryButton} onClick={() => setDialog("flash")} disabled={!connected || !needsSave || writing || audioSettings.saving} title={t("Save profile")}>
                {writing ? <span className="loading loading-spinner loading-xs" aria-hidden="true" /> : <Save size={17} />}
                <span>{writing ? t("Working...") : t("Save profile")}</span>
              </button>
            </div>
          </nav>
          <div className="flex shrink-0 items-center gap-2 border-l border-base-300 pl-4">
            {connected && (
              <button
                className={secondaryButton}
                type="button"
                onClick={() => setDeviceDialog("info")}
                aria-haspopup="dialog"
                title={t("Device information")}
              >
                <Info size={16} aria-hidden="true" /> {t("DAC info")}
              </button>
            )}
            {!connected && supported && (
              <button className={primaryButton} onClick={requestConnection} disabled={connecting}>
                {connecting ? <span className="loading loading-spinner loading-sm" aria-hidden="true" /> : <Usb size={18} />}
                {connecting ? t("Connecting...") : t("Connect DAC")}
              </button>
            )}
            <div className="tooltip tooltip-bottom" data-tip={theme === "dark" ? t("Use light theme") : t("Use dark theme")}>
              <label className="btn btn-ghost btn-square btn-sm swap swap-rotate" title={theme === "dark" ? t("Use light theme") : t("Use dark theme")}>
                <input type="checkbox" checked={theme === "dark"} onChange={(event) => setTheme(event.target.checked ? "dark" : "light")} aria-label={theme === "dark" ? t("Use light theme") : t("Use dark theme")} />
                <Sun className="swap-off" size={18} />
                <Moon className="swap-on" size={18} />
              </label>
            </div>
          </div>
        </div>
      </header>

      {!supported && (
        <div className="mx-auto max-w-[1440px] px-4 pt-4 lg:px-6">
          <div className="alert alert-error" role="alert"><AlertTriangle size={19} /><span>{t("WebHID is unavailable. Open this page in Chrome or Edge.")}</span></div>
        </div>
      )}

      {!connected && supported && (
        <div className="mx-auto max-w-[1440px] px-4 pt-5 lg:px-6">
          <div className="flex items-center gap-3 rounded-xl border border-primary/25 bg-primary/5 px-4 py-3 text-sm">
            <Usb size={20} className="shrink-0 text-primary" aria-hidden="true" />
            <div><p className="font-medium">{connecting ? t("Reading your DAC’s settings…") : t("Connect a DAC to start editing.")}</p><p className="mt-0.5 text-xs text-base-content/70">{t("Preview live. Save when it sounds right.")}</p></div>
          </div>
        </div>
      )}

      <section className={`mx-auto grid min-w-0 max-w-[1440px] gap-4 px-4 py-4 lg:gap-5 lg:px-6 lg:py-6 ${connected && !connecting && !writing && !audioSettings.saving ? "" : "pointer-events-none"}`} aria-disabled={!connected || connecting || writing || audioSettings.saving}>
        <fieldset className="contents" disabled={!connected || connecting || writing || audioSettings.saving}>
        <div className="grid min-w-0 gap-4 xl:grid-cols-[minmax(0,1fr)_19rem]">
          <article className="flex min-w-0 flex-col overflow-hidden rounded-2xl border border-base-300 bg-base-100">
            <div className="flex flex-wrap items-center justify-between gap-3 px-4 pt-5 sm:px-6 sm:pt-6">
              <h2 className="flex items-center gap-2.5 text-xl font-semibold tracking-tight sm:text-2xl"><ChartSpline size={22} className="text-accent" aria-hidden="true" />{t("Frequency response")}</h2>
              <span className="flex items-center gap-2 text-xs font-medium text-base-content/70"><span className={`size-1.5 rounded-full ${!connected ? "bg-base-content/40" : config.enabled ? "bg-success" : "bg-warning"}`} aria-hidden="true" />{!connected ? t("DAC disconnected") : config.enabled ? t("EQ enabled") : t("EQ bypassed")}</span>
            </div>
            <div className="relative mx-1 mt-4 min-h-[280px] flex-1 sm:mx-3 sm:min-h-[350px]">
              <EqGraph spectrumTransport={transport.current} spectrumEnabled={connected && spectrumSupported} config={config} sampleRateHz={sampleRateHz} selectedBand={selectedBand} onSelectBand={setSelectedBand} onChangeBand={updateBand} interactive={connected && !connecting && !writing && !audioSettings.saving} />
            </div>
            <div className="mt-auto px-4 pb-5 sm:px-6">
              <div className="mb-3 flex flex-wrap items-center gap-x-4 gap-y-2 text-xs text-base-content/70" aria-label={t("Graph legend")}>
                <span className="flex items-center gap-1.5"><span className="w-4 border-t-2 border-accent" aria-hidden="true" />{t("Combined EQ")}</span>
                {config.enabled && selected.enabled && <span className="flex items-center gap-1.5"><span className="w-4 border-t border-dashed" style={{ borderColor: bandColors[selectedBand] }} aria-hidden="true" />{t("Selected filter")}</span>}
                {connected && spectrumSupported && <span className="flex items-center gap-1.5"><span className="size-2 rounded-sm bg-info/50" aria-hidden="true" />{t("Output · dBFS")}</span>}
              </div>
              <div className="grid grid-cols-10 gap-1.5" role="group" aria-label={t("Select EQ band")}>
                {config.bands.map((band, index) => <button type="button" key={index} aria-label={t("Select band {number}", { number: index + 1 })} aria-pressed={selectedBand === index} onClick={() => setSelectedBand(index)} className={`flex min-h-11 min-w-0 items-center justify-center gap-1.5 rounded-lg border text-xs font-semibold motion-safe:transition-colors ${selectedBand === index ? "border-base-content/40 bg-base-200 text-base-content" : "border-transparent text-base-content/70 hover:bg-base-200"}`}>
                  <span className={`size-2 shrink-0 rounded-full ${band.enabled ? "" : "opacity-35"}`} style={{ backgroundColor: bandColors[index] }} aria-hidden="true" />{index + 1}
                </button>)}
              </div>
              <div className="mt-4 flex flex-wrap items-center justify-between gap-x-5 gap-y-3 border-t border-base-300 pt-4">
                <div className="flex items-center gap-2 text-sm font-semibold"><span className="size-2 rounded-full" style={{ backgroundColor: bandColors[selectedBand] }} aria-hidden="true" />{t("Band {number}", { number: selectedBand + 1 })}<span className="font-normal text-base-content/70">{filterName(selected.type)}{selected.enabled ? "" : t(" · Off")}</span></div>
                <dl className="flex gap-5 text-sm tabular-nums">
                  <div><dt className="sr-only">{t("Selected band frequency")}</dt><dd className="font-semibold">{selected.frequencyHz.toLocaleString(locale)} <span className="text-xs font-normal text-base-content/70">Hz</span></dd></div>
                  <div><dt className="sr-only">{filterUsesGain(selected.type) ? t("Selected band gain") : t("Selected band Q")}</dt><dd className="font-semibold">{filterUsesGain(selected.type) ? `${selected.gainDb > 0 ? "+" : ""}${selected.gainDb.toFixed(1)}` : selected.q.toFixed(2)} <span className="text-xs font-normal text-base-content/70">{filterUsesGain(selected.type) ? "dB" : "Q"}</span></dd></div>
                </dl>
              </div>
              <p className="mt-3 text-xs text-base-content/70">{connected ? t("Drag a point to shape the EQ. Arrow keys fine-tune; Shift takes larger steps.") : t("The response includes all enabled bands and preamp.")}</p>
            </div>
          </article>

          <aside className="flex min-w-0 flex-col overflow-hidden rounded-2xl border border-base-300 bg-base-100">
            <div className="card-body flex-none gap-4 p-4 sm:p-5">
              <div className="flex items-center justify-between gap-3">
                <h2 className="card-title text-base"><SlidersHorizontal size={18} aria-hidden="true" />{t("Global EQ")}</h2>
                <label className="label cursor-pointer gap-2 font-medium">
                  {t("Enabled")}
                  <input className="toggle toggle-sm" type="checkbox" checked={config.enabled} onChange={(event) => updateGlobal({ enabled: event.target.checked })} disabled={!connected} />
                </label>
              </div>

              <div className="border-t border-base-300 pt-4">
                <div className="flex items-center justify-between gap-3">
                  <span className="flex items-center gap-1 text-sm font-semibold">{t("Preamp")}<MetricTooltip label={t("Preamp")} help={t("Adjust the level before the EQ filters. Auto lowers it as needed to leave room for EQ boosts.")} /></span>
                  <div className="join" role="group" aria-label={t("Preamp mode")}>
                    <button type="button" className={`btn btn-xs join-item min-h-10 min-w-16 sm:min-h-8 ${autoPreamp ? "btn-ghost" : "btn-active"}`} aria-pressed={!autoPreamp} onClick={() => toggleAuto(false)} disabled={!connected}>{t("Manual")}</button>
                    <button type="button" className={`btn btn-xs join-item min-h-10 min-w-16 sm:min-h-8 ${autoPreamp ? "btn-active" : "btn-ghost"}`} aria-pressed={autoPreamp} onClick={() => toggleAuto(true)} disabled={!connected}>{t("Auto")}</button>
                  </div>
                </div>
                <div className="mt-3 flex items-center gap-3">
                  <input className={`${rangeClass} min-w-0 flex-1`} aria-label={t("Preamp gain slider")} type="range" min="-24" max="12" step="0.1" value={Math.max(-24, config.preampDb)} onChange={(event) => updateGlobal({ preampDb: Number(event.target.value) })} disabled={!connected || autoPreamp} />
                  <label className="input input-sm min-h-10 sm:min-h-8 flex w-24 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                    <NumericInput className="min-w-0 grow" label={t("Preamp gain")} min={-241} max={12} step={0.1} value={config.preampDb} onChange={(preampDb) => updateGlobal({ preampDb })} onInvalid={() => setError(rangeMessage(t("Preamp gain"), -241, 12, "dB"))} readOnly={!connected || autoPreamp} />
                    <span className="text-xs text-base-content/70">dB</span>
                  </label>
                </div>
              </div>

              <UsbAudioState settings={audioSettings} connected={connected} busy={writing || connecting || deviceActionPending} sampleRateHz={sampleRateHz} bitDepth={status?.bitDepth ?? null} streaming={status?.streaming ?? false} />
            </div>
            <LevelMeter level={meterLevel} />
          </aside>
        </div>

        <section className="min-w-0 overflow-hidden rounded-2xl border border-base-300 bg-base-100">
          <div className="flex flex-wrap items-center justify-between gap-3 border-b border-base-200 p-4 sm:p-5">
            <h2 className="card-title text-base"><ListFilter size={18} aria-hidden="true" />{t("Filter configuration")}</h2>
            <div className="flex items-center gap-2">
              <button type="button" className={secondaryButton} onClick={() => { void copyEqText(); }} disabled={!connected} title={t("Copy current EQ as text")}>
                <ClipboardCopy size={16} aria-hidden="true" /> {t("Copy EQ")}
              </button>
              <button type="button" className={secondaryButton} onClick={openImportDialog} disabled={!connected} title={t("Import parametric EQ text")}>
                <ClipboardPaste size={16} aria-hidden="true" /> {t("Import EQ")}
              </button>
            </div>
          </div>
          <div className="max-w-full overflow-x-auto">
            <table className="table table-sm w-full min-w-[1000px] table-fixed" aria-label={t("EQ band settings")} role="table">
              <colgroup className="table-column-group">
                <col className="w-[11%]" />
                <col className="w-[16%]" />
                <col className="w-[14%]" />
                <col className="w-[28%]" />
                <col className="w-[16%]" />
                <col className="w-[15%]" />
              </colgroup>
              <thead className="text-xs text-base-content/70" role="rowgroup"><tr role="row"><th className="pl-4 sm:pl-5">{t("Band")}</th><th>{t("Filter")}</th><th>{t("Frequency")}</th><th><span className="inline-flex items-center gap-1">{t("Gain")}<MetricTooltip label={t("Gain")} help={t("Positive gain boosts the selected frequencies; negative gain reduces them.")} /></span></th><th>{t("Width")}</th><th className="pr-4 sm:pr-5">{t("Value")}</th></tr></thead>
              <tbody role="rowgroup">
                {config.bands.map((band, index) => {
                  const qOnly = filterUsesQOnly(band.type);
                  const usesGain = filterUsesGain(band.type);
                  const widthLabel = qOnly ? "Q" : band.type === FilterType.Peaking ? (band.widthMode === WidthMode.Q ? "Q" : t("Bandwidth")) : t("Slope");
                  const widthHelp = band.type === FilterType.LowShelf || band.type === FilterType.HighShelf
                    ? t("Higher slope makes the shelf transition steeper; lower values make it gentler.")
                    : band.type === FilterType.LowPass || band.type === FilterType.HighPass
                      ? t("Higher Q emphasizes the response near the cutoff frequency.")
                      : band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth
                        ? t("Higher bandwidth affects a wider frequency range. One octave spans a doubling of frequency, such as 500 to 1,000 Hz.")
                        : t("Higher Q narrows the affected frequency range.");
                  const widthValue = band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? band.bandwidthOctaves : band.q;
                  const widthMaximum = qOnly || (band.type === FilterType.Peaking && band.widthMode === WidthMode.Q) ? 20 : band.type === FilterType.Peaking ? 4 : 1;
                  const widthUnit = band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? t("octaves") : "";
                  return (
                    <tr role="row" className={`border-b border-base-200 motion-safe:transition-colors ${selectedBand === index ? "bg-base-200" : "hover:bg-base-200/60"} ${band.enabled ? "" : "text-base-content/65"}`} key={index} onClick={() => setSelectedBand(index)} onFocus={() => setSelectedBand(index)}>
                      <th className="!py-3 !pl-5 !pr-2" scope="row" role="rowheader"><div className="flex items-center gap-3"><input className="toggle toggle-sm" type="checkbox" checked={band.enabled} onChange={(event) => updateBand(index, { enabled: event.target.checked })} aria-label={t("Enable band {number}", { number: index + 1 })} /><span className="grid size-6 place-items-center rounded-full text-[11px] font-bold text-black" style={{ backgroundColor: bandColors[index] }}>{index + 1}</span><span className="sr-only">{t("Band {number}", { number: index + 1 })}</span></div></th>
                      <td className="min-w-0 !p-2" role="cell"><SelectMenu className="w-full" value={band.type} options={filterOptions} onChange={(value) => updateBandType(index, value)} label={t("Band {number} filter type", { number: index + 1 })} /></td>
                      <td className="min-w-0 !p-2" role="cell"><span className="sr-only">{t("Frequency")}</span><label className="input input-sm min-h-10 sm:min-h-8 flex w-full items-center gap-2 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={t("Frequency for band {number}", { number: index + 1 })} min={20} max={20000} step={1} value={band.frequencyHz} onChange={(frequencyHz) => updateBand(index, { frequencyHz })} onInvalid={() => setError(rangeMessage(t("Band {number} frequency", { number: index + 1 }), 20, 20000, "Hz"))} /><span className="text-xs text-base-content/70">Hz</span></label></td>
                      <td className="min-w-0 !p-2" role="cell"><span className="sr-only">{t("Gain")}</span><div className={`flex items-center gap-3 ${usesGain ? "" : "opacity-45"}`} title={usesGain ? undefined : t("Gain is not used by this filter type")}><input className={`${rangeClass} flex-1`} aria-label={t("Gain slider for band {number}", { number: index + 1 })} type="range" min="-24" max="24" step="0.1" value={band.gainDb} disabled={!usesGain} onChange={(event) => updateBand(index, { gainDb: Number(event.target.value) })} /><label className="input input-sm min-h-10 sm:min-h-8 flex w-24 shrink-0 items-center gap-1 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={t("Gain for band {number}", { number: index + 1 })} min={-24} max={24} step={0.1} value={band.gainDb} readOnly={!usesGain} onChange={(gainDb) => updateBand(index, { gainDb })} onInvalid={() => setError(rangeMessage(t("Band {number} gain", { number: index + 1 }), -24, 24, "dB"))} /><span className="text-[10px] text-base-content/70">{usesGain ? "dB" : t("N/A")}</span></label></div></td>
                      <td className="min-w-0 !p-2" role="cell"><span className="sr-only">{t("Width")}</span>{band.type === FilterType.Peaking ? <SelectMenu className="w-full" value={band.widthMode} options={widthOptions} onChange={(value) => updateBand(index, { widthMode: value })} label={t("Width mode for band {number}", { number: index + 1 })} /> : <span className="inline-flex items-center gap-1 text-sm text-base-content/70">{qOnly ? "Q" : t("Slope")}<MetricTooltip label={widthLabel} help={widthHelp} /></span>}</td>
                      <td className="min-w-0 !py-3 !pl-2 !pr-5" role="cell"><span className="sr-only">{widthLabel}</span><label className="input input-sm min-h-10 sm:min-h-8 flex w-full items-center gap-1 has-[input[aria-invalid=true]]:input-error"><NumericInput className="min-w-0 grow" label={t("{field} for band {number}", { field: widthLabel, number: index + 1 })} min={0.1} max={widthMaximum} step={0.01} value={widthValue} onChange={(value) => updateBand(index, band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? { bandwidthOctaves: value } : { q: value })} onInvalid={() => setError(rangeMessage(t("Band {number} {field}", { number: index + 1, field: widthLabel.toLowerCase() }), 0.1, widthMaximum, widthUnit))} /><span className="text-xs text-base-content/70">{band.type === FilterType.Peaking && band.widthMode === WidthMode.Bandwidth ? t("oct") : qOnly || band.type === FilterType.Peaking ? "Q" : ""}</span></label></td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </section>

        <div className="grid min-w-0 gap-4 lg:grid-cols-2">
          <CrossfeedSettings settings={audioSettings} connected={connected} busy={writing || connecting || deviceActionPending || audioSettings.saving} onError={setError} />
          <OutputProcessingSettings settings={audioSettings} connected={connected} busy={writing || connecting || deviceActionPending || audioSettings.saving} onError={setError} />
        </div>
        </fieldset>
      </section>

      {(error || notice) && (
        <div className="toast toast-top toast-end z-[1000] mt-28 max-w-[calc(100vw-1rem)]">
          {error && <div className="alert alert-error w-96 max-w-[calc(100vw-2rem)] shadow-lg" role="alert"><AlertTriangle className="shrink-0" size={19} /><span className="min-w-0 break-words">{error}</span><button className="btn btn-ghost btn-square btn-xs ml-auto shrink-0 text-error-content" onClick={() => setError(null)} aria-label={t("Dismiss error")}><X size={16} /></button></div>}
          {notice && <div className="alert alert-success w-96 max-w-[calc(100vw-2rem)] shadow-lg" role="status"><Check className="shrink-0" size={19} /><span className="min-w-0 break-words">{notice}</span><button className="btn btn-ghost btn-square btn-xs ml-auto shrink-0 text-success-content" onClick={() => setNotice(null)} aria-label={t("Dismiss message")}><X size={16} /></button></div>}
        </div>
      )}

      {connectionIssue && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="connection-dialog-title">
          <div className="modal-box max-w-md">
            <div className="mb-4 grid size-10 place-items-center rounded-full bg-error/15 text-error"><AlertTriangle size={22} /></div>
            <h2 className="text-lg font-semibold" id="connection-dialog-title">{t("Connection failed")}</h2>
            <p className="mt-2 text-sm leading-relaxed text-base-content/70">{connectionIssue}</p>

            <div className="modal-action">
              <button className={secondaryButton} onClick={() => setConnectionIssue(null)}>{t("Close")}</button>
              <button className={primaryButton} onClick={() => { setConnectionIssue(null); void requestConnection(); }}>{t("Try again")}</button>
            </div>
          </div>
          <button className="modal-backdrop" onClick={() => setConnectionIssue(null)} aria-label={t("Close connection dialog")}>{t("close")}</button>
        </div>
      )}

      {importDialogOpen && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="import-eq-title">
          <div className="modal-box max-w-2xl">
            <h2 className="text-lg font-semibold" id="import-eq-title">{t("Import parametric EQ")}</h2>
            <p className="mt-2 text-sm leading-relaxed text-base-content/65">
              {t("Paste Equalizer APO-style settings. Imported filters replace all ten live bands; omitted bands are disabled. Nothing is written to flash until you save the profile.")}
            </p>
            <textarea
              className={`textarea mt-4 h-72 w-full resize-y font-mono text-xs leading-relaxed ${importError ? "textarea-error" : "textarea-bordered"}`}
              value={importText}
              onChange={(event) => { setImportText(event.target.value); setImportError(null); }}
              placeholder={"Preamp: -6 dB\nFilter 1: ON PK Fc 100 Hz Gain 3 dB Q 1.4"}
              aria-label={t("Parametric EQ text")}
              aria-invalid={importError ? "true" : undefined}
              autoFocus
            />
            {importError && <p className="mt-2 text-sm text-error" role="alert">{importError}</p>}
            <p className="mt-2 text-xs text-base-content/70">{t("Types: PK, LS, HS, LP, HP, NO, BP. Use Q for width, or BW with peaking filters.")}</p>
            <div className="modal-action">
              <button className={secondaryButton} onClick={() => setImportDialogOpen(false)}>{t("Cancel")}</button>
              <button className={primaryButton} onClick={importEqText} disabled={!importText.trim()}>{t("Import to preview")}</button>
            </div>
          </div>
          <button className="modal-backdrop" onClick={() => setImportDialogOpen(false)} aria-label={t("Close import dialog")}>{t("close")}</button>
        </div>
      )}

      {deviceDialog && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="device-dialog-title">
          <div className={`modal-box ${deviceDialog === "info" ? "!w-[calc(100vw-2rem)] !max-w-4xl overflow-x-hidden" : "max-w-md"}`}>
            {deviceDialog === "info" ? (
              <>
                <div className="flex items-start justify-between gap-4">
                  <div className="min-w-0">
                    <h2 className="truncate text-lg font-semibold" id="device-dialog-title">{transport.current.productName}</h2>
                  </div>
                  <button className="btn btn-ghost btn-square btn-sm shrink-0" type="button" onClick={() => setDeviceDialog(null)} aria-label={t("Close device information")}>
                    <X size={18} />
                  </button>
                </div>

                <DeviceInfoPipeline status={status} telemetry={telemetry} eq={config} audio={audioSettings.audio} crossfeed={audioSettings.crossfeed?.live ?? null} outputProcessing={audioSettings.outputProcessing?.live ?? null} />


                <div className="modal-action flex-col sm:flex-row">
                  <div>
                    <button className="btn btn-error btn-soft w-full" type="button" onClick={() => setDeviceDialog("factory-reset")} disabled={writing || audioSettings.saving || deviceActionPending || !supportsFactoryReset(status?.firmwareVersion ?? "")}>
                      <Trash2 size={17} /> {t("Factory reset")}
                    </button>
                    {!supportsFactoryReset(status?.firmwareVersion ?? "") && <p className="mt-1 text-xs text-base-content/70">{t("Requires firmware 4.2 or newer")}</p>}
                  </div>
                  <button className="btn btn-error btn-soft w-full sm:w-auto" type="button" onClick={() => setDeviceDialog("restart")} disabled={writing || audioSettings.saving}>
                    <Power size={17} /> {t("Restart")}
                  </button>
                  <button className="btn btn-error btn-soft w-full sm:w-auto" type="button" onClick={() => setDeviceDialog("bootsel")} disabled={writing || audioSettings.saving}>
                    <Upload size={17} /> {t("Firmware update")}
                  </button>
                </div>
              </>
            ) : (
              <>
                <div className={`mb-4 grid size-10 place-items-center rounded-full ${deviceDialog === "bootsel-ready" ? "bg-info/15 text-info" : "bg-error/15 text-error"}`}>
                  {deviceDialog === "bootsel-ready" ? <Upload size={22} /> : <AlertTriangle size={22} />}
                </div>
                <h2 className="text-lg font-semibold" id="device-dialog-title">
                  {deviceDialog === "factory-reset" ? t("Factory reset Pumper?") : deviceDialog === "restart" ? t("Restart DAC?") : deviceDialog === "bootsel" ? t("Enter BOOTSEL mode?") : t("Firmware update")}
                </h2>
                <p className="mt-2 text-sm leading-relaxed text-base-content/70">
                  {deviceDialog === "factory-reset" ? t("This permanently deletes all saved EQ profiles, the default profile selection, crossfeed settings, and output-processing settings. Audio will stop and Pumper will restart with factory defaults. This cannot be undone.") : deviceDialog === "restart"
                    ? t("Audio and the controller will disconnect. Unsaved live EQ, crossfeed, and output-processing edits will be lost; saved settings are unchanged.")
                    : deviceDialog === "bootsel"
                      ? t("Audio and the controller will disconnect, and unsaved live EQ, crossfeed, and output-processing edits will be lost. The DAC will appear as an RP2350 USB drive.")
                      : <FirmwareUpdateInstructions />}
                </p>
                <div className="modal-action">
                  {deviceDialog === "bootsel-ready" ? (
                    <button className={secondaryButton} onClick={() => setDeviceDialog(null)}>{t("Close")}</button>
                  ) : (
                    <>
                      <button className={secondaryButton} onClick={() => setDeviceDialog("info")} disabled={deviceActionPending}>{deviceDialog === "factory-reset" ? t("Cancel") : t("Back")}</button>
                      <button className={`${buttonBase} btn-error`} onClick={confirmDeviceAction} disabled={deviceActionPending}>
                        {deviceActionPending && <span className="loading loading-spinner loading-xs" aria-hidden="true" />}
                        {deviceDialog === "factory-reset" ? t("Factory reset") : deviceDialog === "restart" ? t("Restart") : t("Enter BOOTSEL")}
                      </button>
                    </>
                  )}
                </div>
              </>
            )}
          </div>
          <button className="modal-backdrop" onClick={() => { if (!deviceActionPending) setDeviceDialog(null); }} aria-label={t("Close device dialog")}>{t("close")}</button>
        </div>
      )}

      {dialog && (
        <div className="modal modal-open" role="dialog" aria-modal="true" aria-labelledby="dialog-title">
          <div className="modal-box max-w-md">
            <div className="mb-4 grid size-10 place-items-center rounded-full bg-warning/15 text-warning"><AlertTriangle size={22} /></div>
            <h2 className="text-lg font-semibold" id="dialog-title">{dialog === "flash" ? t("Save Profile {number}?", { number: selectedProfile + 1 }) : dialog === "clear" ? t("Clear Profile {number}?", { number: selectedProfile + 1 }) : dialog === "set-default" ? t("Make Profile {number} default?", { number: selectedProfile + 1 }) : dialog === "switch" ? t("Discard unsaved changes?") : t("Restore factory EQ?")}</h2>
            <p className="mt-2 text-sm leading-relaxed text-base-content/65">{dialog === "flash" ? t("Audio may briefly pause while the DAC writes and verifies the profile bank.") : dialog === "clear" ? clearingOnlyProfile ? t("This is the last stored profile. All slots will be empty and the flat factory EQ will load at the next power-on.") : selectedProfileIsDefault ? t("This is the power-on default. The lowest-numbered remaining profile will become the new default.") : t("This stored profile will become empty. The current live EQ will keep playing until another profile is loaded.") : dialog === "set-default" ? t("Audio may briefly pause while the DAC updates flash. This profile will load automatically at power-on.") : dialog === "switch" ? t("The live edits will be replaced by Profile {number}.", { number: (pendingProfile ?? 0) + 1 }) : t("The flat factory EQ will replace the live preview. It will remain unsaved until you save the profile.")}</p>
            <div className="modal-action"><button className={secondaryButton} onClick={closeDialog}>{t("Cancel")}</button><button className={dialog === "clear" ? `${buttonBase} btn-error` : primaryButton} onClick={confirmDialog}>{dialog === "flash" ? t("Save profile") : dialog === "clear" ? t("Clear profile") : dialog === "set-default" ? t("Make default") : dialog === "switch" ? t("Load profile") : t("Restore defaults")}</button></div>
          </div>
          <button className="modal-backdrop" onClick={closeDialog} aria-label={t("Close dialog")}>{t("close")}</button>
        </div>
      )}
    </main>
  );
}
