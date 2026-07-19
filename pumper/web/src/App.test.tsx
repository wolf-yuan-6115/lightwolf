import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import App from "./App";
import { PumperHidTransport } from "./hidTransport";
import {
  defaultConfig,
  encodeBand,
  encodeGlobal,
  type EqConfig,
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

function mockConnectedPumper(deviceConfig: EqConfig = defaultConfig, rejectRequest?: (opcode: Opcode) => Error | null) {
  const device = { vendorId: 0x2e8a, productId: 0xf10a } as HIDDevice;
  Object.defineProperty(navigator, "hid", {
    configurable: true,
    value: { getDevices: vi.fn().mockResolvedValue([device]) },
  });
  vi.spyOn(PumperHidTransport.prototype, "open").mockResolvedValue();
  vi.spyOn(PumperHidTransport.prototype, "close").mockResolvedValue();

  let activeProfile = 0;
  const request = vi.spyOn(PumperHidTransport.prototype, "request").mockImplementation(async (opcode, payload) => {
    const rejection = rejectRequest?.(opcode) ?? null;
    if (rejection) throw rejection;
    const requestPayload = payload ?? new Uint8Array();
    let responsePayload: Uint8Array<ArrayBufferLike> = new Uint8Array();
    if (opcode === Opcode.Hello || opcode === Opcode.GetStatus) {
      responsePayload = new Uint8Array(32);
      const view = new DataView(responsePayload.buffer);
      responsePayload.set([1, 8, 10, 0x04]);
      view.setUint32(4, 48000, true);
      view.setInt32(28, 42375, true);
    } else if (opcode === Opcode.GetGlobal) {
      responsePayload = encodeGlobal(deviceConfig);
    } else if (opcode === Opcode.GetProfiles) {
      responsePayload = new Uint8Array(12);
      const view = new DataView(responsePayload.buffer);
      responsePayload.set([10, activeProfile, 0]);
      view.setUint16(4, 0x0003, true);
    } else if (opcode === Opcode.GetBand) {
      const index = requestPayload[0];
      responsePayload = encodeBand(index, deviceConfig.bands[index]);
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

function getProfileOption(label: string) {
  const listbox = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Profile"]');
  if (!listbox) throw new Error("Profile listbox was not rendered");
  const labelElement = within(listbox).getByText(label);
  const option = labelElement.closest("button");
  if (!option) throw new Error(`Profile option ${label} was not rendered`);
  return option;
}

describe("Pumper controller", () => {
  it("shows a useful browser compatibility state without WebHID", () => {
    render(<App />);
    expect(screen.getByText(/WebHID is unavailable/i)).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Frequency response" })).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "Preamp mode" })).toBeInTheDocument();
    expect(screen.getByLabelText("Realtime input and output levels")).toBeInTheDocument();
    expect(screen.queryByText("Pre-EQ")).not.toBeInTheDocument();
    expect(screen.queryByText("Post-EQ")).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Profile: Profile 1 (empty)" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Make default" })).toBeDisabled();
    const clearProfileButton = screen.getByRole("button", { name: "Clear Profile 1" });
    expect(clearProfileButton).toBeDisabled();
    expect(clearProfileButton.closest(".tooltip")).toHaveAttribute("data-tip", "Clear profile");
    expect(screen.getByRole("heading", { name: "Global EQ" })).toBeInTheDocument();
    expect(screen.getByLabelText("Preamp gain")).toBeVisible();
    expect(screen.getByLabelText("Preamp gain")).toHaveAttribute("readonly");
    expect(screen.getByLabelText("Preamp gain").closest("label")).toHaveClass("w-24");
    expect(screen.getByLabelText("Preamp gain slider")).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Signal levels" })).toBeInTheDocument();
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
    let profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    expect(screen.getByLabelText("Preamp gain")).not.toHaveAttribute("readonly");

    fireEvent.click(getProfileOption("Profile 3 (empty)"));
    profileMenu = screen.getByRole("button", { name: "Profile: Profile 3 (empty)" });
    expect(screen.getByRole("button", { name: "Save profile" })).toBeEnabled();
    fireEvent.click(getProfileOption("Profile 2"));

    await waitFor(() => expect(screen.getByRole("button", { name: "Profile: Profile 2" })).toBeInTheDocument());
    expect(screen.getByRole("button", { name: "Save profile" })).toBeDisabled();
    expect(screen.queryByRole("heading", { name: "Discard unsaved changes?" })).not.toBeInTheDocument();
  });

  it("disables saving after an EQ value is changed back to its stored value", async () => {
    mockConnectedPumper({
      ...defaultConfig,
      preampDb: 4.7,
      bands: defaultConfig.bands.map((band) => ({ ...band })),
    });

    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    const saveButton = screen.getByRole("button", { name: "Save profile" });
    const preampInput = screen.getByLabelText("Preamp gain");
    expect(saveButton).toBeDisabled();

    fireEvent.change(preampInput, { target: { value: "4.8" } });
    expect(saveButton).toBeEnabled();

    fireEvent.change(preampInput, { target: { value: "4.7" } });
    expect(saveButton).toBeDisabled();
  });

  it("identifies the exact numeric control when a draft is invalid", async () => {
    mockConnectedPumper();

    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    const gainInput = screen.getByLabelText("Gain for band 3");

    fireEvent.focus(gainInput);
    fireEvent.change(gainInput, { target: { value: "-25" } });
    fireEvent.blur(gainInput);

    const message = await screen.findByText("Band 3 gain must be between -24 and 24 dB.");
    expect(message.closest(".toast")).not.toBeNull();
    expect(gainInput).toHaveAttribute("aria-invalid", "true");
    expect(gainInput).toHaveValue("0");
  });

  it("adds the changed control to a generic DAC validation error", async () => {
    mockConnectedPumper(defaultConfig, (opcode) => opcode === Opcode.SetBand ? new Error("One or more EQ values are out of range") : null);

    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    fireEvent.change(screen.getByLabelText("Gain for band 3"), { target: { value: "-1" } });

    expect(await screen.findByText("Band 3 gain: One or more EQ values are out of range")).toBeInTheDocument();
  });

  it("keeps shelf slope valid when changing a peaking filter type", async () => {
    mockConnectedPumper({
      ...defaultConfig,
      bands: defaultConfig.bands.map((band, index) => index === 0 ? { ...band, q: 4 } : { ...band }),
    });

    render(<App />);
    await screen.findByRole("button", { name: "Band 1 filter type: Peaking" });
    const filterMenu = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Band 1 filter type"]');
    if (!filterMenu) throw new Error("Band 1 filter menu was not rendered");
    const lowShelf = within(filterMenu).getByRole("option", { name: "Low shelf", hidden: true });
    fireEvent.click(lowShelf);

    expect(screen.getByLabelText("Slope for band 1")).toHaveValue("1");
    expect(screen.queryByText(/Band 1 slope must be between/i)).not.toBeInTheDocument();
  });

  it("confirms device resets and shows the BOOTSEL handoff", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const deviceButton = await screen.findByRole("button", { name: "DAC info" });
    expect(screen.queryByText("Active configuration version")).not.toBeInTheDocument();

    fireEvent.click(deviceButton);
    let modal = screen.getByRole("dialog");
    expect(within(modal).getByRole("heading", { name: "Pumper USB DAC" })).toBeInTheDocument();
    expect(within(modal).getByText("Firmware")).toBeInTheDocument();
    expect(within(modal).getByText("Chip temperature")).toBeInTheDocument();
    expect(within(modal).getByText("42.4 °C")).toBeInTheDocument();
    expect(within(modal).getByLabelText(/About Chip temperature:/)).toBeInTheDocument();
    expect(within(modal).getByText("Active configuration version")).toBeInTheDocument();
    expect(within(modal).getByText("Audio underruns")).toBeInTheDocument();
    expect(within(modal).getByText("Backpressure events")).toBeInTheDocument();
    expect(within(modal).getByLabelText(/About Active configuration version:/)).toBeInTheDocument();
    expect(within(modal).getByLabelText(/About Audio underruns:/)).toBeInTheDocument();
    expect(within(modal).getByLabelText(/About Backpressure events:/)).toBeInTheDocument();

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
