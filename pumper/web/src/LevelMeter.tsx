import { MeterLevel, StereoMeterLevel } from "./protocol";

const thresholdsDb = [
  -60, -54, -48, -42, -36, -33, -30, -27, -24, -21, -18, -16,
  -14, -12, -10, -8, -6, -5, -4, -3, -2, -1.5, -1, 0,
];

function peakDb(sample: number): number {
  return sample > 0 ? 20 * Math.log10(sample / 32768) : -Infinity;
}

function roundDb(value: number): number {
  const rounded = Math.round(value * 10) / 10;
  return Object.is(rounded, -0) ? 0 : rounded;
}

function displayDb(value: number): string {
  return Number.isFinite(value) ? `${roundDb(value).toFixed(1)} dBFS` : "-inf dBFS";
}

interface ChannelMeterProps {
  channel: "L" | "R";
  peak: number;
  source: "input" | "output";
  tone: "indigo" | "emerald";
}

function ChannelMeter({ channel, peak, source, tone }: ChannelMeterProps) {
  const currentPeakDb = peakDb(peak);
  const displayedPeakDb = roundDb(currentPeakDb);
  const nominalColor = tone === "indigo"
    ? "bg-indigo-500"
    : "bg-emerald-500";

  return (
    <div className="grid min-w-0 grid-cols-[18px_minmax(0,1fr)_76px] items-center gap-2.5 max-sm:grid-cols-[14px_minmax(0,1fr)_64px] max-sm:gap-1.5" aria-label={`${channel} ${source} ${displayDb(currentPeakDb)}`}>
      <strong className="text-[11px] text-base-content/70">{channel}</strong>
      {/* Fixed pixel tracks keep compact meter segments from rounding to unequal widths. */}
      <div className="grid h-3.5 w-max grid-cols-[repeat(24,4px)] gap-[2px]" aria-hidden="true">
        {thresholdsDb.map((threshold) => {
          const lit = displayedPeakDb >= threshold;
          const color = !lit
            ? "bg-base-300"
            : threshold >= -1
              ? "bg-red-500"
              : threshold >= -6
                ? "bg-amber-400"
                : nominalColor;
          return <span className={`min-w-0 rounded-[1px] ${lit ? "transition-none" : "transition-colors duration-50"} ${color}`} key={threshold} />;
        })}
      </div>
      <span className="text-right text-[10px] tabular-nums text-base-content/70">{displayDb(currentPeakDb)}</span>
    </div>
  );
}

interface MeterRowProps {
  title: string;
  source: "input" | "output";
  tone: "indigo" | "emerald";
  level: StereoMeterLevel | null;
  limiterIndicator?: React.ReactNode;
}

function MeterRow({ title, source, tone, level, limiterIndicator }: MeterRowProps) {
  return (
    <div className="grid min-w-0 gap-2 p-4">
      <div className="flex items-center justify-between gap-3">
        <strong className="text-xs font-semibold">{title}</strong>
        {limiterIndicator}
      </div>
      <div className="grid min-w-0 gap-1.5">
        <ChannelMeter channel="L" peak={level?.leftPeak ?? 0} source={source} tone={tone} />
        <ChannelMeter channel="R" peak={level?.rightPeak ?? 0} source={source} tone={tone} />
      </div>
    </div>
  );
}

function LimiterIndicator({ active }: { active: boolean }) {
  const label = active ? "Limiter active" : "Limiter inactive";
  return (
    <span
      className={`badge badge-sm h-5 gap-1.5 px-2 text-[9px] font-semibold tracking-wider ${active ? "badge-error badge-soft" : "badge-ghost text-base-content/45"}`}
      role="status"
      aria-label={label}
    >
      <span
        className={`status ${active ? "status-error" : "bg-current opacity-45"}`}
        aria-hidden="true"
      />
      LIM
    </span>
  );
}

export function LevelMeter({ level }: { level: MeterLevel | null }) {
  return (
    <section className="mt-auto min-w-0 border-t border-base-300 bg-base-200/40 text-base-content" aria-label="Realtime input and output levels">
      <h2 className="sr-only">Signal levels</h2>
      <div className="grid min-w-0 divide-y divide-base-200">
        <MeterRow title="INPUT" source="input" tone="indigo" level={level?.preEq ?? null} />
        <MeterRow
          title="OUTPUT"
          source="output"
          tone="emerald"
          level={level?.postEq ?? null}
          limiterIndicator={<LimiterIndicator active={level?.limiterActive ?? false} />}
        />
      </div>
    </section>
  );
}
