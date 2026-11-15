import { t } from "./i18n";
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

function displayValue(value: number): string {
  return Number.isFinite(value) ? roundDb(value).toFixed(1) : "-inf";
}

function displayDb(value: number): string {
  return `${displayValue(value)} dBFS`;
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
    <div className="flex min-w-0 flex-col items-center gap-2" aria-label={t(source === "input" ? "{channel} input {level}" : "{channel} output {level}", { channel, level: displayDb(currentPeakDb) })}>
      <strong className="text-[11px] text-base-content/70">{channel}</strong>
      {/* Low levels start at the bottom; fixed-height segments keep both channels aligned. */}
      <div className="flex w-5 flex-col-reverse gap-0.5" aria-hidden="true">
        {thresholdsDb.map((threshold) => {
          const lit = displayedPeakDb >= threshold;
          const color = !lit
            ? "bg-base-300"
            : threshold >= -1
              ? "bg-red-500"
              : threshold >= -6
                ? "bg-amber-400"
                : nominalColor;
          return <span className={`h-1 w-full shrink-0 rounded-[1px] ${lit ? "transition-none" : "transition-colors duration-50"} ${color}`} key={threshold} />;
        })}
      </div>
      <span className="text-center text-[10px] tabular-nums text-base-content/70">
        <span className="block">{displayValue(currentPeakDb)}</span>
        <span className="block">dBFS</span>
      </span>
    </div>
  );
}

interface MeterPairProps {
  title: string;
  source: "input" | "output";
  tone: "indigo" | "emerald";
  level: StereoMeterLevel | null;
  limiterIndicator?: React.ReactNode;
}

function MeterPair({ title, source, tone, level, limiterIndicator }: MeterPairProps) {
  return (
    <div className="flex min-w-0 flex-col items-center gap-3 p-3">
      <strong className="flex min-h-5 items-center text-center text-xs font-semibold">{title}</strong>
      <div className="grid grid-cols-[repeat(2,2rem)] gap-2">
        <ChannelMeter channel="L" peak={level?.leftPeak ?? 0} source={source} tone={tone} />
        <ChannelMeter channel="R" peak={level?.rightPeak ?? 0} source={source} tone={tone} />
      </div>
      <div className="flex h-5 items-center justify-center">{limiterIndicator}</div>
    </div>
  );
}

function LimiterIndicator({ active }: { active: boolean }) {
  const label = active ? t("Limiter active") : t("Limiter inactive");
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
    <section className="mt-auto min-w-0 border-t border-base-300 bg-base-200/40 text-base-content" aria-label={t("Realtime input and output levels")}>
      <h2 className="sr-only">{t("Signal levels")}</h2>
      <div className="mx-auto grid w-full min-w-0 max-w-sm grid-cols-2 divide-x divide-base-300">
        <MeterPair title={t("INPUT")} source="input" tone="indigo" level={level?.preEq ?? null} />
        <MeterPair
          title={t("OUTPUT")}
          source="output"
          tone="emerald"
          level={level?.postEq ?? null}
          limiterIndicator={<LimiterIndicator active={level?.limiterActive ?? false} />}
        />
      </div>
    </section>
  );
}
