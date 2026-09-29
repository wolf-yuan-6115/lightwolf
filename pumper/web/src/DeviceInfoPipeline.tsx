import {
  Activity, AudioWaveform, CircleX, ClockAlert, Cpu, DatabaseZap, Gauge,
  Radio, ShieldAlert, Thermometer, Timer, Usb, SlidersHorizontal, Volume2, Shuffle,
  type LucideIcon,
} from "lucide-react";
import { CrossfeedMode, type AudioControls, type CrossfeedConfig, type EqConfig,
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
  return value == null ? "—" : value.toLocaleString("en-US");
}

function formattedDuration(milliseconds: number | null | undefined): string {
  if (milliseconds == null) return "—";
  if (milliseconds < 1000) return `${milliseconds.toLocaleString("en-US")} ms`;
  const totalSeconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (days) return `${days}d ${hours}h`;
  if (hours) return `${hours}h ${minutes}m`;
  if (minutes) return `${minutes}m ${seconds}s`;
  return `${seconds}s`;
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
  return channel == null ? "—" : channel.muted ? "Muted" : `${channel.volumeDb > 0 ? "+" : ""}${channel.volumeDb.toLocaleString("en-US")} dB`;
}

export function DeviceInfoPipeline({ status, telemetry, eq, audio, crossfeed, outputProcessing: output }: DeviceInfoPipelineProps) {
  const streaming = telemetry?.streaming ?? status?.streaming ?? false;
  const crossfeedValues = crossfeed == null ? null : crossfeedParameters(crossfeed);
  const limiterObserved = telemetry == null ? "—" : telemetry.limiterActiveMs > 0 ? "Observed" : "Not observed";
  const feedbackCorrection = telemetry == null
    ? "—"
    : `${telemetry.feedbackCorrectionPpm > 0 ? "+" : ""}${formattedNumber(telemetry.feedbackCorrectionPpm)} ppm`;

  const device: SummaryMetric[] = [
    { icon: Cpu, label: "Firmware", help: "Firmware version currently running on the DAC.", value: status?.firmwareVersion ?? "—" },
    { icon: Thermometer, label: "Chip temperature", help: "Approximate RP2350 junction temperature reported by its internal sensor; this is not the ambient temperature.", value: status?.temperatureC == null ? "—" : `${status.temperatureC.toFixed(1)} °C` },
    { icon: Gauge, label: "System clock", help: "Current RP2350 system clock. It rises while the USB audio stream is open and returns to idle speed when the stream closes.", value: status?.systemClockMHz == null ? "—" : `${status.systemClockMHz.toFixed(0)} MHz` },
    { icon: Timer, label: "Device uptime", help: "Elapsed time since the DAC last booted.", value: formattedDuration(telemetry == null ? null : telemetry.uptimeSeconds * 1000) },
  ];

  const stages: PipelineStage[] = [
    {
      title: "USB Audio In",
      icon: Usb,
      metrics: [
        { label: "Sample rate", help: "Sample rate currently selected by the USB audio host.", value: formattedSampleRate(status?.sampleRateHz) },
        { label: "Bit depth", help: "PCM bit depth currently selected by the USB audio host.", value: status?.bitDepth == null ? "—" : `${status.bitDepth}-bit` },
        { label: "Stream duration", help: "Duration of the current or most recently completed USB audio stream.", value: formattedDuration(telemetry?.streamDurationMs) },
        { label: "USB audio packets", help: "Audio OUT packets received from the USB host since boot.", value: formattedNumber(telemetry?.usbAudioPackets) },
        { label: "Stream starts", help: "Number of audio streams opened since the DAC booted.", value: formattedNumber(telemetry?.streamStarts) },
        { label: "USB feedback", help: "Whether asynchronous USB feedback is currently active.", value: telemetry == null ? "—" : telemetry.feedbackActive ? "Active" : "Inactive" },
        { label: "Feedback correction", help: "Current USB asynchronous-feedback adjustment relative to the nominal sample rate.", value: feedbackCorrection },
      ],
    },
    {
      title: "EQ & preamp",
      icon: SlidersHorizontal,
      metrics: [
        { label: "EQ state", help: "Whether the live equalizer is enabled.", value: status == null ? "—" : eq.enabled ? "Enabled" : "Bypassed" },
        { label: "Preamp", help: "Live preamp gain applied before the EQ filters when EQ is enabled.", value: status == null ? "—" : `${eq.preampDb > 0 ? "+" : ""}${eq.preampDb.toLocaleString("en-US")} dB` },
        { label: "Enabled bands", help: "Number of enabled filters in the live EQ configuration. EQ bypass also bypasses these filters.", value: status == null ? "—" : `${eq.bands.filter((band) => band.enabled).length} / ${eq.bands.length}` },
        { label: "Active configuration", help: "The EQ settings revision currently running on the audio processor.", value: formattedNumber(status?.appliedGeneration) },
      ],
    },
    {
      title: "Crossfeed",
      icon: Shuffle,
      metrics: [
        { label: "Crossfeed mode", help: "Live crossfeed mode. Off bypasses the crossfeed stage.", value: crossfeed == null ? "—" : CrossfeedMode[crossfeed.mode] },
        { label: "Crossfeed strength", help: "Effective crossfeed strength for the selected preset or custom mode.", value: crossfeedValues == null ? "—" : `${crossfeedValues.strengthPercent}%` },
        { label: "Crossfeed cutoff", help: "Effective crossfeed cutoff frequency. Off bypasses the stage.", value: crossfeedValues == null ? "—" : `${formattedNumber(crossfeedValues.cutoffHz)} Hz` },
        { label: "Crossfeed delay", help: "Effective crossfeed delay for the selected preset or custom mode.", value: crossfeedValues == null ? "—" : `${crossfeedValues.delayMs} ms` },
      ],
    },
    {
      title: "Host gain",
      icon: Volume2,
      metrics: [
        { label: "Master gain", help: "USB host master volume or mute, applied after crossfeed.", value: channelGain(audio?.master) },
        { label: "Left gain", help: "USB host left-channel volume or mute, combined with the master gain.", value: channelGain(audio?.left) },
        { label: "Right gain", help: "USB host right-channel volume or mute, combined with the master gain.", value: channelGain(audio?.right) },
      ],
    },
    {
      title: "Output processing",
      icon: AudioWaveform,
      metrics: [
        { label: "Channel mode", help: "Live output channel mode before the limiter.", value: output == null ? "—" : output.mono ? "Mono" : "Stereo" },
        { label: "Stereo width", help: "Live stereo width. 100% is neutral; mono output has no stereo separation.", value: output == null ? "—" : `${output.widthPercent}%` },
        { label: "Balance", help: "Live balance between the left and right output channels.", value: output == null ? "—" : output.balancePercent === 0 ? "Center" : `${Math.abs(output.balancePercent)}% ${output.balancePercent < 0 ? "left" : "right"}` },
        { label: "Channel routing", help: "Whether the left and right output channels are swapped.", value: output == null ? "—" : output.swap ? "Swapped" : "L → L · R → R" },
        { label: "Polarity", help: "Live phase inversion applied to the output channels.", value: output == null ? "—" : output.invertLeft && output.invertRight ? "Both inverted" : output.invertLeft ? "Left inverted" : output.invertRight ? "Right inverted" : "Normal" },
      ],
    },
    {
      title: "Limiter",
      icon: ShieldAlert,
      metrics: [
        { label: "Activity state", help: "Whether limiter gain reduction has been observed during this stream.", value: limiterObserved },
        { label: "Active duration", help: "Cumulative time the always-on limiter reduced gain during this stream.", value: formattedDuration(telemetry?.limiterActiveMs) },
      ],
    },
    {
      title: "I²S Audio Out",
      icon: AudioWaveform,
      metrics: [
        { label: "Buffered", help: "Stereo frames currently queued for I²S output.", value: telemetry == null ? "—" : `${formattedNumber(telemetry.i2sBufferedFrames)} frames` },
        { label: "Low-water", help: "Lowest number of queued stereo frames observed after the output buffer was primed.", value: status?.i2sLowWaterFrames == null ? "—" : `${formattedNumber(status.i2sLowWaterFrames)} frames` },
        { label: "High-water", help: "Highest number of queued stereo frames observed during this stream.", value: telemetry == null ? "—" : `${formattedNumber(telemetry.i2sHighWaterFrames)} frames` },
        { label: "Audio underruns", help: "Audio frames replaced with silence because the output buffer ran empty.", value: formattedNumber(status?.underrunFrames) },
      ],
    },
  ];

  const health: SummaryMetric[] = [
    { icon: Radio, label: "Meter reports", help: "Level-meter reports successfully submitted to the USB host since boot.", value: formattedNumber(telemetry?.meterReports) },
    { icon: CircleX, label: "Malformed HID reports", help: "HID output reports rejected because their framing or protocol header was invalid.", value: formattedNumber(telemetry?.malformedHidReports) },
    { icon: ClockAlert, label: "Busy HID reports", help: "HID output reports ignored while another response, flash operation, or reset was in progress.", value: formattedNumber(telemetry?.busyHidReports) },
    { icon: DatabaseZap, label: "Flash failures", help: "Profile or global-setting flash operations that failed since boot.", value: formattedNumber(telemetry?.flashFailures) },
  ];

  const processing: SummaryMetric[] = [
    { icon: Activity, label: "Average load", help: "Average share of playback time spent processing the whole audio chain during this stream.", value: telemetry == null ? "—" : `${telemetry.averageDspLoadPercent.toFixed(2)}%` },
    { icon: Gauge, label: "Peak load", help: "Highest processing-time share observed for one block across the whole audio chain.", value: telemetry == null ? "—" : `${telemetry.peakDspLoadPercent.toFixed(2)}%` },
    { icon: Timer, label: "Worst block", help: "Longest processing time measured for the whole audio chain during this stream.", value: status?.maxDspBlockUs == null ? "—" : `${formattedNumber(status.maxDspBlockUs)} µs` },
    { icon: ClockAlert, label: "Backpressure events", help: "Times USB audio had to wait because every processing buffer was busy.", value: formattedNumber(status?.backpressureEvents) },
  ];

  return (
    <div className="mt-6 space-y-6">
      <SummaryRail title="Device" metrics={device} />

      <section className="rounded-lg border border-base-300 bg-base-200 p-4 sm:p-5" aria-labelledby="audio-flow-title">
        <div className="mb-2">
          <h3 className="text-xs font-medium text-base-content/60" id="audio-flow-title">Signal path</h3>
        </div>
        <ol className={styles.flow} aria-label="Audio signal flow" data-flow-state={streaming ? "streaming" : "idle"}>
          {stages.map((stage) => (
            <li className={`${styles.stage} relative min-w-0`} key={stage.title}>
              <Stage stage={stage} />
            </li>
          ))}
        </ol>
        <div className="mt-4 border-t border-base-300 pt-4">
          <SummaryRail title="Processing totals" metrics={processing} />
        </div>
      </section>

      <SummaryRail title="Transport & storage" metrics={health} />
    </div>
  );
}
