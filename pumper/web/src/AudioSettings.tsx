import { Activity, Clock3, Headphones, Save, Usb, Volume2, VolumeX } from "lucide-react";
import { NumericInput } from "./NumericInput";
import { SelectMenu } from "./SelectMenu";
import { SaveStateBadge } from "./SaveStateBadge";
import { CrossfeedMode, defaultCrossfeed, type AudioChannelControl, type CrossfeedConfig } from "./protocol";
import type { useAudioSettings } from "./useAudioSettings";

const modeOptions = [
  { value: CrossfeedMode.Off, label: "Off" },
  { value: CrossfeedMode.Low, label: "Low" },
  { value: CrossfeedMode.Medium, label: "Medium" },
  { value: CrossfeedMode.High, label: "High" },
  { value: CrossfeedMode.Custom, label: "Custom" },
];

function ChannelState({ label, control }: { label: string; control: AudioChannelControl | null }) {
  return (
    <div className="flex min-w-0 items-center justify-between gap-3 rounded-box bg-base-200 px-3 py-2.5">
      <span className="flex items-center gap-2 text-sm text-base-content/65">
        {control?.muted ? <VolumeX size={16} aria-hidden="true" /> : <Volume2 size={16} aria-hidden="true" />}{label}
      </span>
      <span className="flex items-center gap-2 text-sm font-semibold tabular-nums">
        {control ? <>{control.muted ? "Muted · " : ""}{control.volumeDb.toLocaleString("en-US", { maximumFractionDigits: 2 })} dB</> : "—"}
      </span>
    </div>
  );
}

interface Props {
  settings: ReturnType<typeof useAudioSettings>;
  connected: boolean;
  busy: boolean;
  onError: (message: string) => void;
}

export function UsbAudioState({ settings, connected, busy, sampleRateHz, streaming }: Omit<Props, "onError"> & { sampleRateHz: number; streaming: boolean }) {
  const { audio, supported } = settings;
  const unavailable = !connected ? "Connect DAC to view audio settings" : !supported ? "Requires firmware 2.2" : null;
  return (
    <section className="grid min-w-0 gap-3 border-t border-base-200 pt-4" aria-labelledby="usb-audio-title">
      <h3 className="flex items-center gap-2 text-sm font-semibold" id="usb-audio-title"><Usb size={16} aria-hidden="true" />USB audio</h3>
      <dl className="grid min-w-0 gap-3">
        <div className="flex min-w-0 items-center justify-between gap-3 rounded-box bg-base-200 px-3 py-2.5">
          <dt className="flex items-center gap-2 text-sm text-base-content/65"><Clock3 size={16} aria-hidden="true" />Sample rate</dt>
          <dd className="text-sm font-semibold tabular-nums">{(sampleRateHz / 1000).toFixed(sampleRateHz % 1000 ? 1 : 0)} kHz</dd>
        </div>
        <div className="flex min-w-0 items-center justify-between gap-3 rounded-box bg-base-200 px-3 py-2.5">
          <dt className="flex items-center gap-2 text-sm text-base-content/65"><Activity size={16} aria-hidden="true" />Stream state</dt>
          <dd className="text-sm font-semibold">{streaming ? "Streaming" : "Idle"}</dd>
        </div>
      </dl>
      <div className="grid min-w-0 gap-2" role="group" aria-label="Host USB audio controls" aria-disabled={!!unavailable || busy}>
        <ChannelState label="Master volume" control={audio?.master ?? null} />
      </div>
      {unavailable && <p className="grow-0 text-xs text-base-content/55">{unavailable}</p>}
      {!unavailable && !audio && <p className="grow-0 text-xs text-base-content/55">Reading USB audio state…</p>}
    </section>
  );
}

const presetParameters = {
  [CrossfeedMode.Off]: { strengthPercent: 0, cutoffHz: 700, delayMs: 0 },
  [CrossfeedMode.Low]: { strengthPercent: 10, cutoffHz: 700, delayMs: 0.2 },
  [CrossfeedMode.Medium]: { strengthPercent: 20, cutoffHz: 700, delayMs: 0.25 },
  [CrossfeedMode.High]: { strengthPercent: 30, cutoffHz: 700, delayMs: 0.3 },
};

