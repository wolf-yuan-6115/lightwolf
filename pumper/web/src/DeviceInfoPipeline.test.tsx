import { cleanup, render, screen, within } from "@testing-library/react";
import { afterEach, expect, it } from "vitest";
import { DeviceInfoPipeline } from "./DeviceInfoPipeline";
import { CrossfeedMode, defaultConfig, defaultOutputProcessing, type DeviceStatus, type DeviceTelemetry } from "./protocol";

afterEach(cleanup);

const status: DeviceStatus = {
  firmwareVersion: "3.4", bandCount: 10, streaming: true, dirty: false,
  eqEnabled: true, sampleRateHz: 48000, bitDepth: 24, configGeneration: 303,
  savedGeneration: 303, appliedGeneration: 303, underrunFrames: 0,
  backpressureEvents: 2, temperatureC: 37.7, systemClockMHz: 150,
  maxDspBlockUs: 232, i2sLowWaterFrames: 95,
};
const telemetry: DeviceTelemetry = {
  streaming: true, feedbackActive: true, uptimeSeconds: 400,
  streamDurationMs: 199000, i2sBufferedFrames: 144, i2sHighWaterFrames: 193,
  feedbackCorrectionPpm: -12, averageDspLoadPercent: 22.65, peakDspLoadPercent: 22.8,
  limiterActiveMs: 10, usbAudioPackets: 5500332, streamStarts: 35,
  malformedHidReports: 0, busyHidReports: 1, flashFailures: 0, meterReports: 42,
};
const props = {
  status, telemetry, eq: { ...defaultConfig, enabled: true, preampDb: -3 },
  audio: {
    master: { volumeDb: -12, muted: false },
    left: { volumeDb: 0, muted: true },
    right: { volumeDb: -1.5, muted: false },
  },
  crossfeed: { mode: CrossfeedMode.High, strengthPercent: 11, cutoffHz: 900, delayMs: 0.1 },
  outputProcessing: { ...defaultOutputProcessing, mono: true, swap: true, invertRight: true, balancePercent: -20, widthPercent: 80 },
};

function stage(title: string) {
  return within(screen.getByRole("heading", { name: title }).closest("article")!);
}

it("shows the complete processing order and live settings, not retained custom preset values", () => {
  render(<DeviceInfoPipeline {...props} />);
  const flow = screen.getByRole("list", { name: "Audio signal flow" });
  expect(within(flow).getAllByRole("heading").map((heading) => heading.textContent)).toEqual([
    "USB Audio In", "EQ & preamp", "Crossfeed", "Host gain", "Output processing", "Limiter", "I²S Audio Out",
  ]);
  expect(flow).toHaveAttribute("data-flow-state", "streaming");
  for (const value of ["High", "30%", "700 Hz", "0.3 ms"]) expect(stage("Crossfeed").getByText(value)).toBeVisible();
  for (const value of ["-12 dB", "Muted", "-1.5 dB"]) expect(stage("Host gain").getByText(value)).toBeVisible();
  for (const value of ["Mono", "80%", "20% left", "Swapped", "Right inverted"]) expect(stage("Output processing").getByText(value)).toBeVisible();
  expect(stage("EQ & preamp").getByText("-3 dB")).toBeVisible();
  expect(stage("USB Audio In").getByText("3m 19s")).toBeVisible();
  expect(stage("USB Audio In").getByText("-12 ppm")).toBeVisible();
  expect(stage("I²S Audio Out").getByText("144 frames")).toBeVisible();
  expect(within(screen.getByRole("region", { name: "Processing totals" })).getByText("22.65%")).toBeVisible();
  for (const help of screen.getAllByLabelText(/^About /)) expect(help).toHaveAttribute("tabindex", "0");
});

it("renders custom crossfeed and neutral stereo settings", () => {
  render(<DeviceInfoPipeline {...props} crossfeed={{ ...props.crossfeed, mode: CrossfeedMode.Custom }} outputProcessing={defaultOutputProcessing} />);
  for (const value of ["Custom", "11%", "900 Hz", "0.1 ms"]) expect(stage("Crossfeed").getByText(value)).toBeVisible();
  for (const value of ["Stereo", "100%", "Center", "L → L · R → R", "Normal"]) expect(stage("Output processing").getByText(value)).toBeVisible();
});

it("keeps unavailable settings unknown and uses telemetry before status for flow state", () => {
  const { rerender } = render(<DeviceInfoPipeline {...props} telemetry={null} audio={null} crossfeed={null} outputProcessing={null} />);
  const flow = screen.getByRole("list", { name: "Audio signal flow" });
  expect(flow).toHaveAttribute("data-flow-state", "streaming");
  expect(stage("Host gain").getAllByText("—")).toHaveLength(3);
  expect(stage("Crossfeed").getAllByText("—")).toHaveLength(4);
  expect(stage("Output processing").getAllByText("—")).toHaveLength(5);
  rerender(<DeviceInfoPipeline {...props} telemetry={{ ...telemetry, streaming: false }} />);
  expect(flow).toHaveAttribute("data-flow-state", "idle");
  rerender(<DeviceInfoPipeline {...props} status={null} telemetry={null} />);
  expect(flow).toHaveAttribute("data-flow-state", "idle");
  expect(stage("EQ & preamp").getAllByText("—")).toHaveLength(4);
});
