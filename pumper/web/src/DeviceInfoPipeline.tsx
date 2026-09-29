import {
  Activity, AudioWaveform, CircleX, ClockAlert, Cpu, DatabaseZap, Gauge,
  Info, Radio, ShieldAlert, Thermometer, Timer, Usb,
  type LucideIcon,
} from "lucide-react";
import type { DeviceStatus, DeviceTelemetry } from "./protocol";
import styles from "./DeviceInfoPipeline.module.css";

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
  return (
    <span
      className="tooltip tooltip-top inline-flex size-5 shrink-0 cursor-help items-center justify-center rounded-full text-base-content/40 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-base-content"
      tabIndex={0}
      aria-label={`About ${metric.label}: ${metric.help}`}
    >
      <span className="tooltip-content z-20 w-64 max-w-[calc(100vw-2rem)] whitespace-normal text-left text-xs leading-relaxed">{metric.help}</span>
      <Info size={13} aria-hidden="true" />
    </span>
  );
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
    <article className="min-w-0">
      <header className="relative z-10 mb-4 flex items-center gap-2.5 py-1 xl:mb-6 xl:w-fit xl:pr-4">
        <Icon className="shrink-0 text-base-content/65" size={18} aria-hidden="true" />
        <h4 className="text-sm font-semibold">{stage.title}</h4>
      </header>
      <dl className="space-y-2.5">
        {stage.metrics.map((metric) => (
          <div className="flex items-baseline justify-between gap-3" key={metric.label}>
            <dt className="flex min-w-0 items-center gap-0.5 text-xs text-base-content/65">
              <span>{metric.label}</span><MetricHelp metric={metric} />
            </dt>
            <dd className="min-w-0 break-words text-right font-mono text-xs tabular-nums">{metric.value}</dd>
          </div>
        ))}
      </dl>
    </article>
  );
}

export function DeviceInfoPipeline({ status, telemetry }: { status: DeviceStatus | null; telemetry: DeviceTelemetry | null }) {
  const streaming = telemetry?.streaming ?? status?.streaming ?? false;
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
      title: "DSP",
      icon: Activity,
      metrics: [
        { label: "Average load", help: "Average share of each audio block's playback time spent in DSP during this stream.", value: telemetry == null ? "—" : `${telemetry.averageDspLoadPercent.toFixed(2)}%` },
        { label: "Peak load", help: "Highest processing-time share observed for one audio block during this stream.", value: telemetry == null ? "—" : `${telemetry.peakDspLoadPercent.toFixed(2)}%` },
        { label: "Worst block", help: "Longest EQ processing time observed since the current audio stream opened.", value: status?.maxDspBlockUs == null ? "—" : `${formattedNumber(status.maxDspBlockUs)} µs` },
        { label: "Backpressure events", help: "Times USB audio had to wait because every processing buffer was busy.", value: formattedNumber(status?.backpressureEvents) },
        { label: "Active configuration", help: "The EQ settings revision currently running on the audio processor.", value: formattedNumber(status?.appliedGeneration) },
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

  return (
    <div className="mt-6 space-y-6">
      <SummaryRail title="Device" metrics={device} />

      <section className="rounded-lg border border-base-300 bg-base-200 p-4 sm:p-5" aria-labelledby="audio-flow-title">
        <div className="mb-6 flex items-center justify-between gap-3">
          <h3 className="text-xs font-medium text-base-content/60" id="audio-flow-title">Signal path</h3>
        </div>
        <ol className={`${styles.flow} grid grid-cols-1 xl:grid-cols-4 xl:gap-8`} aria-label="Audio signal flow" data-flow-state={streaming ? "streaming" : "idle"}>
          {stages.map((stage) => (
            <li className={`${styles.stage} relative min-w-0`} key={stage.title}>
              <Stage stage={stage} />
            </li>
          ))}
        </ol>
      </section>

      <SummaryRail title="Transport & storage" metrics={health} />
    </div>
  );
}
