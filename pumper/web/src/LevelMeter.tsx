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
    ? "bg-indigo-500 shadow-[0_0_5px_rgba(99,102,241,0.42)]"
    : "bg-emerald-500 shadow-[0_0_5px_rgba(16,185,129,0.38)]";

  return (
    <div className="grid min-w-0 grid-cols-[18px_minmax(0,1fr)_76px] items-center gap-2.5 max-sm:grid-cols-[14px_minmax(0,1fr)_64px] max-sm:gap-1.5" aria-label={`${channel} ${source} ${displayDb(currentPeakDb)}`}>
      <strong className="text-[11px] text-base-content/60">{channel}</strong>
      <div className="grid h-3.5 min-w-0 grid-cols-24 gap-[3px] max-sm:gap-0.5" aria-hidden="true">
        {thresholdsDb.map((threshold) => {
          const lit = displayedPeakDb >= threshold;
          const color = !lit
            ? "bg-base-300"
            : threshold >= -1
              ? "bg-red-500 shadow-[0_0_6px_rgba(239,68,68,0.5)]"
              : threshold >= -6
                ? "bg-amber-400 shadow-[0_0_5px_rgba(251,191,36,0.42)]"
                : nominalColor;
          return <span className={`min-w-0 rounded-[1px] transition-colors duration-75 ${color}`} key={threshold} />;
        })}
      </div>
      <span className="text-right text-[10px] tabular-nums text-base-content/60">{displayDb(currentPeakDb)}</span>
    </div>
  );
}

interface MeterRowProps {
  title: string;
  source: "input" | "output";
  tone: "indigo" | "emerald";
  level: StereoMeterLevel | null;
}

function MeterRow({ title, source, tone, level }: MeterRowProps) {
  return (
    <div className="grid min-w-0 gap-3 p-4 sm:p-5">
      <strong className="text-xs font-semibold">{title}</strong>
      <div className="grid min-w-0 gap-1.5">
        <ChannelMeter channel="L" peak={level?.leftPeak ?? 0} source={source} tone={tone} />
        <ChannelMeter channel="R" peak={level?.rightPeak ?? 0} source={source} tone={tone} />
      </div>
    </div>
  );
}

export function LevelMeter({ level }: { level: MeterLevel | null }) {
  return (
    <section className="card card-border min-w-0 overflow-hidden bg-base-100 text-base-content" aria-label="Realtime input and output levels">
      <div className="border-b border-base-200 px-4 py-3 sm:px-5">
        <h2 className="card-title text-base">Signal levels</h2>
      </div>
      <div className="grid min-w-0 divide-y divide-base-200 md:grid-cols-2 md:divide-x md:divide-y-0">
        <MeterRow title="INPUT" source="input" tone="indigo" level={level?.preEq ?? null} />
        <MeterRow title="OUTPUT" source="output" tone="emerald" level={level?.postEq ?? null} />
      </div>
    </section>
  );
}
