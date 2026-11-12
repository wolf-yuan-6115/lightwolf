import { t, locale } from "./i18n";
import {
  Activity, AudioWaveform, CircleX, ClockAlert, Cpu, DatabaseZap, Gauge,
  Radio, ShieldAlert, Thermometer, Timer, Usb, SlidersHorizontal, Volume2, Shuffle,
  type LucideIcon,
} from "lucide-react";
import { type AudioControls, type CrossfeedConfig, type EqConfig,
  type OutputProcessingConfig, type DeviceStatus, type DeviceTelemetry } from "./protocol";
import styles from "./DeviceInfoPipeline.module.css";
import { crossfeedParameters } from "./crossfeedParameters";
import { MetricTooltip } from "./MetricTooltip";

interface Metric {
  label: string;
  help: string;
  value: string;
}

interface SummaryMetric extends Metric {
  icon: LucideIcon;
}

interface PipelineStage {
  title: string;
  icon: LucideIcon;
  metrics: Metric[];
}

function formattedNumber(value: number | null | undefined): string {
  return value == null ? "—" : value.toLocaleString(locale);
}

function formattedDuration(milliseconds: number | null | undefined): string {
  if (milliseconds == null) return "—";
  if (milliseconds < 1000) return `${milliseconds.toLocaleString(locale)} ms`;
  const totalSeconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (days) return t("{days}d {hours}h", { days, hours });
  if (hours) return t("{hours}h {minutes}m", { hours, minutes });
  if (minutes) return t("{minutes}m {seconds}s", { minutes, seconds });
  return t("{seconds}s", { seconds });
}

function formattedSampleRate(sampleRateHz: number | null | undefined): string {
  if (sampleRateHz == null) return "—";
  return `${Number.isInteger(sampleRateHz / 1000) ? sampleRateHz / 1000 : (sampleRateHz / 1000).toFixed(1)} kHz`;
}

function MetricHelp({ metric }: { metric: Metric }) {
  return <MetricTooltip label={metric.label} help={metric.help} />;
}

function SummaryRail({ title, metrics }: { title: string; metrics: SummaryMetric[] }) {
  return (
    <section aria-label={title}>
      <h3 className="mb-3 text-xs font-medium text-base-content/60">{title}</h3>
      <dl className="grid grid-cols-2 gap-x-4 gap-y-4 md:grid-cols-4 md:gap-x-6">
        {metrics.map((metric) => {
          const Icon = metric.icon;
          return (
            <div className="flex min-w-0 items-start gap-2.5" key={metric.label}>
              <span className="mt-0.5 hidden shrink-0 text-base-content/45 sm:block">
                <Icon size={16} aria-hidden="true" />
              </span>
              <div className="min-w-0">
                <dt className="flex items-center gap-0.5 text-[11px] leading-tight text-base-content/55">
                  <span>{metric.label}</span><MetricHelp metric={metric} />
                </dt>
                <dd className="mt-0.5 break-words text-sm font-semibold tabular-nums">{metric.value}</dd>
              </div>
            </div>
          );
        })}
      </dl>
    </section>
  );
}

function Stage({ stage }: { stage: PipelineStage }) {
  const Icon = stage.icon;
  return (
    <article className="grid min-w-0 gap-3 py-4 md:grid-cols-[9rem_minmax(0,1fr)] md:gap-6">
      <header className="relative flex min-h-8 items-start pt-1.5">
        <span className="absolute -left-12 top-0 grid size-8 place-items-center rounded-md border border-base-300 bg-base-200 text-base-content/70">
          <Icon size={17} aria-hidden="true" />
        </span>
        <h4 className="text-sm font-semibold">{stage.title}</h4>
      </header>
      <dl className="grid min-w-0 grid-cols-1 gap-x-6 gap-y-3 sm:grid-cols-2 lg:grid-cols-3">
        {stage.metrics.map((metric) => (
          <div className="min-w-0" key={metric.label}>
            <dt className="flex min-w-0 items-center gap-0.5 text-xs text-base-content/65">
              <span>{metric.label}</span><MetricHelp metric={metric} />
            </dt>
            <dd className="mt-0.5 min-w-0 break-words font-mono text-xs tabular-nums">{metric.value}</dd>
          </div>
        ))}
      </dl>
    </article>
  );
}

interface DeviceInfoPipelineProps {
  status: DeviceStatus | null;
  telemetry: DeviceTelemetry | null;
  eq: EqConfig;
  audio: AudioControls | null;
  crossfeed: CrossfeedConfig | null;
  outputProcessing: OutputProcessingConfig | null;
}

function channelGain(channel: AudioControls["master"] | undefined): string {
  return channel == null ? "—" : channel.muted ? t("Muted") : `${channel.volumeDb > 0 ? "+" : ""}${channel.volumeDb.toLocaleString(locale)} dB`;
}

