import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, expect, it } from "vitest";
import { UsbAudioState } from "./AudioSettings";
import type { useAudioSettings } from "./useAudioSettings";

afterEach(cleanup);
it("shows Pinned while preserving the mute indication", () => {
  const settings = { supported: true, acknowledgedPin: true, audio: { master: { volumeDb: -30, muted: true } } } as ReturnType<typeof useAudioSettings>;
  render(<UsbAudioState settings={settings} connected busy={false} sampleRateHz={48000} bitDepth={24} streaming />);
  expect(screen.getByText("Muted · Pinned")).toBeTruthy();
  expect(screen.queryByText(/-30/)).toBeNull();
});
it("shows host volume when following USB", () => {
  const settings = { supported: true, acknowledgedPin: false, audio: { master: { volumeDb: -30, muted: false } } } as ReturnType<typeof useAudioSettings>;
  render(<UsbAudioState settings={settings} connected busy={false} sampleRateHz={48000} bitDepth={24} streaming />);
  expect(screen.getByText("-30 dB")).toBeTruthy();
});
