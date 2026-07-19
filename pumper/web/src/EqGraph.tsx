import { PointerEvent, useMemo, useState } from "react";
import { responseCurve } from "./eqMath";
import { EqBand, EqConfig } from "./protocol";

export const bandColors = [
  "#f87171",
  "#fb923c",
  "#fbbf24",
  "#a3e635",
  "#34d399",
  "#22d3ee",
  "#38bdf8",
  "#818cf8",
  "#e879f9",
  "#f472b6",
];

interface EqGraphProps {
  config: EqConfig;
  sampleRateHz: number;
  selectedBand: number;
  onSelectBand: (index: number) => void;
  onChangeBand: (index: number, patch: Partial<EqBand>) => void;
}

const width = 1000;
const height = 350;
const margin = { left: 55, right: 18, top: 20, bottom: 38 };
const graphWidth = width - margin.left - margin.right;
const graphHeight = height - margin.top - margin.bottom;
const minimumGain = -30;
const maximumGain = 18;

function xForFrequency(frequencyHz: number): number {
  return margin.left + (Math.log10(frequencyHz / 20) / 3) * graphWidth;
}

function frequencyForX(x: number): number {
  return 20 * 1000 ** ((x - margin.left) / graphWidth);
}

function yForGain(gainDb: number): number {
  return margin.top + ((maximumGain - gainDb) / (maximumGain - minimumGain)) * graphHeight;
}

function gainForY(y: number): number {
  return maximumGain - ((y - margin.top) / graphHeight) * (maximumGain - minimumGain);
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, value));
}

function roundedFrequency(value: number): number {
  const step = value < 100 ? 1 : value < 1000 ? 5 : value < 10000 ? 10 : 50;
  return Math.round(value / step) * step;
}

export function EqGraph({ config, sampleRateHz, selectedBand, onSelectBand, onChangeBand }: EqGraphProps) {
  const [dragging, setDragging] = useState<number | null>(null);
  const curve = useMemo(() => responseCurve(config, sampleRateHz), [config, sampleRateHz]);
  const path = useMemo(
    () =>
      curve
        .map((point, index) => {
          const x = xForFrequency(point.frequencyHz);
          const y = yForGain(clamp(point.gainDb, minimumGain, maximumGain));
          return `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
        })
        .join(" "),
    [curve],
  );

  const updateFromPointer = (event: PointerEvent<SVGSVGElement>) => {
    if (dragging === null) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const x = clamp(((event.clientX - rect.left) / rect.width) * width, margin.left, width - margin.right);
    const y = clamp(((event.clientY - rect.top) / rect.height) * height, margin.top, height - margin.bottom);
    onChangeBand(dragging, {
      frequencyHz: clamp(roundedFrequency(frequencyForX(x)), 20, 20000),
      gainDb: Math.round(clamp(gainForY(y), -24, 24) * 10) / 10,
    });
  };

  const startDragging = (event: PointerEvent<SVGCircleElement>, index: number) => {
    event.currentTarget.setPointerCapture(event.pointerId);
    setDragging(index);
    onSelectBand(index);
  };

  const frequencyTicks = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  const gainTicks = [-24, -18, -12, -6, 0, 6, 12, 18];

  return (
    <svg
      className="block aspect-[1000/350] max-h-[440px] min-h-[260px] w-full touch-none select-none sm:min-h-0"
      viewBox={`0 0 ${width} ${height}`}
      role="img"
      aria-label="Combined equalizer frequency response"
      onPointerMove={updateFromPointer}
      onPointerUp={() => setDragging(null)}
      onPointerCancel={() => setDragging(null)}
    >
      <rect className="fill-base-200" x={margin.left} y={margin.top} width={graphWidth} height={graphHeight} rx={3} />
      {frequencyTicks.map((frequency) => (
        <g key={frequency}>
          <line className="stroke-base-300 [stroke-width:1]" x1={xForFrequency(frequency)} x2={xForFrequency(frequency)} y1={margin.top} y2={height - margin.bottom} />
          <text className="fill-base-content/45 text-[11px] font-semibold" x={xForFrequency(frequency)} y={height - 13} textAnchor="middle">
            {frequency >= 1000 ? `${frequency / 1000}k` : frequency}
          </text>
        </g>
      ))}
      {gainTicks.map((gain) => (
        <g key={gain}>
          <line className={gain === 0 ? "stroke-base-content/45 [stroke-width:1.5]" : "stroke-base-300 [stroke-width:1]"} x1={margin.left} x2={width - margin.right} y1={yForGain(gain)} y2={yForGain(gain)} />
          <text className="fill-base-content/45 text-[11px] font-semibold" x={margin.left - 10} y={yForGain(gain) + 4} textAnchor="end">
            {gain > 0 ? `+${gain}` : gain}
          </text>
        </g>
      ))}
      <path className="fill-none stroke-base-100/70 [stroke-width:8]" d={path} />
      <path className="fill-none stroke-accent [stroke-width:3]" d={path} />
      {config.bands.map((band, index) => (
        <g key={index} className={band.enabled ? "" : "opacity-35"}>
          <circle
            cx={xForFrequency(band.frequencyHz)}
            cy={yForGain(clamp(band.gainDb, minimumGain, maximumGain))}
            r={selectedBand === index ? 14 : 12}
            fill={bandColors[index]}
            stroke={selectedBand === index ? "var(--color-base-content)" : "var(--color-base-300)"}
            strokeWidth={selectedBand === index ? 3 : 2}
            className="cursor-grab active:cursor-grabbing"
            onPointerDown={(event) => startDragging(event, index)}
            onClick={() => onSelectBand(index)}
          />
          <text
            className="pointer-events-none fill-black text-[10px] font-black"
            x={xForFrequency(band.frequencyHz)}
            y={yForGain(clamp(band.gainDb, minimumGain, maximumGain)) + 4}
            textAnchor="middle"
          >
            {index + 1}
          </text>
        </g>
      ))}
    </svg>
  );
}
