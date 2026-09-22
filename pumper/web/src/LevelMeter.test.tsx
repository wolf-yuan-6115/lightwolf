import { act, render, screen, within } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { LevelMeter } from "./LevelMeter";

describe("LevelMeter", () => {
  it("shows distinct input and output levels with shared warning colors", () => {
    const { container } = render(
      <LevelMeter
        level={{
          sequence: 1,
          preEq: {
            leftPeak: 32767,
            rightPeak: 32768,
            leftMeanSquare: 16384 * 16384,
            rightMeanSquare: 32768 * 32768,
          },
          postEq: {
            leftPeak: 16384,
            rightPeak: 16384,
            leftMeanSquare: 16384 * 16384,
            rightMeanSquare: 16384 * 16384,
          },
          limiterActive: false,
        }}
      />,
    );

    expect(screen.getByLabelText("L input 0.0 dBFS")).toBeInTheDocument();
    expect(screen.getByLabelText("L output -6.0 dBFS")).toBeInTheDocument();
    expect(container.querySelector(".bg-indigo-500")).toBeInTheDocument();
    expect(container.querySelector(".bg-emerald-500")).toBeInTheDocument();
    expect(container.querySelector(".bg-amber-400")).toBeInTheDocument();
    expect(container.querySelector(".bg-red-500")).toBeInTheDocument();
    const leftInputSegments = container.querySelector('[aria-label="L input 0.0 dBFS"] [aria-hidden="true"]');
    expect(leftInputSegments?.lastElementChild).toHaveClass("bg-red-500");
    expect(screen.getByRole("status", { name: "Limiter inactive" })).toHaveClass("opacity-25");
  });

  it("holds the limiter indicator for one second after the latest active report", () => {
    vi.useFakeTimers();
    const level = {
      sequence: 1,
      preEq: { leftPeak: 0, rightPeak: 0, leftMeanSquare: 0, rightMeanSquare: 0 },
      postEq: { leftPeak: 0, rightPeak: 0, leftMeanSquare: 0, rightMeanSquare: 0 },
      limiterActive: true,
    };
    const { container, rerender } = render(<LevelMeter level={level} />);

    expect(within(container).getByRole("status", { name: "Limiter active" })).toHaveClass("opacity-100");
    act(() => vi.advanceTimersByTime(750));
    rerender(<LevelMeter level={{ ...level, sequence: 2 }} />);
    act(() => vi.advanceTimersByTime(999));
    expect(within(container).getByRole("status", { name: "Limiter active" })).toBeInTheDocument();
    act(() => vi.advanceTimersByTime(1));
    expect(within(container).getByRole("status", { name: "Limiter inactive" })).toBeInTheDocument();
    vi.useRealTimers();
  });
});
