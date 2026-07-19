import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import App from "./App";
import { PumperHidTransport } from "./hidTransport";
import {
  defaultConfig,
  encodeBand,
  encodeGlobal,
  Opcode,
  ProtocolStatus,
  ResponsePacket,
} from "./protocol";

const originalHid = Object.getOwnPropertyDescriptor(navigator, "hid");
const originalLocalStorage = Object.getOwnPropertyDescriptor(window, "localStorage");
const storedValues = new Map<string, string>();
const localStorageMock: Storage = {
  get length() { return storedValues.size; },
  clear: () => storedValues.clear(),
  getItem: (key) => storedValues.get(key) ?? null,
  key: (index) => Array.from(storedValues.keys())[index] ?? null,
  removeItem: (key) => storedValues.delete(key),
  setItem: (key, value) => storedValues.set(key, value),
};

beforeEach(() => {
  storedValues.clear();
  Object.defineProperty(window, "localStorage", { configurable: true, value: localStorageMock });
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  window.localStorage.removeItem("pumper-theme");
  document.documentElement.removeAttribute("data-theme");
  document.documentElement.style.removeProperty("color-scheme");
  if (originalLocalStorage) Object.defineProperty(window, "localStorage", originalLocalStorage);
  else Reflect.deleteProperty(window, "localStorage");
  if (originalHid) Object.defineProperty(navigator, "hid", originalHid);
  else Reflect.deleteProperty(navigator, "hid");
});

function mockConnectedPumper() {
  const device = { vendorId: 0x2e8a, productId: 0xf10a } as HIDDevice;
  Object.defineProperty(navigator, "hid", {
    configurable: true,
    value: { getDevices: vi.fn().mockResolvedValue([device]) },
  });
  vi.spyOn(PumperHidTransport.prototype, "open").mockResolvedValue();
  vi.spyOn(PumperHidTransport.prototype, "close").mockResolvedValue();

  let activeProfile = 0;
  const request = vi.spyOn(PumperHidTransport.prototype, "request").mockImplementation(async (opcode, payload) => {
    const requestPayload = payload ?? new Uint8Array();
    let responsePayload: Uint8Array<ArrayBufferLike> = new Uint8Array();
    if (opcode === Opcode.Hello || opcode === Opcode.GetStatus) {
      responsePayload = new Uint8Array(28);
      const view = new DataView(responsePayload.buffer);
      responsePayload.set([1, 7, 10, 0x04]);
      view.setUint32(4, 48000, true);
    } else if (opcode === Opcode.GetGlobal) {
      responsePayload = encodeGlobal(defaultConfig);
    } else if (opcode === Opcode.GetProfiles) {
      responsePayload = new Uint8Array(12);
      const view = new DataView(responsePayload.buffer);
      responsePayload.set([10, activeProfile, 0]);
      view.setUint16(4, 0x0003, true);
    } else if (opcode === Opcode.GetBand) {
      const index = requestPayload[0];
      responsePayload = encodeBand(index, defaultConfig.bands[index]);
    } else if (opcode === Opcode.LoadProfile) {
      activeProfile = requestPayload[0];
    }
    return {
      opcode: opcode | 0x80,
      requestId: 1,
      status: ProtocolStatus.Ok,
      payload: responsePayload,
    } satisfies ResponsePacket;
  });

  return { request };
}

