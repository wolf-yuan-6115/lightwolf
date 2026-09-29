import { Info } from "lucide-react";
import { useEffect, useId, useLayoutEffect, useRef, useState } from "react";

export function MetricTooltip({ label, help }: { label: string; help: string }) {
  const id = useId();
  const trigger = useRef<HTMLSpanElement>(null);
  const tooltip = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);
  const hovered = useRef(false);
  const focused = useRef(false);

  useLayoutEffect(() => {
    if (!open || !trigger.current || !tooltip.current) return;
    const content = tooltip.current;
    // The native top layer escapes the modal's scroll clipping and transformed box.
    content.showPopover?.();
    const anchor = trigger.current.getBoundingClientRect();
    const box = content.getBoundingClientRect();
    const modal = trigger.current.closest(".modal-box")?.getBoundingClientRect();
    const leftEdge = Math.max(8, (modal?.left ?? 0) + 8);
    const rightEdge = Math.min(window.innerWidth - 8, (modal?.right ?? window.innerWidth) - 8);
    const topEdge = Math.max(8, (modal?.top ?? 0) + 8);
    const bottomEdge = Math.min(window.innerHeight - 8, (modal?.bottom ?? window.innerHeight) - 8);
    const left = Math.max(leftEdge, Math.min(anchor.left + anchor.width / 2 - box.width / 2, rightEdge - box.width));
    const above = anchor.top - box.height - 8;
    const top = Math.max(topEdge, Math.min(above >= topEdge ? above : anchor.bottom + 8, bottomEdge - box.height));
    content.style.left = `${left}px`;
    content.style.top = `${top}px`;
    return () => { content.hidePopover?.(); };
  }, [open]);

  useEffect(() => {
    if (!open) return;
    const close = () => setOpen(false);
    window.addEventListener("resize", close);
    document.addEventListener("scroll", close, true);
    return () => {
      window.removeEventListener("resize", close);
      document.removeEventListener("scroll", close, true);
    };
  }, [open]);

  return (
    <>
      <span
        ref={trigger}
        className="inline-flex size-5 shrink-0 cursor-help items-center justify-center rounded-full text-base-content/40 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-base-content"
        tabIndex={0}
        aria-label={`About ${label}: ${help}`}
        aria-describedby={open ? id : undefined}
        onMouseEnter={() => { hovered.current = true; setOpen(true); }}
        onMouseLeave={() => { hovered.current = false; if (!focused.current) setOpen(false); }}
        onFocus={() => { focused.current = true; setOpen(true); }}
        onBlur={() => { focused.current = false; if (!hovered.current) setOpen(false); }}
        onKeyDown={(event) => { if (event.key === "Escape") { event.stopPropagation(); setOpen(false); } }}
      >
        <Info size={13} aria-hidden="true" />
      </span>
      <span
        ref={tooltip}
        id={id}
        role="tooltip"
        popover="manual"
        className="pointer-events-none fixed inset-auto z-[1001] m-0 w-64 max-w-[calc(100vw-3rem)] rounded-lg border border-base-300 bg-base-content px-3 py-2 text-left text-xs leading-relaxed text-base-100 shadow-lg"
        style={{ display: open ? "block" : "none" }}
      >{help}</span>
    </>
  );
}