export function CrossfeedSettings({ settings, connected, busy, onError }: Props) {
  const { crossfeed, supported, saving } = settings;
  const unavailable = !connected ? "Connect DAC to view audio settings" : !supported ? "Requires firmware 2.2" : null;
  const disabled = !connected || !supported || busy || saving || crossfeed === null;
  const live = crossfeed?.live ?? defaultCrossfeed;
  const displayed = live.mode === CrossfeedMode.Custom ? live : presetParameters[live.mode];
  const parametersDisabled = disabled || live.mode !== CrossfeedMode.Custom;
  const customControls: Array<{ key: keyof Pick<CrossfeedConfig, "strengthPercent" | "cutoffHz" | "delayMs">; label: string; min: number; max: number; step: number; unit: string }> = [
    { key: "strengthPercent", label: "Crossfeed strength", min: 0, max: 40, step: 1, unit: "%" },
    { key: "cutoffHz", label: "Crossfeed cutoff", min: 300, max: 2000, step: 10, unit: "Hz" },
    { key: "delayMs", label: "Crossfeed delay", min: 0, max: 0.6, step: 0.01, unit: "ms" },
  ];

  return (
    <section className="card card-border min-w-0 bg-base-100" aria-labelledby="crossfeed-title">
      <div className="card-body gap-4 p-4 sm:p-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="card-title text-base" id="crossfeed-title"><Headphones size={18} aria-hidden="true" />Headphone crossfeed</h2>
          {crossfeed && <SaveStateBadge state={crossfeed.dirty ? "Unsaved" : "Saved"} label="Crossfeed save state" />}
        </div>
        {unavailable && <p className="grow-0 text-xs text-base-content/55">{unavailable}</p>}
        {!unavailable && !crossfeed && <p className="grow-0 text-xs text-base-content/55">Reading crossfeed settings…</p>}
        <fieldset className="grid min-w-0 gap-4 md:grid-cols-3" disabled={disabled}>
          <div className="flex flex-wrap items-center justify-between gap-3 md:col-span-3">
            <span className="text-sm font-semibold">Mode</span>
            <SelectMenu className="w-36" label="Crossfeed mode" value={live.mode} options={modeOptions} disabled={disabled} onChange={(mode) => settings.update({ mode })} />
          </div>
          {customControls.map(({ key, label, min, max, step, unit }) => (
            <div className="grid min-w-0 gap-2" key={key}>
              <span className="text-sm font-semibold">{label.replace("Crossfeed ", "").replace(/^./, (letter) => letter.toUpperCase())}</span>
              <div className="flex min-w-0 items-center gap-3">
                <input className="range range-xs min-w-18 flex-1" type="range" aria-label={`${label} slider`} min={min} max={max} step={step} value={displayed[key]} disabled={parametersDisabled} onChange={(event) => { if (!parametersDisabled) settings.update({ [key]: Number(event.target.value) }); }} />
                <label className="input input-sm flex w-28 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                  <NumericInput className="min-w-0 grow" label={label} min={min} max={max} step={step} value={displayed[key]} readOnly={parametersDisabled} onChange={(value) => { if (!parametersDisabled) settings.update({ [key]: value }); }} onInvalid={() => onError(`${label} must be between ${min.toLocaleString("en-US")} and ${max.toLocaleString("en-US")} ${unit}.`)} />
                  <span className="text-xs text-base-content/55">{unit}</span>
                </label>
              </div>
            </div>
          ))}
        </fieldset>
        <div className="mt-auto flex flex-wrap items-center justify-end gap-3 border-t border-base-200 pt-4">
          <button className="btn btn-primary btn-sm" disabled={disabled || !crossfeed?.dirty} onClick={() => void settings.save()}>
            {saving ? <span className="loading loading-spinner loading-xs" aria-hidden="true" /> : <Save size={16} aria-hidden="true" />}
            <span>{saving ? "Saving…" : "Save crossfeed"}</span>
          </button>
        </div>
      </div>
    </section>
  );
}
