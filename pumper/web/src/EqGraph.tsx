import { KeyboardEvent, PointerEvent, useEffect, useId, useMemo, useRef, useState } from "react";
import { OutputSpectrum } from "./OutputSpectrum";
import type { PumperHidTransport } from "./hidTransport";
import { responseCurve } from "./eqMath";
import { EqBand, EqConfig, FilterType } from "./protocol";

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
  spectrumTransport?: PumperHidTransport | null;
  spectrumEnabled?: boolean;
  config: EqConfig;
  sampleRateHz: number;
  selectedBand: number;
  onSelectBand: (index: number) => void;
  onChangeBand: (index: number, patch: Partial<EqBand>) => void;
  interactive?: boolean;
}

const margin = { left: 55, right: 18, top: 20, bottom: 38 };
const minimumGain = -30;
const maximumGain = 18;

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, value));
}

function roundedFrequency(value: number): number {
  const step = value < 100 ? 1 : value < 1000 ? 5 : value < 10000 ? 10 : 50;
  return Math.round(value / step) * step;
}

function filterUsesGain(type: FilterType): boolean {
  return type <= FilterType.HighShelf;
}

export function EqGraph({ spectrumTransport = null, spectrumEnabled = false, config, sampleRateHz, selectedBand, onSelectBand, onChangeBand, interactive = true }: EqGraphProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const descriptionId = useId();
  const [width, setWidth] = useState(1000);
  const height = width < 640 ? 280 : 350;
  const viewWidth = spectrumEnabled ? width + 35 : width;
  const graphWidth = width - margin.left - margin.right;
  const graphHeight = height - margin.top - margin.bottom;
  const xForFrequency = (frequencyHz: number) => margin.left + (Math.log10(frequencyHz / 20) / 3) * graphWidth;
  const yForGain = (gainDb: number) => margin.top + ((maximumGain - gainDb) / (maximumGain - minimumGain)) * graphHeight;
  const frequencyForX = (x: number) => 20 * 1000 ** ((x - margin.left) / graphWidth);
  const gainForY = (y: number) => maximumGain - ((y - margin.top) / graphHeight) * (maximumGain - minimumGain);

  useEffect(() => {
    const svg = svgRef.current;
    if (!svg || typeof ResizeObserver === "undefined") return;
    const observer = new ResizeObserver(([entry]) => {
      if (entry.contentRect.width > 0) {
        setWidth(Math.max(280, Math.min(1000, Math.round(entry.contentRect.width) - (spectrumEnabled ? 35 : 0))));
      }
    });
    observer.observe(svg);
    return () => observer.disconnect();
  }, [spectrumEnabled]);

  const [dragging, setDragging] = useState<number | null>(null);
  useEffect(() => {
    if (!interactive) setDragging(null);
  }, [interactive]);
  const curve = useMemo(() => responseCurve(config, sampleRateHz), [config, sampleRateHz]);
  const selected = config.bands[selectedBand];
  const selectedCurve = useMemo(() => responseCurve({ ...config, preampDb: 0, bands: [selected] }, sampleRateHz), [config.enabled, selected, sampleRateHz]);
  const path = useMemo(
    () =>
      curve
        .map((point, index) => {
          const x = xForFrequency(point.frequencyHz);
          const y = yForGain(clamp(point.gainDb, minimumGain, maximumGain));
          return `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
        })
        .join(" "),
    [curve, graphWidth, graphHeight],
  );

  const updateFromPointer = (event: PointerEvent<SVGSVGElement>) => {
    if (!interactive || dragging === null) return;
    const rect = event.currentTarget.getBoundingClientRect();
    // Account for meet scaling when the SVG is constrained by its container.
    const scale = Math.min(rect.width / viewWidth, rect.height / height);
    const offsetX = (rect.width - viewWidth * scale) / 2;
    const offsetY = (rect.height - height * scale) / 2;
    const x = clamp((event.clientX - rect.left - offsetX) / scale, margin.left, width - margin.right);
    const y = clamp((event.clientY - rect.top - offsetY) / scale, margin.top, height - margin.bottom);
    const band = config.bands[dragging];
    onChangeBand(dragging, {
      frequencyHz: clamp(roundedFrequency(frequencyForX(x)), 20, 20000),
      ...(filterUsesGain(band.type) ? { gainDb: Math.round(clamp(gainForY(y), -24, 24) * 10) / 10 } : {}),
    });
  };

  const startDragging = (event: PointerEvent<SVGCircleElement>, index: number) => {
    if (!interactive) return;
    // Keep capture on the stable SVG while selection brings a band to the front.
    svgRef.current?.setPointerCapture?.(event.pointerId);
    setDragging(index);
    onSelectBand(index);
  };

  const updateFromKeyboard = (event: KeyboardEvent<SVGCircleElement>, index: number) => {
    if (!interactive) return;
    const band = config.bands[index];
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      onSelectBand(index);
      return;
    }
    const horizontal = event.key === "ArrowLeft" || event.key === "ArrowRight";
    const vertical = event.key === "ArrowUp" || event.key === "ArrowDown";
    if (!horizontal && !vertical) return;
    event.preventDefault();
    onSelectBand(index);
    if (horizontal) {
      const direction = event.key === "ArrowRight" ? 1 : -1;
      const frequencyHz = Math.round(band.frequencyHz * 2 ** (direction * (event.shiftKey ? 1 / 6 : 1 / 48)));
      onChangeBand(index, { frequencyHz: clamp(frequencyHz === band.frequencyHz ? frequencyHz + direction : frequencyHz, 20, 20000) });
    } else if (filterUsesGain(band.type)) {
      const direction = event.key === "ArrowUp" ? 1 : -1;
      onChangeBand(index, { gainDb: clamp(Math.round((band.gainDb + direction * (event.shiftKey ? 1 : 0.1)) * 10) / 10, -24, 24) });
    }
  };

  const frequencyTicks = width < 640 ? [20, 100, 1000, 20000] : [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
  const gainTicks = [-24, -18, -12, -6, 0, 6, 12, 18];
  const selectedPath = useMemo(
    () => selectedCurve.map((point, index) => {
      const x = xForFrequency(point.frequencyHz);
      const y = yForGain(clamp(point.gainDb, minimumGain, maximumGain));
      return `${index === 0 ? "M" : "L"}${x.toFixed(2)},${y.toFixed(2)}`;
    }).join(" "),
    [selectedCurve, graphWidth, graphHeight],
  );

  return (
    <svg
      ref={svgRef}
      className="block max-h-[440px] w-full touch-none select-none"
      style={{ aspectRatio: `${viewWidth} / ${height}` }}
      viewBox={`0 0 ${viewWidth} ${height}`}
      role="group"
      aria-label="Combined equalizer frequency response"
      aria-describedby={descriptionId}
      onPointerMove={updateFromPointer}
      onPointerUp={() => setDragging(null)}
      onPointerCancel={() => setDragging(null)}
      onLostPointerCapture={() => setDragging(null)}
    >
      <desc id={descriptionId}>Solid line: combined EQ with preamp. Dashed line and shading: selected filter alone, without preamp. Select or drag a numbered point. Arrow keys adjust frequency and gain; hold Shift for larger steps.</desc>
      <rect className="fill-base-200" x={margin.left} y={margin.top} width={graphWidth} height={graphHeight} rx={3} />
      <text className="fill-base-content/70 text-[11px] font-medium" x={margin.left - 10} y={12} textAnchor="end" aria-hidden="true">dB</text>
      {spectrumEnabled && <text className="fill-base-content/70 text-[11px] font-medium" x={width - margin.right + 8} y={12} aria-hidden="true">dBFS</text>}
      {frequencyTicks.map((frequency) => (
        <g key={frequency} aria-hidden="true">
          <line className="stroke-base-300 [stroke-width:1]" x1={xForFrequency(frequency)} x2={xForFrequency(frequency)} y1={margin.top} y2={height - margin.bottom} />
          <text className="fill-base-content/70 text-[11px] font-medium tabular-nums" x={xForFrequency(frequency)} y={height - 13} textAnchor="middle">
            {frequency >= 1000 ? `${frequency / 1000}k` : frequency}
          </text>
        </g>
      ))}
      {gainTicks.map((gain) => (
        <g key={gain} aria-hidden="true">
          <line className={gain === 0 ? "stroke-base-content/45 [stroke-width:1.5]" : "stroke-base-300 [stroke-width:1]"} x1={margin.left} x2={width - margin.right} y1={yForGain(gain)} y2={yForGain(gain)} />
          <text className="fill-base-content/70 text-[11px] font-medium tabular-nums" x={margin.left - 10} y={yForGain(gain) + 4} textAnchor="end">
            {gain > 0 ? `+${gain}` : gain}
          </text>
        </g>
      ))}
      <OutputSpectrum transport={spectrumTransport} enabled={spectrumEnabled} left={margin.left} top={margin.top} width={graphWidth} height={graphHeight} />
      <g pointerEvents="none" aria-hidden="true">
        {config.enabled && selected.enabled && <>
          <path d={`${selectedPath} L${width - margin.right},${yForGain(0)} L${margin.left},${yForGain(0)} Z`} fill={bandColors[selectedBand]} opacity={0.09} />
          <path d={selectedPath} fill="none" stroke={bandColors[selectedBand]} strokeWidth={1.5} strokeDasharray="4 4" opacity={0.65} />
        </>}
        <line x1={0} x2={0} y1={margin.top} y2={height - margin.bottom} stroke={bandColors[selectedBand]} strokeWidth={1} strokeDasharray="3 5" opacity={0.6} style={{ transform: `translateX(${xForFrequency(selected.frequencyHz)}px)` }} className={dragging === null ? "motion-safe:transition-transform motion-safe:duration-150 motion-safe:ease-out" : ""} />
      </g>
      <path className="fill-none stroke-base-100/70 [stroke-width:8]" d={path} pointerEvents="none" aria-hidden="true" />
      <path className="fill-none stroke-accent [stroke-width:3]" d={path} pointerEvents="none" aria-hidden="true" />
      {config.bands.map((band, index) => ({ band, index })).sort((a, b) => Number(a.index === selectedBand) - Number(b.index === selectedBand)).map(({ band, index }) => {
        const handleGain = filterUsesGain(band.type) ? band.gainDb : 0;
        return (
          <g key={index} className={band.enabled ? "" : "opacity-35"}>
            {selectedBand === index && <circle cx={xForFrequency(band.frequencyHz)} cy={yForGain(clamp(handleGain, minimumGain, maximumGain))} r={21} fill={bandColors[index]} opacity={0.15} pointerEvents="none" aria-hidden="true" />}
            <circle
              cx={xForFrequency(band.frequencyHz)}
              cy={yForGain(clamp(handleGain, minimumGain, maximumGain))}
              r={selectedBand === index ? 14 : 12}
              fill={bandColors[index]}
              stroke={selectedBand === index ? "var(--color-base-content)" : "var(--color-base-300)"}
              strokeWidth={selectedBand === index ? 3 : 2}
              className="cursor-grab outline-none focus-visible:stroke-base-content focus-visible:[stroke-width:4] active:cursor-grabbing"
              role="button"
              tabIndex={interactive && selectedBand === index ? 0 : -1}
              aria-disabled={!interactive}
              aria-pressed={selectedBand === index}
              aria-label={`Band ${index + 1} graph handle`}
              aria-describedby={descriptionId}
              onPointerDown={(event) => startDragging(event, index)}
              onClick={() => { if (interactive) onSelectBand(index); }}
              onFocus={() => { if (interactive) onSelectBand(index); }}
              onKeyDown={(event) => updateFromKeyboard(event, index)}
            />
            <text
              className="pointer-events-none fill-black text-[10px] font-black"
              aria-hidden="true"
              x={xForFrequency(band.frequencyHz)}
              y={yForGain(clamp(handleGain, minimumGain, maximumGain)) + 4}
              textAnchor="middle"
            >
              {index + 1}
            </text>
          </g>
        );
      })}
    </svg>
  );
}
