import { MeterLevel } from "./protocol";

const thresholdsDb = [
  -60, -54, -48, -42, -36, -33, -30, -27, -24, -21, -18, -16,
  -14, -12, -10, -8, -6, -5, -4, -3, -2, -1.5, -1, 0,
];

const fullScaleSquared = 32768 * 32768;

function peakDb(sample: number): number {
  return sample > 0 ? 20 * Math.log10(sample / 32768) : -Infinity;
}

function rmsDb(meanSquare: number): number {
  return meanSquare > 0 ? 10 * Math.log10(meanSquare / fullScaleSquared) : -Infinity;
}

function displayDb(value: number): string {
  return Number.isFinite(value) ? `${value.toFixed(1)} dBFS` : "-inf dBFS";
}

interface ChannelMeterProps {
  channel: "L" | "R";
  peak: number;
  meanSquare: number;
}

function ChannelMeter({ channel, peak, meanSquare }: ChannelMeterProps) {
  const currentPeakDb = peakDb(peak);
  const currentRmsDb = rmsDb(meanSquare);

  return (
    <div className="grid min-w-0 grid-cols-[18px_minmax(0,1fr)_76px] items-center gap-2.5 max-sm:grid-cols-[14px_minmax(0,1fr)_64px] max-sm:gap-1.5" aria-label={`${channel} output ${displayDb(currentPeakDb)}`}>
      <strong className="text-[11px] text-stone-400">{channel}</strong>
      <div className="grid h-3.5 min-w-0 grid-cols-24 gap-[3px] max-sm:gap-0.5" aria-hidden="true">
        {thresholdsDb.map((threshold) => {
          const lit = currentRmsDb >= threshold;
          const color = !lit
            ? "bg-stone-800"
            : threshold >= -1
              ? "bg-red-500 shadow-[0_0_6px_rgba(239,68,68,0.5)]"
              : threshold >= -6
                ? "bg-amber-400 shadow-[0_0_5px_rgba(251,191,36,0.42)]"
                : "bg-emerald-500 shadow-[0_0_5px_rgba(16,185,129,0.38)]";
          return <span className={`min-w-0 rounded-[1px] transition-colors duration-75 ${color}`} key={threshold} />;
        })}
      </div>
      <span className="text-right text-[10px] tabular-nums text-stone-400">{displayDb(currentPeakDb)}</span>
    </div>
  );
}

export function LevelMeter({ level }: { level: MeterLevel | null }) {
  return (
    <section className="grid min-h-22 grid-cols-[112px_minmax(0,1fr)] items-center gap-6 border-b border-stone-800 bg-black px-[clamp(18px,3vw,48px)] py-3.5 text-stone-200 max-sm:grid-cols-1 max-sm:gap-2 max-sm:px-3.5" aria-label="Realtime stereo output level">
      <div className="max-sm:flex max-sm:items-baseline max-sm:justify-between">
        <span className="block text-[10px] font-extrabold text-stone-500">OUTPUT LEVEL</span>
        <small className="mt-1 block text-xs font-bold text-stone-300 max-sm:mt-0">Post-EQ</small>
      </div>
      <div className="grid min-w-0 gap-1.5">
        <ChannelMeter channel="L" peak={level?.leftPeak ?? 0} meanSquare={level?.leftMeanSquare ?? 0} />
        <ChannelMeter channel="R" peak={level?.rightPeak ?? 0} meanSquare={level?.rightMeanSquare ?? 0} />
      </div>
    </section>
  );
}
