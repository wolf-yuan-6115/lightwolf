import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { EqGraph } from "./EqGraph";
import { defaultConfig, FilterType, WidthMode } from "./protocol";

afterEach(cleanup);

describe("EQ graph editing", () => {
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
