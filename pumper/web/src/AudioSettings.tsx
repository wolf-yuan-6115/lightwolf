import { t, locale } from "./i18n";
import { Activity, AudioLines, Clock3, Headphones, Save, SlidersHorizontal, Usb, Volume2, VolumeX } from "lucide-react";
import { MetricTooltip } from "./MetricTooltip";
import { NumericInput } from "./NumericInput";
import { SelectMenu } from "./SelectMenu";
import { SaveStateBadge } from "./SaveStateBadge";
import { CrossfeedMode, defaultCrossfeed, defaultOutputProcessing, type AudioChannelControl, type CrossfeedConfig, type OutputProcessingConfig } from "./protocol";
import type { useAudioSettings } from "./useAudioSettings";
import { crossfeedParameters } from "./crossfeedParameters";

const modeOptions = [
  { value: CrossfeedMode.Off, label: t("Off") },
  { value: CrossfeedMode.Low, label: t("Low") },
  { value: CrossfeedMode.Medium, label: t("Medium") },
  { value: CrossfeedMode.High, label: t("High") },
  { value: CrossfeedMode.Custom, label: t("Custom") },
];

function ChannelState({ label, control, pinned = false }: { label: string; control: AudioChannelControl | null; pinned?: boolean }) {
  return (
    <div className="flex min-w-0 items-center justify-between gap-3 border-b border-base-200 py-2.5">
      <span className="flex items-center gap-2 text-sm text-base-content/70">
        {control?.muted ? <VolumeX size={16} aria-hidden="true" /> : <Volume2 size={16} aria-hidden="true" />}{label}
      </span>
      <span className="flex items-center gap-2 text-sm font-semibold tabular-nums">
        {control ? <>{control.muted ? t("Muted · ") : ""}{pinned ? t("Pinned") : `${control.volumeDb.toLocaleString(locale, { maximumFractionDigits: 2 })} dB`}</> : "—"}
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

export function UsbAudioState({ settings, connected, busy, sampleRateHz, bitDepth, streaming }: Omit<Props, "onError"> & { sampleRateHz: number; bitDepth: number | null; streaming: boolean }) {
  const { audio, supported } = settings;
  const unavailable = !connected ? t("Connect DAC to view audio settings") : !supported ? t("Requires firmware 2.2") : null;
  return (
    <section className="grid min-w-0 gap-3 border-t border-base-200 pt-4" aria-labelledby="usb-audio-title">
      <h3 className="flex items-center gap-2 text-sm font-semibold" id="usb-audio-title"><Usb size={16} aria-hidden="true" />{t("USB audio")}</h3>
      <dl className="grid min-w-0 gap-1">
        <div className="flex min-w-0 items-center justify-between gap-3 border-b border-base-200 py-2.5">
          <dt className="flex items-center gap-2 text-sm text-base-content/70"><Clock3 size={16} aria-hidden="true" />{t("Sample rate")}</dt>
          <dd className="text-sm font-semibold tabular-nums">{connected ? `${(sampleRateHz / 1000).toFixed(sampleRateHz % 1000 ? 1 : 0)} kHz` : "—"}</dd>
        </div>
        {bitDepth !== null && <div className="flex min-w-0 items-center justify-between gap-3 border-b border-base-200 py-2.5">
          <dt className="flex items-center gap-2 text-sm text-base-content/70"><AudioLines size={16} aria-hidden="true" />{t("Bit depth")}</dt>
          <dd className="text-sm font-semibold tabular-nums">{bitDepth}-bit</dd>
        </div>}
        <div className="flex min-w-0 items-center justify-between gap-3 border-b border-base-200 py-2.5">
          <dt className="flex items-center gap-2 text-sm text-base-content/70"><Activity size={16} aria-hidden="true" />{t("Stream state")}</dt>
          <dd className="text-sm font-semibold">{!connected ? "—" : streaming ? t("Streaming") : t("Idle")}</dd>
        </div>
      </dl>
      <div className="grid min-w-0 gap-2" role="group" aria-label={t("Host USB audio controls")} aria-disabled={!!unavailable || busy}>
        <ChannelState pinned={settings.acknowledgedPin} label={t("Master volume")} control={audio?.master ?? null} />
      </div>
      {unavailable && <p className="grow-0 text-xs text-base-content/70">{unavailable}</p>}
      {!unavailable && !audio && <p className="grow-0 text-xs text-base-content/70">{t("Reading USB audio state…")}</p>}
    </section>
  );
}

export function CrossfeedSettings({ settings, connected, busy, onError }: Props) {
  const { crossfeed, supported, savingCrossfeed: saving } = settings;
  const unavailable = !connected ? t("Connect DAC to view audio settings") : !supported ? t("Requires firmware 2.2") : null;
  const disabled = !connected || !supported || busy || saving || crossfeed === null;
  const live = crossfeed?.live ?? defaultCrossfeed;
  const displayed = crossfeedParameters(live);
  const parametersDisabled = disabled || live.mode !== CrossfeedMode.Custom;
  const customControls: Array<{ key: keyof Pick<CrossfeedConfig, "strengthPercent" | "cutoffHz" | "delayMs">; label: string; shortLabel: string; help: string; min: number; max: number; step: number; unit: string }> = [
    { key: "strengthPercent", label: t("Crossfeed strength"), shortLabel: t("Strength"), help: t("How much of the opposite channel is mixed in."), min: 0, max: 40, step: 1, unit: "%" },
    { key: "cutoffHz", label: t("Crossfeed cutoff"), shortLabel: t("Cutoff"), help: t("Sets the frequency range mixed into the opposite channel; higher frequencies are mixed in less."), min: 300, max: 2000, step: 10, unit: "Hz" },
    { key: "delayMs", label: t("Crossfeed delay"), shortLabel: t("Delay"), help: t("Delay added to the sound mixed in from the opposite channel."), min: 0, max: 0.6, step: 0.01, unit: "ms" },
  ];

  return (
    <section className="min-w-0 rounded-2xl border border-base-300 bg-base-100" aria-labelledby="crossfeed-title">
      <div className="card-body gap-4 p-4 sm:p-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="card-title text-base" id="crossfeed-title"><Headphones size={18} aria-hidden="true" />{t("Headphone crossfeed")}</h2>
          {crossfeed && <SaveStateBadge state={crossfeed.dirty ? "Unsaved" : "Saved"} label={t("Crossfeed save state")} />}
        </div>
        {unavailable && <p className="grow-0 text-xs text-base-content/70">{unavailable}</p>}
        <p className="grow-0 text-xs leading-relaxed text-base-content/70">{t("Blend a little of each channel into the other to soften hard left/right separation in headphones.")}</p>
        {!unavailable && !crossfeed && <p className="grow-0 text-xs text-base-content/70">{t("Reading crossfeed settings…")}</p>}
        <fieldset className="grid min-w-0 gap-4" disabled={disabled}>
          <div className="flex flex-wrap items-center justify-between gap-3">
            <span className="text-sm font-semibold">{t("Mode")}</span>
            <SelectMenu className="w-36" label={t("Crossfeed mode")} value={live.mode} options={modeOptions} disabled={disabled} onChange={(mode) => settings.update({ mode })} />
          </div>
          {customControls.map(({ key, label, shortLabel, help, min, max, step, unit }) => (
            <div className="grid min-w-0 gap-2" key={key}>
              <span className="flex items-center gap-1 text-sm font-semibold">{shortLabel}<MetricTooltip label={label} help={help} /></span>
              <div className="flex min-w-0 items-center gap-3">
                <input className="range range-xs min-w-18 flex-1" type="range" aria-label={t("{label} slider", { label })} min={min} max={max} step={step} value={displayed[key]} disabled={parametersDisabled} onChange={(event) => { if (!parametersDisabled) settings.update({ [key]: Number(event.target.value) }); }} />
                <label className="input input-sm min-h-10 sm:min-h-8 flex w-24 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                  <NumericInput className="min-w-0 grow" label={label} min={min} max={max} step={step} value={displayed[key]} readOnly={parametersDisabled} onChange={(value) => { if (!parametersDisabled) settings.update({ [key]: value }); }} onInvalid={() => onError(t("{label} must be between {min} and {max}{unit}.", { label, min: min.toLocaleString(locale), max: max.toLocaleString(locale), unit: ` ${unit}` }))} />
                  <span className="text-xs text-base-content/70">{unit}</span>
                </label>
              </div>
            </div>
          ))}
        </fieldset>
        <div className="mt-auto flex flex-wrap items-center justify-end gap-3 border-t border-base-200 pt-4">
          <button className="btn btn-primary btn-sm min-h-10 sm:min-h-8" disabled={disabled || !crossfeed?.dirty} onClick={() => void settings.save()}>
            {saving ? <span className="loading loading-spinner loading-xs" aria-hidden="true" /> : <Save size={16} aria-hidden="true" />}
            <span>{saving ? t("Saving…") : t("Save crossfeed")}</span>
          </button>
        </div>
      </div>
    </section>
  );
}

export function OutputProcessingSettings({ settings, connected, busy, onError }: Props) {
  const { outputProcessing, outputSupported, savingOutput: saving } = settings;
  const unavailable = !connected ? t("Connect DAC to view audio settings") : !outputSupported ? t("Requires firmware 3.0") : null;
  const disabled = !connected || !outputSupported || busy || saving || outputProcessing === null;
  const live = outputProcessing?.live ?? defaultOutputProcessing;
  const toggles: Array<{ key: keyof Pick<OutputProcessingConfig, "mono" | "swap" | "invertLeft" | "invertRight">; label: string }> = [
    { key: "mono", label: t("Mono") },
    { key: "swap", label: t("Swap channels") },
    { key: "invertLeft", label: t("Invert left polarity") },
    { key: "invertRight", label: t("Invert right polarity") },
  ];

  return (
    <section className="min-w-0 rounded-2xl border border-base-300 bg-base-100" aria-labelledby="output-processing-title">
      <div className="card-body gap-4 p-4 sm:p-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="card-title text-base" id="output-processing-title"><SlidersHorizontal size={18} aria-hidden="true" />{t("Output processing")}</h2>
          {outputProcessing && <SaveStateBadge state={outputProcessing.dirty ? "Unsaved" : "Saved"} label={t("Output processing save state")} />}
        </div>
        {unavailable && <p className="grow-0 text-xs text-base-content/70">{unavailable}</p>}
        {!unavailable && !outputProcessing && <p className="grow-0 text-xs text-base-content/70">{t("Reading output settings…")}</p>}
        <fieldset className="grid min-w-0 gap-4" disabled={disabled}>
          {settings.pinSupported && <label className="flex cursor-pointer items-center justify-between gap-3 min-h-11 border-b border-base-200 py-2.5 text-sm font-medium">
            {t("Follow USB volume")}
            <input className="toggle toggle-sm" type="checkbox" checked={!live.pinUsbVolume} onChange={(event) => settings.updateOutput({ pinUsbVolume: !event.target.checked })} />
          </label>}
          <div className="grid gap-2 sm:grid-cols-2 lg:grid-cols-1 xl:grid-cols-2">
            {toggles.map(({ key, label }) => (
              <label className="flex cursor-pointer items-center justify-between gap-3 min-h-11 border-b border-base-200 py-2.5 text-sm font-medium" key={key}>
                <span className="inline-flex items-center gap-1">{label}{(key === "invertLeft" || key === "invertRight") && <MetricTooltip label={label} help={t("Reverse the signal polarity in this channel. This does not swap the left and right channels.")} />}</span>
                <input className="toggle toggle-sm" type="checkbox" aria-label={label} checked={live[key]} onChange={(event) => settings.updateOutput({ [key]: event.target.checked })} />
              </label>
            ))}
          </div>
          <div className="grid min-w-0 gap-2">
            <div className="flex items-center justify-between gap-3 text-sm font-semibold"><span>{t("Balance")}</span><span className="text-xs font-normal text-base-content/70">{t("Left · Center · Right")}</span></div>
            <div className="flex min-w-0 items-center gap-3">
              <input className="range range-xs min-w-18 flex-1" type="range" aria-label={t("Balance slider")} min={-100} max={100} step={1} value={live.balancePercent} onChange={(event) => settings.updateOutput({ balancePercent: Number(event.target.value) })} />
              <label className="input input-sm min-h-10 sm:min-h-8 flex w-24 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                <NumericInput className="min-w-0 grow" label={t("Balance")} min={-100} max={100} step={1} value={live.balancePercent} onChange={(balancePercent) => settings.updateOutput({ balancePercent })} onInvalid={() => onError(t("Balance must be between -100 and 100 %."))} />
                <span className="text-xs text-base-content/70">%</span>
              </label>
            </div>
          </div>
          <div className="grid min-w-0 gap-2">
            <div className="flex items-center justify-between gap-3 text-sm font-semibold"><span className="inline-flex items-center gap-1">{t("Stereo width")}<MetricTooltip label={t("Stereo width")} help={t("100% keeps the original stereo width. Lower values bring the channels closer together; higher values widen their differences.")} /></span><span className="text-xs font-normal text-base-content/70">{t("100% is neutral")}</span></div>
            <div className="flex min-w-0 items-center gap-3">
              <input className="range range-xs min-w-18 flex-1" type="range" aria-label={t("Stereo width slider")} min={0} max={200} step={1} value={live.widthPercent} disabled={disabled || live.mono} onChange={(event) => settings.updateOutput({ widthPercent: Number(event.target.value) })} />
              <label className="input input-sm min-h-10 sm:min-h-8 flex w-24 shrink-0 items-center gap-1.5 has-[input[aria-invalid=true]]:input-error">
                <NumericInput className="min-w-0 grow" label={t("Stereo width")} min={0} max={200} step={1} value={live.widthPercent} readOnly={disabled || live.mono} onChange={(widthPercent) => settings.updateOutput({ widthPercent })} onInvalid={() => onError(t("Stereo width must be between 0 and 200 %."))} />
                <span className="text-xs text-base-content/70">%</span>
              </label>
            </div>
          </div>
        </fieldset>
        <div className="mt-auto flex flex-wrap items-center justify-end gap-3 border-t border-base-200 pt-4">
          <button className="btn btn-primary btn-sm min-h-10 sm:min-h-8" disabled={disabled || !outputProcessing?.dirty} onClick={() => void settings.saveOutput()}>
            {saving ? <span className="loading loading-spinner loading-xs" aria-hidden="true" /> : <Save size={16} aria-hidden="true" />}
            <span>{saving ? t("Saving…") : t("Save output")}</span>
          </button>
        </div>
      </div>
    </section>
  );
}
