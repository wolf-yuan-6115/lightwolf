import { useEffect, useState, type KeyboardEvent } from "react";

interface NumericInputProps {
  value: number;
  onChange: (value: number) => void;
  label: string;
  min: number;
  max: number;
  step: number;
  onInvalid?: () => void;
  readOnly?: boolean;
  className?: string;
}

const completeNumber = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/;

function formatValue(value: number): string {
  return Number.isFinite(value) ? String(value) : "";
}

function parseValue(draft: string, minimum: number, maximum: number): number | null {
  const trimmed = draft.trim();
  if (!completeNumber.test(trimmed)) return null;
  const value = Number(trimmed);
  return Number.isFinite(value) && value >= minimum && value <= maximum ? value : null;
}

function stepValue(value: number, step: number, direction: 1 | -1, minimum: number, maximum: number): number {
  const decimals = step.toString().split(".")[1]?.length ?? 0;
  const factor = 10 ** decimals;
  const stepped = Math.round((value + step * direction) * factor) / factor;
  return Math.min(maximum, Math.max(minimum, stepped));
}

export function NumericInput({ value, onChange, label, min, max, step, onInvalid, readOnly = false, className = "" }: NumericInputProps) {
  const [draft, setDraft] = useState(() => formatValue(value));
  const [editing, setEditing] = useState(false);
  const [invalid, setInvalid] = useState(false);

  useEffect(() => {
    if (!editing || readOnly) setDraft(formatValue(value));
  }, [editing, readOnly, value]);

  useEffect(() => setInvalid(false), [readOnly, value]);

  const updateDraft = (nextDraft: string) => {
    setInvalid(false);
    setDraft(nextDraft);
    const parsed = parseValue(nextDraft, min, max);
    if (parsed !== null && parsed !== value) onChange(parsed);
  };

  const finishEditing = () => {
    const parsed = parseValue(draft, min, max);
    setEditing(false);
    if (parsed === null) {
      setInvalid(true);
      setDraft(formatValue(value));
      onInvalid?.();
    } else {
      setInvalid(false);
      setDraft(formatValue(parsed));
      if (parsed !== value) onChange(parsed);
    }
  };

  const handleKeyDown = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === "Enter") {
      event.currentTarget.blur();
    } else if (event.key === "Escape") {
      event.preventDefault();
      setInvalid(false);
      setDraft(formatValue(value));
    } else if (!readOnly && (event.key === "ArrowUp" || event.key === "ArrowDown")) {
      event.preventDefault();
      const parsed = parseValue(draft, min, max) ?? value;
      const next = stepValue(parsed, step, event.key === "ArrowUp" ? 1 : -1, min, max);
      setDraft(formatValue(next));
      if (next !== value) onChange(next);
    }
  };

  return (
    <input
      className={className}
      type="text"
      inputMode="decimal"
      role="spinbutton"
      aria-label={label}
      aria-valuemin={min}
      aria-valuemax={max}
      aria-valuenow={value}
      aria-invalid={invalid || undefined}
      value={draft}
      readOnly={readOnly}
      onFocus={() => {
        setEditing(true);
        setInvalid(false);
      }}
      onChange={(event) => updateDraft(event.target.value)}
      onBlur={finishEditing}
      onKeyDown={handleKeyDown}
    />
  );
}