export function DeviceInfoPipeline({ status, telemetry, eq, audio, crossfeed, outputProcessing: output }: DeviceInfoPipelineProps) {
  const streaming = telemetry?.streaming ?? status?.streaming ?? false;
  const crossfeedValues = crossfeed == null ? null : crossfeedParameters(crossfeed);
  const limiterObserved = telemetry == null ? "—" : telemetry.limiterActiveMs > 0 ? t("Observed") : t("Not observed");
  const feedbackCorrection = telemetry == null
    ? "—"
    : `${telemetry.feedbackCorrectionPpm > 0 ? "+" : ""}${formattedNumber(telemetry.feedbackCorrectionPpm)} ppm`;

  const device: SummaryMetric[] = [
    { icon: Cpu, label: t("Firmware"), help: t("Firmware version currently running on the DAC."), value: status?.firmwareVersion ?? "—" },
    { icon: Thermometer, label: t("Chip temperature"), help: t("Approximate RP2350 junction temperature reported by its internal sensor; this is not the ambient temperature."), value: status?.temperatureC == null ? "—" : `${status.temperatureC.toFixed(1)} °C` },
    { icon: Gauge, label: t("System clock"), help: t("Current RP2350 system clock. It rises while the USB audio stream is open and returns to idle speed when the stream closes."), value: status?.systemClockMHz == null ? "—" : `${status.systemClockMHz.toFixed(0)} MHz` },
    { icon: Timer, label: t("Device uptime"), help: t("Elapsed time since the DAC last booted."), value: formattedDuration(telemetry == null ? null : telemetry.uptimeSeconds * 1000) },
  ];

  const stages: PipelineStage[] = [
    {
      title: t("USB Audio In"),
      icon: Usb,
      metrics: [
        { label: t("Sample rate"), help: t("Sample rate currently selected by the USB audio host."), value: formattedSampleRate(status?.sampleRateHz) },
        { label: t("Bit depth"), help: t("PCM bit depth currently selected by the USB audio host."), value: status?.bitDepth == null ? "—" : `${status.bitDepth}-bit` },
        { label: t("Stream duration"), help: t("Duration of the current or most recently completed USB audio stream."), value: formattedDuration(telemetry?.streamDurationMs) },
        { label: t("USB audio packets"), help: t("Audio OUT packets received from the USB host since boot."), value: formattedNumber(telemetry?.usbAudioPackets) },
        { label: t("Stream starts"), help: t("Number of audio streams opened since the DAC booted."), value: formattedNumber(telemetry?.streamStarts) },
        { label: t("USB feedback"), help: t("Whether asynchronous USB feedback is currently active."), value: telemetry == null ? "—" : telemetry.feedbackActive ? t("Active") : t("Inactive") },
        { label: t("Feedback correction"), help: t("Current USB asynchronous-feedback adjustment relative to the nominal sample rate."), value: feedbackCorrection },
      ],
    },
    {
      title: t("EQ & preamp"),
      icon: SlidersHorizontal,
      metrics: [
        { label: t("EQ state"), help: t("Whether the live equalizer is enabled."), value: status == null ? "—" : eq.enabled ? t("Enabled") : t("Bypassed") },
        { label: t("Preamp"), help: t("Live preamp gain applied before the EQ filters when EQ is enabled."), value: status == null ? "—" : `${eq.preampDb > 0 ? "+" : ""}${eq.preampDb.toLocaleString(locale)} dB` },
        { label: t("Enabled bands"), help: t("Number of enabled filters in the live EQ configuration. EQ bypass also bypasses these filters."), value: status == null ? "—" : `${eq.bands.filter((band) => band.enabled).length} / ${eq.bands.length}` },
        { label: t("Active configuration"), help: t("The EQ settings revision currently running on the audio processor."), value: formattedNumber(status?.appliedGeneration) },
      ],
    },
    {
      title: t("Crossfeed"),
      icon: Shuffle,
      metrics: [
        { label: t("Crossfeed mode"), help: t("Live crossfeed mode. Off bypasses the crossfeed stage."), value: crossfeed == null ? "—" : [t("Off"), t("Low"), t("Medium"), t("High"), t("Custom")][crossfeed.mode] },
        { label: t("Crossfeed strength"), help: t("Effective crossfeed strength for the selected preset or custom mode."), value: crossfeedValues == null ? "—" : `${crossfeedValues.strengthPercent}%` },
        { label: t("Crossfeed cutoff"), help: t("Effective crossfeed cutoff frequency. Off bypasses the stage."), value: crossfeedValues == null ? "—" : `${formattedNumber(crossfeedValues.cutoffHz)} Hz` },
        { label: t("Crossfeed delay"), help: t("Effective crossfeed delay for the selected preset or custom mode."), value: crossfeedValues == null ? "—" : `${crossfeedValues.delayMs} ms` },
      ],
    },
    {
      title: t("Host gain"),
      icon: Volume2,
      metrics: [
        { label: t("Master gain"), help: t("USB host master volume or mute, applied after crossfeed."), value: channelGain(audio?.master) },
        { label: t("Left gain"), help: t("USB host left-channel volume or mute, combined with the master gain."), value: channelGain(audio?.left) },
        { label: t("Right gain"), help: t("USB host right-channel volume or mute, combined with the master gain."), value: channelGain(audio?.right) },
      ],
    },
    {
      title: t("Output processing"),
      icon: AudioWaveform,
      metrics: [
        { label: t("Channel mode"), help: t("Live output channel mode before the limiter."), value: output == null ? "—" : output.mono ? t("Mono") : t("Stereo") },
        { label: t("Stereo width"), help: t("Live stereo width. 100% is neutral; mono output has no stereo separation."), value: output == null ? "—" : `${output.widthPercent}%` },
        { label: t("Balance"), help: t("Live balance between the left and right output channels."), value: output == null ? "—" : output.balancePercent === 0 ? t("Center") : t(output.balancePercent < 0 ? "{percent}% left" : "{percent}% right", { percent: Math.abs(output.balancePercent) }) },
        { label: t("Channel routing"), help: t("Whether the left and right output channels are swapped."), value: output == null ? "—" : output.swap ? t("Swapped") : "L → L · R → R" },
        { label: t("Polarity"), help: t("Live phase inversion applied to the output channels."), value: output == null ? "—" : output.invertLeft && output.invertRight ? t("Both inverted") : output.invertLeft ? t("Left inverted") : output.invertRight ? t("Right inverted") : t("Normal") },
      ],
    },
    {
      title: t("Limiter"),
      icon: ShieldAlert,
      metrics: [
        { label: t("Activity state"), help: t("Whether limiter gain reduction has been observed during this stream."), value: limiterObserved },
        { label: t("Active duration"), help: t("Cumulative time the always-on limiter reduced gain during this stream."), value: formattedDuration(telemetry?.limiterActiveMs) },
      ],
    },
    {
      title: t("I²S Audio Out"),
      icon: AudioWaveform,
      metrics: [
        { label: t("Buffered"), help: t("Stereo frames currently queued for I²S output."), value: telemetry == null ? "—" : t("{count} frames", { count: formattedNumber(telemetry.i2sBufferedFrames) }) },
        { label: t("Low-water"), help: t("Lowest number of queued stereo frames observed after the output buffer was primed."), value: status?.i2sLowWaterFrames == null ? "—" : t("{count} frames", { count: formattedNumber(status.i2sLowWaterFrames) }) },
        { label: t("High-water"), help: t("Highest number of queued stereo frames observed during this stream."), value: telemetry == null ? "—" : t("{count} frames", { count: formattedNumber(telemetry.i2sHighWaterFrames) }) },
        { label: t("Audio underruns"), help: t("Audio frames replaced with silence because the output buffer ran empty."), value: formattedNumber(status?.underrunFrames) },
      ],
    },
  ];

  const health: SummaryMetric[] = [
    { icon: Radio, label: t("Meter reports"), help: t("Level-meter reports successfully submitted to the USB host since boot."), value: formattedNumber(telemetry?.meterReports) },
    { icon: CircleX, label: t("Malformed HID reports"), help: t("HID output reports rejected because their framing or protocol header was invalid."), value: formattedNumber(telemetry?.malformedHidReports) },
    { icon: ClockAlert, label: t("Busy HID reports"), help: t("HID output reports ignored while another response, flash operation, or reset was in progress."), value: formattedNumber(telemetry?.busyHidReports) },
    { icon: DatabaseZap, label: t("Flash failures"), help: t("Profile or global-setting flash operations that failed since boot."), value: formattedNumber(telemetry?.flashFailures) },
  ];

  const processing: SummaryMetric[] = [
    { icon: Activity, label: t("Average load"), help: t("Average share of playback time spent processing the whole audio chain during this stream."), value: telemetry == null ? "—" : `${telemetry.averageDspLoadPercent.toFixed(2)}%` },
    { icon: Gauge, label: t("Peak load"), help: t("Highest processing-time share observed for one block across the whole audio chain."), value: telemetry == null ? "—" : `${telemetry.peakDspLoadPercent.toFixed(2)}%` },
    { icon: Timer, label: t("Worst block"), help: t("Longest processing time measured for the whole audio chain during this stream."), value: status?.maxDspBlockUs == null ? "—" : `${formattedNumber(status.maxDspBlockUs)} µs` },
    { icon: ClockAlert, label: t("Backpressure events"), help: t("Times USB audio had to wait because every processing buffer was busy."), value: formattedNumber(status?.backpressureEvents) },
  ];

  return (
    <div className="mt-6 space-y-6">
      <SummaryRail title={t("Device")} metrics={device} />

      <section className="rounded-lg border border-base-300 bg-base-200 p-4 sm:p-5" aria-labelledby="audio-flow-title">
        <div className="mb-2">
          <h3 className="text-xs font-medium text-base-content/60" id="audio-flow-title">{t("Signal path")}</h3>
        </div>
        <ol className={styles.flow} aria-label={t("Audio signal flow")} data-flow-state={streaming ? "streaming" : "idle"}>
          {stages.map((stage) => (
            <li className={`${styles.stage} relative min-w-0`} key={stage.title}>
              <Stage stage={stage} />
            </li>
          ))}
        </ol>
        <div className="mt-4 border-t border-base-300 pt-4">
          <SummaryRail title={t("Processing totals")} metrics={processing} />
        </div>
      </section>

      <SummaryRail title={t("Transport & storage")} metrics={health} />
    </div>
  );
}
