import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { NumericInput } from "./NumericInput";

afterEach(cleanup);

describe("NumericInput", () => {
  it("keeps a leading minus while typing and commits the completed value", () => {
    const onChange = vi.fn();
    render(<NumericInput value={4.7} onChange={onChange} label="Gain" min={-24} max={24} step={0.1} />);
    const input = screen.getByRole("spinbutton", { name: "Gain" });

    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "-" } });
    expect(input).toHaveValue("-");
    expect(onChange).not.toHaveBeenCalled();

    fireEvent.change(input, { target: { value: "-4.8" } });
    expect(input).toHaveValue("-4.8");
    expect(onChange).toHaveBeenLastCalledWith(-4.8);
  });

  it("restores the current value when an incomplete draft loses focus", () => {
    const onInvalid = vi.fn();
    render(<NumericInput value={4.7} onChange={vi.fn()} onInvalid={onInvalid} label="Gain" min={-24} max={24} step={0.1} />);
    const input = screen.getByRole("spinbutton", { name: "Gain" });

    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "-" } });
    fireEvent.blur(input);

    expect(input).toHaveValue("4.7");
    expect(input).toHaveAttribute("aria-invalid", "true");
    expect(onInvalid).toHaveBeenCalledOnce();
  });

  it("reports an out-of-range draft without committing it", () => {
    const onChange = vi.fn();
    const onInvalid = vi.fn();
    render(<NumericInput value={4.7} onChange={onChange} onInvalid={onInvalid} label="Gain" min={-24} max={24} step={0.1} />);
    const input = screen.getByRole("spinbutton", { name: "Gain" });

    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "25" } });
    fireEvent.blur(input);

    expect(input).toHaveValue("4.7");
    expect(onChange).not.toHaveBeenCalled();
    expect(onInvalid).toHaveBeenCalledOnce();
  });
});
