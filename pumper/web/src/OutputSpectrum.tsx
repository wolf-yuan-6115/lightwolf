import type { PumperHidTransport } from "./hidTransport";
import { useOutputSpectrum } from "./useOutputSpectrum";

interface Props {
  transport: PumperHidTransport | null;
  enabled: boolean;
  left: number;
  top: number;
  width: number;
  height: number;
}

export function OutputSpectrum({ transport, enabled, left, top, width, height }: Props) {
  const power = useOutputSpectrum(transport, enabled);
  const y = (db: number) => top + height * (Math.max(-96, Math.min(0, db)) / -96);
  const path = power ? Array.from(power, (value, i) =>
    `${i === 0 ? "M" : "L"}${(left + width * i / 255).toFixed(2)},${y(value > 0 ? 10 * Math.log10(value) : -96).toFixed(2)}`,
  ).join(" ") : "";
  return (
    <g pointerEvents="none" aria-label="Output spectrum in dBFS">
      {path && <path d={`${path} L${left + width},${top + height} L${left},${top + height} Z`} className="fill-info/12 stroke-info/35 [stroke-width:1]" />}
      {enabled && <>
        {[0, -24, -48, -72, -96].map((db) => <text key={db} x={left + width + 8} y={y(db) + (db === 0 ? 12 : db === -96 ? -4 : 3)} textAnchor="start" className="fill-base-content/50 text-[11px]">{db}</text>)}
      </>}
    </g>
  );
}
