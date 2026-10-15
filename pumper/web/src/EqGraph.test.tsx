import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { EqGraph } from "./EqGraph";
import { defaultConfig, FilterType, WidthMode } from "./protocol";

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("EQ graph editing", () => {
  it("fine-tunes frequency and gain from the keyboard, with larger Shift steps", () => {
    const onChangeBand = vi.fn();
    const onSelectBand = vi.fn();
    render(<EqGraph config={defaultConfig} sampleRateHz={48000} selectedBand={0} onSelectBand={onSelectBand} onChangeBand={onChangeBand} />);
    const handle = screen.getByRole("button", { name: "Band 1 graph handle" });
    fireEvent.keyDown(handle, { key: "ArrowUp" });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { gainDb: 0.1 });
    fireEvent.keyDown(handle, { key: "ArrowDown", shiftKey: true });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { gainDb: -1 });
    fireEvent.keyDown(handle, { key: "ArrowRight" });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { frequencyHz: 69 });
    fireEvent.keyDown(handle, { key: "ArrowRight", shiftKey: true });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { frequencyHz: 76 });
    expect(onSelectBand).toHaveBeenCalledWith(0);
  });

  it("keeps keyboard edits in bounds and gain-independent filters frequency-only", () => {
    const config = { ...defaultConfig, bands: defaultConfig.bands.map((band, index) => index === 0 ? { ...band, frequencyHz: 20000, gainDb: 24 } : index === 1 ? { ...band, type: FilterType.LowPass, widthMode: WidthMode.Q } : band) };
    const onChangeBand = vi.fn();
    render(<EqGraph config={config} sampleRateHz={48000} selectedBand={0} onSelectBand={vi.fn()} onChangeBand={onChangeBand} />);
    fireEvent.keyDown(screen.getByLabelText("Band 1 graph handle"), { key: "ArrowUp", shiftKey: true });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { gainDb: 24 });
    fireEvent.keyDown(screen.getByLabelText("Band 1 graph handle"), { key: "ArrowRight", shiftKey: true });
    expect(onChangeBand).toHaveBeenLastCalledWith(0, { frequencyHz: 20000 });
    onChangeBand.mockClear();
    fireEvent.keyDown(screen.getByLabelText("Band 2 graph handle"), { key: "ArrowUp" });
    expect(onChangeBand).not.toHaveBeenCalled();
    fireEvent.keyDown(screen.getByLabelText("Band 2 graph handle"), { key: "ArrowLeft" });
    expect(onChangeBand.mock.calls[0][1]).toHaveProperty("frequencyHz");
    expect(onChangeBand.mock.calls[0][1]).not.toHaveProperty("gainDb");
  });

  it("blocks graph interaction while disconnected or saving", () => {
    const onChangeBand = vi.fn();
    const onSelectBand = vi.fn();
    const { container } = render(<EqGraph config={defaultConfig} sampleRateHz={48000} selectedBand={0} onSelectBand={onSelectBand} onChangeBand={onChangeBand} interactive={false} />);
    const handle = screen.getByLabelText("Band 1 graph handle");
    expect(handle).toHaveAttribute("tabindex", "-1");
    expect(handle).toHaveAttribute("aria-disabled", "true");
    fireEvent.keyDown(handle, { key: "ArrowUp" });
    fireEvent.click(handle);
    fireEvent.pointerDown(handle, { pointerId: 1 });
    fireEvent.pointerMove(container.querySelector("svg")!, { clientX: 200, clientY: 20 });
    expect(onChangeBand).not.toHaveBeenCalled();
    expect(onSelectBand).not.toHaveBeenCalled();
  });

  it("keeps drag coordinates and axis labels correct after a phone resize", () => {
    let resize!: ResizeObserverCallback;
    const disconnect = vi.fn();
    vi.stubGlobal("ResizeObserver", class {
      constructor(callback: ResizeObserverCallback) { resize = callback; }
      observe() {}
      disconnect = disconnect;
    });
    const onChangeBand = vi.fn();
    const view = render(<EqGraph config={defaultConfig} sampleRateHz={48000} selectedBand={0} onSelectBand={vi.fn()} onChangeBand={onChangeBand} spectrumEnabled />);
    act(() => resize([{ contentRect: { width: 360 } } as ResizeObserverEntry], {} as ResizeObserver));
    const svg = view.container.querySelector("svg")!;
    const capture = vi.fn();
    Object.defineProperty(svg, "setPointerCapture", { value: capture });
    expect(svg).toHaveAttribute("viewBox", "0 0 360 280");
    expect(svg).toHaveTextContent("20k");
    expect(svg).not.toHaveTextContent("10k");
    Object.defineProperty(svg, "getBoundingClientRect", { value: () => ({ left: 10, top: 20, width: 360, height: 280 }) });
    const handle = screen.getByLabelText("Band 1 graph handle");
    Object.defineProperty(handle, "setPointerCapture", { value: vi.fn() });
    fireEvent.pointerDown(handle, { pointerId: 1 });
    expect(capture).toHaveBeenCalledWith(1);
    // At this size, x=181 is the log-frequency midpoint and y=75.5 is +6 dB.
    fireEvent.pointerMove(svg, { clientX: 191, clientY: 95.5 });
    expect(onChangeBand).toHaveBeenCalledWith(0, { frequencyHz: 630, gainDb: 6 });
    view.unmount();
    expect(disconnect).toHaveBeenCalledOnce();
  });

  it("pins gain-independent handles to zero and changes only frequency while dragging", () => {
    const config = {
      ...defaultConfig,
      bands: defaultConfig.bands.map((band, index) => index === 0 ? {
        ...band,
        type: FilterType.LowPass,
        widthMode: WidthMode.Q,
        frequencyHz: 1000,
        gainDb: 12,
      } : { ...band }),
    };
    const onChangeBand = vi.fn();
    const { container } = render(<EqGraph config={config} sampleRateHz={48000} selectedBand={0} onSelectBand={vi.fn()} onChangeBand={onChangeBand} />);
    const handle = screen.getByLabelText("Band 1 graph handle");
    Object.defineProperty(handle, "setPointerCapture", { configurable: true, value: vi.fn() });
    const svg = container.querySelector("svg")!;
    Object.defineProperty(svg, "getBoundingClientRect", {
      configurable: true,
      value: () => ({ left: 0, top: 0, width: 1000, height: 350, right: 1000, bottom: 350, x: 0, y: 0, toJSON: () => ({}) }),
    });

    expect(handle).toHaveAttribute("cy", "129.5");
    fireEvent.pointerDown(handle, { pointerId: 1 });
    fireEvent.pointerMove(svg, { clientX: 500, clientY: 20 });
    expect(onChangeBand).toHaveBeenCalledOnce();
    expect(onChangeBand.mock.calls[0][0]).toBe(0);
    expect(onChangeBand.mock.calls[0][1]).toHaveProperty("frequencyHz");
    expect(onChangeBand.mock.calls[0][1]).not.toHaveProperty("gainDb");
  });
  it("maps mobile pointer coordinates correctly with the spectrum scale visible", () => {
    const onChangeBand = vi.fn();
    const { container } = render(<EqGraph config={defaultConfig} sampleRateHz={48000} selectedBand={0} onSelectBand={vi.fn()} onChangeBand={onChangeBand} spectrumEnabled />);
    const svg = container.querySelector("svg")!;
    Object.defineProperty(svg, "getBoundingClientRect", { value: () => ({ left: 10, top: 20, width: 500, height: 260 }) });
    const handle = screen.getByLabelText("Band 1 graph handle");
    Object.defineProperty(handle, "setPointerCapture", { value: vi.fn() });
    expect(container).not.toHaveTextContent("Output spectrum · dBFS");
    const scale = 500 / 1035;
    const offsetY = (260 - 350 * scale) / 2;
    fireEvent.pointerDown(handle, { pointerId: 1 });
    // ViewBox y=93 represents +6 dB, independently of the letterbox padding.
    fireEvent.pointerMove(svg, { clientX: 10 + 500 * scale, clientY: 20 + offsetY + 93 * scale });
    expect(onChangeBand.mock.calls[0][1].gainDb).toBe(6);
    expect(container.querySelector('[aria-label="Output spectrum in dBFS"]')).toHaveAttribute("pointer-events", "none");
  });

});
