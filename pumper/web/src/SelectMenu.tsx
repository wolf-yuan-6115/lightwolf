import { Check } from "lucide-react";
import { useId, useRef, useState, type CSSProperties, type KeyboardEvent, type SyntheticEvent } from "react";

export interface SelectMenuOption<T extends string | number> {
  value: T;
  label: string;
}

interface SelectMenuProps<T extends string | number> {
  value: T;
  options: readonly SelectMenuOption<T>[];
  onChange: (value: T) => void;
  label: string;
  disabled?: boolean;
  className?: string;
}

type AnchorStyles = CSSProperties & {
  anchorName?: string;
  positionAnchor?: string;
  positionTry?: string;
  positionTryFallbacks?: string;
};

export function SelectMenu<T extends string | number>({ value, options, onChange, label, disabled = false, className = "" }: SelectMenuProps<T>) {
  const reactId = useId().replace(/[^a-zA-Z0-9_-]/g, "");
  const popoverId = `select-menu-${reactId}`;
  const anchorName = `--select-menu-${reactId}`;
  const triggerRef = useRef<HTMLButtonElement>(null);
  const popoverRef = useRef<HTMLUListElement>(null);
  const optionRefs = useRef<Array<HTMLButtonElement | null>>([]);
  const [open, setOpen] = useState(false);
  const selectedIndex = Math.max(0, options.findIndex((option) => option.value === value));
  const selectedOption = options[selectedIndex];

  const focusOption = (index: number) => {
    const wrappedIndex = (index + options.length) % options.length;
    optionRefs.current[wrappedIndex]?.focus();
  };

  const showMenu = () => {
    const popover = popoverRef.current;
    if (!popover || open) return;
    try {
      if (typeof popover.showPopover === "function") popover.showPopover();
    } catch {
      // The Popover API may already be opening from the trigger's default action.
    }
  };

  const hideMenu = (restoreFocus = true) => {
    const popover = popoverRef.current;
    try {
      if (popover && typeof popover.hidePopover === "function") popover.hidePopover();
    } catch {
      // Ignore a close request after light-dismiss already hid the popover.
    }
    setOpen(false);
    if (restoreFocus) triggerRef.current?.focus();
  };

  const handleTriggerKeyDown = (event: KeyboardEvent<HTMLButtonElement>) => {
    if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;
    event.preventDefault();
    showMenu();
    queueMicrotask(() => focusOption(selectedIndex));
  };

  const handleOptionKeyDown = (event: KeyboardEvent<HTMLButtonElement>, index: number) => {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      focusOption(index + 1);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      focusOption(index - 1);
    } else if (event.key === "Home") {
      event.preventDefault();
      focusOption(0);
    } else if (event.key === "End") {
      event.preventDefault();
      focusOption(options.length - 1);
    } else if (event.key === "Escape") {
      event.preventDefault();
      hideMenu();
    } else if (event.key === "Tab") {
      hideMenu(false);
    }
  };

  const handleToggle = (event: SyntheticEvent<HTMLUListElement>) => {
    const isOpen = (event.nativeEvent as ToggleEvent).newState === "open";
    setOpen(isOpen);
    if (isOpen) queueMicrotask(() => focusOption(selectedIndex));
  };

  const triggerStyle: AnchorStyles = { anchorName };
  const popoverStyle: AnchorStyles = {
    positionAnchor: anchorName,
    positionTry: "normal flip-block",
    positionTryFallbacks: "flip-block",
    width: "anchor-size(width)",
  };

  return (
    <>
      <button
        ref={triggerRef}
        className={`select select-sm ${className}`}
        type="button"
        value={String(value)}
        disabled={disabled}
        popoverTarget={popoverId}
        aria-haspopup="listbox"
        aria-controls={popoverId}
        aria-expanded={open}
        aria-label={`${label}: ${selectedOption?.label ?? "-"}`}
        style={triggerStyle}
        onKeyDown={handleTriggerKeyDown}
      >
        <span className="min-w-0 truncate">{selectedOption?.label ?? "-"}</span>
      </button>
      <ul
        ref={popoverRef}
        className="dropdown dropdown-start menu menu-sm max-h-64 overflow-y-auto rounded-box border border-base-300 bg-base-100 p-1 shadow-lg"
        id={popoverId}
        popover="auto"
        role="listbox"
        aria-label={label}
        style={popoverStyle}
        onToggle={handleToggle}
      >
        {options.map((option, index) => {
          const selected = option.value === value;
          return (
            <li role="none" key={String(option.value)}>
              <button
                ref={(element) => { optionRefs.current[index] = element; }}
                className={selected ? "menu-active" : ""}
                type="button"
                role="option"
                aria-selected={selected}
                onClick={() => {
                  if (!selected) onChange(option.value);
                  hideMenu();
                }}
                onKeyDown={(event) => handleOptionKeyDown(event, index)}
              >
                <span className="min-w-0 flex-1 truncate text-left">{option.label}</span>
                {selected && <Check className="shrink-0" size={15} aria-hidden="true" />}
              </button>
            </li>
          );
        })}
      </ul>
    </>
  );
}