describe("Pumper controller", () => {
  it("shows a useful browser compatibility state without WebHID", () => {
    render(<App />);
    expect(screen.getByText(/WebHID is unavailable/i)).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Frequency response" })).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "Preamp mode" })).toBeInTheDocument();
    expect(screen.getByLabelText("Realtime stereo input and output levels")).toBeInTheDocument();
    expect(screen.getByText("Pre-EQ")).toBeInTheDocument();
    expect(screen.getByText("Post-EQ")).toBeInTheDocument();
    expect(screen.getByLabelText("Profile")).toHaveValue("0");
    expect(screen.getByRole("button", { name: "Make default" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Delete Profile 1" })).toBeDisabled();
    expect(screen.getByRole("heading", { name: "Global EQ" })).toBeInTheDocument();
    expect(screen.getByLabelText("Preamp gain")).toBeVisible();
    expect(screen.getByLabelText("Preamp gain")).toHaveAttribute("readonly");
    expect(screen.getByRole("heading", { name: "Stereo levels" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Filter configuration" })).toBeInTheDocument();
    expect(screen.getAllByLabelText(/Enable band \d+/)).toHaveLength(10);
  });

  it("switches between built-in themes and persists the selection", () => {
    render(<App />);

    expect(document.documentElement).toHaveAttribute("data-theme", "light");
    fireEvent.click(screen.getByLabelText("Use dark theme"));

    expect(document.documentElement).toHaveAttribute("data-theme", "dark");
    expect(window.localStorage.getItem("pumper-theme")).toBe("dark");
    expect(screen.getByLabelText("Use light theme")).toBeChecked();
  });

  it("shows a connection failure without embedding system setup commands", async () => {
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: {
        getDevices: vi.fn().mockResolvedValue([]),
        requestDevice: vi.fn().mockRejectedValue(new Error("Linux denied access to Pumper")),
      },
    });
    render(<App />);
    expect(screen.queryByText("Not connected")).not.toBeInTheDocument();
    expect(screen.queryByRole("heading", { name: "Connect Pumper" })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Connect DAC" }));

    expect(await screen.findByRole("heading", { name: "Connection failed" })).toBeInTheDocument();
    expect(screen.getByText("Linux denied access to Pumper")).toBeInTheDocument();
    expect(screen.queryByText(/udev/i)).not.toBeInTheDocument();
  });

  it("shows a canceled device selection as a toast", async () => {
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: {
        getDevices: vi.fn().mockResolvedValue([]),
        requestDevice: vi.fn().mockResolvedValue([]),
      },
    });

    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: "Connect DAC" }));

    const message = await screen.findByText("No Pumper DAC was selected");
    expect(message.closest(".toast")).not.toBeNull();
    expect(screen.queryByRole("heading", { name: "Connection failed" })).not.toBeInTheDocument();
  });

  it("switches away from an empty profile without claiming the EQ was edited", async () => {
    mockConnectedPumper();

    render(<App />);
    const select = await screen.findByLabelText("Profile");
    await waitFor(() => expect(select).toBeEnabled());
    expect(screen.getByLabelText("Preamp gain")).not.toHaveAttribute("readonly");

    fireEvent.change(select, { target: { value: "2" } });
    expect(select).toHaveValue("2");
    fireEvent.change(select, { target: { value: "1" } });

    await waitFor(() => expect(select).toHaveValue("1"));
    expect(screen.queryByRole("heading", { name: "Discard unsaved changes?" })).not.toBeInTheDocument();
  });

  it("confirms device resets and shows the BOOTSEL handoff", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const deviceButton = await screen.findByRole("button", { name: "Pumper USB DAC" });
    expect(screen.queryByText("EQ revision")).not.toBeInTheDocument();

    fireEvent.click(deviceButton);
    let modal = screen.getByRole("dialog");
    expect(within(modal).getByRole("heading", { name: "Pumper USB DAC" })).toBeInTheDocument();
    expect(within(modal).getByText("Firmware")).toBeInTheDocument();
    expect(within(modal).getByText("EQ revision")).toBeInTheDocument();
    expect(within(modal).getByText("Audio underruns")).toBeInTheDocument();
    expect(within(modal).getByText("Backpressure events")).toBeInTheDocument();

    fireEvent.click(within(modal).getByRole("button", { name: "Restart" }));
    modal = screen.getByRole("dialog");
    expect(within(modal).getByRole("heading", { name: "Restart DAC?" })).toBeInTheDocument();
    fireEvent.click(within(modal).getByRole("button", { name: "Restart" }));
    await waitFor(() => expect(request).toHaveBeenCalledWith(Opcode.RestartDevice));

    fireEvent.click(deviceButton);
    modal = screen.getByRole("dialog");
    fireEvent.click(within(modal).getByRole("button", { name: "Firmware update" }));
    modal = screen.getByRole("dialog");
    expect(within(modal).getByRole("heading", { name: "Enter BOOTSEL mode?" })).toBeInTheDocument();
    fireEvent.click(within(modal).getByRole("button", { name: "Enter BOOTSEL" }));

    modal = await screen.findByRole("dialog");
    expect(within(modal).getByRole("heading", { name: "Firmware update" })).toBeInTheDocument();
    expect(within(modal).getByText("RP2350")).toBeInTheDocument();
    expect(request).toHaveBeenCalledWith(Opcode.EnterBootsel);
  });
});
