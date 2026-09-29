import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { MetricTooltip } from "./MetricTooltip";

afterEach(() => { cleanup(); vi.restoreAllMocks(); });

it("opens help on hover or focus, links it accessibly, and dismisses on Escape", () => {
  render(<MetricTooltip label="Firmware" help="Firmware version currently running on the DAC." />);
  const trigger = screen.getByLabelText(/^About Firmware:/);
  const content = document.querySelector('[role="tooltip"]')!;
  const showPopover = vi.fn();
  const hidePopover = vi.fn();
  Object.defineProperties(content, { showPopover: { value: showPopover }, hidePopover: { value: hidePopover } });
  expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
  fireEvent.mouseEnter(trigger);
  expect(showPopover).toHaveBeenCalledOnce();
  const tooltip = screen.getByRole("tooltip");
  expect(tooltip).toHaveAttribute("popover", "manual");
  expect(trigger).toHaveAttribute("aria-describedby", tooltip.id);
  fireEvent.focus(trigger);
  fireEvent.mouseLeave(trigger);
  expect(tooltip).toBeVisible();
  fireEvent.keyDown(trigger, { key: "Escape" });
  expect(hidePopover).toHaveBeenCalledOnce();
  expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
  fireEvent.blur(trigger);
  fireEvent.focus(trigger);
  expect(screen.getByRole("tooltip")).toBeVisible();
  fireEvent.blur(trigger);
  expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
});

it("clamps near the left edge and flips below a trigger near the top of the modal", () => {
  const rect = (left: number, top: number, width: number, height: number) => ({ left, top, width, height, right: left + width, bottom: top + height }) as DOMRect;
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockImplementation(function (this: HTMLElement) {
    if (this.classList.contains("modal-box")) return rect(100, 100, 600, 500);
    if (this.getAttribute("role") === "tooltip") return rect(0, 0, 256, 60);
    return rect(120, 120, 20, 20);
  });
  render(<div className="modal-box"><MetricTooltip label="Firmware" help="Firmware version." /></div>);
  fireEvent.focus(screen.getByLabelText(/^About Firmware:/));
  expect(screen.getByRole("tooltip")).toHaveStyle({ left: "108px", top: "148px" });
});

it("clamps near the right edge and closes when the modal scrolls or window resizes", () => {
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockImplementation(function (this: HTMLElement) {
    if (this.classList.contains("modal-box")) return { left: 100, top: 100, right: 700, bottom: 600 } as DOMRect;
    if (this.getAttribute("role") === "tooltip") return { width: 256, height: 60 } as DOMRect;
    return { left: 670, top: 550, bottom: 570, width: 20 } as DOMRect;
  });
  const { container } = render(<div className="modal-box"><MetricTooltip label="Underruns" help="Frames replaced with silence." /></div>);
  const trigger = screen.getByLabelText(/^About Underruns:/);
  fireEvent.mouseEnter(trigger);
  expect(screen.getByRole("tooltip")).toHaveStyle({ left: "436px", top: "482px" });
  fireEvent.scroll(container.firstChild!);
  expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
  fireEvent.mouseLeave(trigger);
  fireEvent.mouseEnter(trigger);
  fireEvent(window, new Event("resize"));
  expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
});
