import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import App from "./App";
import { PumperHidTransport } from "./hidTransport";
import { audioSettingsMock } from "./test/audioSettingsMock";
import {
  defaultConfig,
  decodeBand,
  decodeGlobal,
  encodeBand,
  encodeGlobal,
  type EqConfig,
  Opcode,
  ProtocolStatus,
  ResponsePacket,
  CrossfeedMode,
  decodeCrossfeed,
  decodeOutputProcessing,
  FilterType,
  WidthMode,
} from "./protocol";

const originalHid = Object.getOwnPropertyDescriptor(navigator, "hid");
const originalClipboard = Object.getOwnPropertyDescriptor(navigator, "clipboard");
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
  vi.useRealTimers();
  vi.restoreAllMocks();
  window.localStorage.removeItem("pumper-theme");
  document.documentElement.removeAttribute("data-theme");
  document.documentElement.style.removeProperty("color-scheme");
  if (originalLocalStorage) Object.defineProperty(window, "localStorage", originalLocalStorage);
  else Reflect.deleteProperty(window, "localStorage");
  if (originalHid) Object.defineProperty(navigator, "hid", originalHid);
  else Reflect.deleteProperty(navigator, "hid");
  if (originalClipboard) Object.defineProperty(navigator, "clipboard", originalClipboard);
  else Reflect.deleteProperty(navigator, "clipboard");
});

function mockConnectedPumper(deviceConfig: EqConfig = defaultConfig, rejectRequest?: (opcode: Opcode) => Error | null, version = [1, 8], statusPayloadSize = 48) {
  const audioMock = audioSettingsMock();
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
    if ([Opcode.GetAudioControls, Opcode.GetCrossfeed, Opcode.SetCrossfeed, Opcode.SaveCrossfeed, Opcode.GetOutputProcessing, Opcode.SetOutputProcessing, Opcode.SaveOutputProcessing].includes(opcode)) {
      return audioMock.handle(opcode, new Uint8Array(requestPayload));
    }
    let responsePayload: Uint8Array<ArrayBufferLike> = new Uint8Array();
    if (opcode === Opcode.Hello || opcode === Opcode.GetStatus) {
      responsePayload = new Uint8Array(statusPayloadSize);
      const view = new DataView(responsePayload.buffer);
      responsePayload.set([...version, 10, 0x04]);
      view.setUint32(4, 48000, true);
      view.setInt32(28, 42375, true);
      if (statusPayloadSize >= 48) responsePayload[44] = 16;
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

  return { request, audioMock };
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
  it("copies the current EQ as parametric text", async () => {
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.defineProperty(navigator, "clipboard", { configurable: true, value: { writeText } });
    mockConnectedPumper({ ...defaultConfig, preampDb: -3.5 });
    render(<App />);
    const copyButton = await screen.findByRole("button", { name: "Copy EQ" });
    await waitFor(() => expect(copyButton).toBeEnabled());

    fireEvent.click(copyButton);

    await waitFor(() => expect(writeText).toHaveBeenCalledOnce());
    expect(writeText.mock.calls[0][0]).toContain("Preamp: -3.5 dB");
    expect(writeText.mock.calls[0][0]).toContain("Filter 1: ON PK Fc 68 Hz Gain 0 dB BW 1.89 oct");
    expect(await screen.findByText("Parametric EQ text copied")).toBeInTheDocument();
  });

  it("imports text into live preview without saving flash", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const importButton = await screen.findByRole("button", { name: "Import EQ" });
    await waitFor(() => expect(importButton).toBeEnabled());
    request.mockClear();

    fireEvent.click(importButton);
    fireEvent.change(screen.getByLabelText("Parametric EQ text"), {
      target: { value: "Equalizer: ON\nPreamp: -6 dB\nFilter 1: ON PK Fc 120 Hz Gain 4 dB Q 1.2" },
    });
    fireEvent.click(screen.getByRole("button", { name: "Import to preview" }));

    expect(await screen.findByText("Parametric EQ imported into live preview")).toBeInTheDocument();
    expect(screen.getByLabelText("Preamp gain")).toHaveValue("-6");
    expect(screen.getByLabelText("Frequency for band 1")).toHaveValue("120");
    expect(screen.getByLabelText("Enable band 2")).not.toBeChecked();
    expect(screen.getByRole("button", { name: "Save profile" })).toBeEnabled();
    await waitFor(() => expect(request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand)).toHaveLength(10));
    expect(request.mock.calls.some(([opcode]) => opcode === Opcode.SaveProfile)).toBe(false);
  });

  it("keeps the import dialog open when EQ text is invalid", async () => {
    mockConnectedPumper();
    render(<App />);
    const importButton = await screen.findByRole("button", { name: "Import EQ" });
    await waitFor(() => expect(importButton).toBeEnabled());

    fireEvent.click(importButton);
    fireEvent.change(screen.getByLabelText("Parametric EQ text"), { target: { value: "Filter 1: ON PK Fc 2 Hz Gain 0 dB Q 1" } });
    fireEvent.click(screen.getByRole("button", { name: "Import to preview" }));

    expect(screen.getByRole("dialog", { name: "Import parametric EQ" })).toBeInTheDocument();
    expect(screen.getByRole("alert")).toHaveTextContent("Filter 1 frequency must be between 20 and 20000");
  });

  it("keeps new audio controls disabled on old firmware without sending unsupported commands", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    expect(screen.getAllByText("Requires firmware 2.2")).toHaveLength(2);
    expect(screen.getByRole("button", { name: "Crossfeed mode: Off" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Save crossfeed" })).toBeDisabled();
    expect(screen.getByText("Requires firmware 3.0")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Save output" })).toBeDisabled();
    expect(request.mock.calls.some(([opcode]) => [Opcode.GetAudioControls, Opcode.GetCrossfeed].includes(opcode))).toBe(false);
    expect(request.mock.calls.some(([opcode]) => opcode === Opcode.GetOutputProcessing)).toBe(false);
    const filterMenu = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Band 1 filter type"]')!;
    expect(within(filterMenu).queryByRole("option", { name: "Low-pass", hidden: true })).not.toBeInTheDocument();
  });

  it("displays host controls without editable volume controls", async () => {
    const { audioMock } = mockConnectedPumper(defaultConfig, undefined, [2, 2]);
    const view = new DataView(audioMock.audio.buffer);
    view.setInt16(0, -10 * 256, true);
    view.setInt16(2, -5 * 256, true);
    audioMock.audio[8] = 1;
    render(<App />);
    const group = screen.getByRole("group", { name: "Host USB audio controls" });
    expect(await within(group).findByText("-10 dB")).toBeInTheDocument();
    expect(within(group).queryByText("Left (effective)")).not.toBeInTheDocument();
    expect(within(group).queryByText("Right (effective)")).not.toBeInTheDocument();
    expect(within(group).queryByRole("slider")).not.toBeInTheDocument();
    expect(within(group).queryByRole("button")).not.toBeInTheDocument();
    const globalPane = screen.getByRole("heading", { name: "Global EQ" }).closest("aside")!;
    expect(within(globalPane).getByRole("group", { name: "Host USB audio controls" })).toBe(group);
    expect(within(globalPane).getByText("Sample rate")).toBeInTheDocument();
    expect(within(globalPane).getByText("Bit depth")).toBeInTheDocument();
    expect(within(globalPane).getByText("16-bit")).toBeInTheDocument();
    expect(within(globalPane).getByText("Stream state")).toBeInTheDocument();
    expect(within(group).getByText("Master volume")).toBeInTheDocument();
    const filters = screen.getByRole("heading", { name: "Filter configuration" }).closest("section")!;
    const crossfeed = screen.getByRole("heading", { name: "Headphone crossfeed" }).closest("section")!;
    expect(filters.compareDocumentPosition(crossfeed) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
  });

  it("omits bit depth for firmware with the legacy status payload", async () => {
    mockConnectedPumper(defaultConfig, undefined, [3, 0], 44);
    render(<App />);
    await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    expect(screen.queryByText("Bit depth")).not.toBeInTheDocument();
  });

  it("shows each preset's parameters read-only and restores retained Custom settings", async () => {
    const { request } = mockConnectedPumper(defaultConfig, undefined, [2, 2]);
    render(<App />);
    const mode = await screen.findByRole("button", { name: "Crossfeed mode: Off" });
    await waitFor(() => expect(mode).toBeEnabled());
    const chooseMode = (name: string) => fireEvent.click(within(document.querySelector<HTMLElement>('[role="listbox"][aria-label="Crossfeed mode"]')!).getByRole("option", { name, hidden: true }));
    for (const [name, strength, delay] of [["Low", "10", "0.2"], ["Medium", "20", "0.25"], ["High", "30", "0.3"], ["Off", "0", "0"]]) {
      chooseMode(name);
      expect(screen.getByLabelText("Crossfeed strength")).toHaveValue(strength);
      expect(screen.getByLabelText("Crossfeed cutoff")).toHaveValue("700");
      expect(screen.getByLabelText("Crossfeed delay")).toHaveValue(delay);
      for (const control of ["strength", "cutoff", "delay"]) {
        expect(screen.getByLabelText(`Crossfeed ${control}`)).toHaveAttribute("readonly");
        expect(screen.getByLabelText(`Crossfeed ${control} slider`)).toBeDisabled();
      }
    }
    request.mockClear();
    fireEvent.keyDown(screen.getByLabelText("Crossfeed strength"), { key: "ArrowUp" });
    expect(screen.getByLabelText("Crossfeed strength")).toHaveValue("0");
    expect(request).not.toHaveBeenCalled();
    chooseMode("Custom");
    expect(screen.getByLabelText("Crossfeed strength")).toHaveValue("20");
    expect(screen.getByLabelText("Crossfeed delay")).toHaveValue("0.25");
    expect(screen.getByLabelText("Crossfeed strength slider")).toBeEnabled();
    expect(screen.getByLabelText("Crossfeed strength")).not.toHaveAttribute("readonly");
  });

  it("preserves Custom values across presets and saves independently of EQ", async () => {
    const { request, audioMock } = mockConnectedPumper(defaultConfig, undefined, [2, 2]);
    render(<App />);
    const mode = await screen.findByRole("button", { name: "Crossfeed mode: Off" });
    await waitFor(() => expect(mode).toBeEnabled());
    const chooseMode = (name: string) => {
      const list = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Crossfeed mode"]')!;
      fireEvent.click(within(list).getByRole("option", { name, hidden: true }));
    };
    chooseMode("Custom");
    expect(screen.getByLabelText("Crossfeed strength")).toHaveValue("20");
    expect(screen.getByLabelText("Crossfeed cutoff")).toHaveValue("700");
    expect(screen.getByLabelText("Crossfeed delay")).toHaveValue("0.25");
    fireEvent.change(screen.getByLabelText("Crossfeed strength slider"), { target: { value: "35" } });
    fireEvent.change(screen.getByLabelText("Crossfeed cutoff slider"), { target: { value: "1000" } });
    fireEvent.change(screen.getByLabelText("Crossfeed delay slider"), { target: { value: "0.4" } });
    chooseMode("Low");
    expect(screen.getByLabelText("Crossfeed strength")).toHaveValue("10");
    expect(screen.getByLabelText("Crossfeed cutoff")).toHaveValue("700");
    expect(screen.getByLabelText("Crossfeed delay")).toHaveValue("0.2");
    expect(screen.getByLabelText("Crossfeed strength")).toHaveAttribute("readonly");
    expect(screen.getByLabelText("Crossfeed strength slider")).toBeDisabled();
    chooseMode("Custom");
    expect(screen.getByLabelText("Crossfeed strength")).toHaveValue("35");
    expect(screen.getByLabelText("Crossfeed cutoff")).toHaveValue("1000");
    expect(screen.getByLabelText("Crossfeed delay")).toHaveValue("0.4");
    expect(screen.getByRole("button", { name: "Save profile" })).toBeDisabled();
    fireEvent.click(getProfileOption("Profile 2"));
    await screen.findByRole("button", { name: "Profile: Profile 2" });
    expect(screen.queryByRole("heading", { name: "Discard unsaved changes?" })).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Crossfeed mode: Custom" })).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Save crossfeed" }));
    expect(await screen.findByText("Crossfeed saved for power-on")).toBeInTheDocument();
    expect(audioMock.state.saved).toEqual({ mode: CrossfeedMode.Custom, strengthPercent: 35, cutoffHz: 1000, delayMs: 0.4 });
    const calls = request.mock.calls.filter(([opcode]) => [Opcode.SetCrossfeed, Opcode.SaveCrossfeed].includes(opcode));
    expect(calls.at(-2)?.[0]).toBe(Opcode.SetCrossfeed);
    expect(decodeCrossfeed(calls.at(-2)?.[1] as Uint8Array).strengthPercent).toBe(35);
    expect(calls.at(-1)?.[0]).toBe(Opcode.SaveCrossfeed);
    expect(screen.getByRole("button", { name: "Save crossfeed" })).toBeDisabled();
    fireEvent.click(screen.getByRole("button", { name: "Defaults" }));
    fireEvent.click(within(screen.getByRole("dialog")).getByRole("button", { name: "Restore defaults" }));
    await waitFor(() => expect(request).toHaveBeenCalledWith(Opcode.RestoreDefaults));
    expect(audioMock.state.saved.mode).toBe(CrossfeedMode.Custom);
    expect(screen.getByRole("button", { name: "Crossfeed mode: Custom" })).toBeInTheDocument();
  });

  it("shows failed crossfeed saves without clearing unsaved state", async () => {
    mockConnectedPumper(defaultConfig, (opcode) => opcode === Opcode.SaveCrossfeed ? new Error("Storage error") : null, [2, 2]);
    render(<App />);
    const mode = await screen.findByRole("button", { name: "Crossfeed mode: Off" });
    await waitFor(() => expect(mode).toBeEnabled());
    const list = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Crossfeed mode"]')!;
    fireEvent.click(within(list).getByRole("option", { name: "Medium", hidden: true }));
    fireEvent.click(screen.getByRole("button", { name: "Save crossfeed" }));
    expect(await screen.findByText("Save crossfeed: Storage error")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Save crossfeed" })).toBeEnabled();
    expect(screen.getByRole("button", { name: "Crossfeed mode: Medium" })).toBeInTheDocument();
  });

  it("previews and saves firmware 3 output processing independently", async () => {
    const { request, audioMock } = mockConnectedPumper(defaultConfig, undefined, [3, 0]);
    render(<App />);
    const balance = await screen.findByLabelText("Balance");
    await waitFor(() => expect(balance).not.toHaveAttribute("readonly"));
    const crossfeedCard = screen.getByRole("heading", { name: "Headphone crossfeed" }).closest("section")!;
    const outputCard = screen.getByRole("heading", { name: "Output processing" }).closest("section")!;
    expect(crossfeedCard.parentElement).toBe(outputCard.parentElement);
    const mono = screen.getByRole("checkbox", { name: "Mono" });
    const width = screen.getByLabelText("Stereo width");
    fireEvent.click(mono);
    expect(width).toHaveAttribute("readonly");
    expect(screen.getByLabelText("Stereo width slider")).toBeDisabled();
    expect(width).toHaveValue("100");
    fireEvent.click(mono);
    expect(width).not.toHaveAttribute("readonly");
    fireEvent.click(screen.getByRole("checkbox", { name: "Swap channels" }));
    fireEvent.click(screen.getByRole("checkbox", { name: "Invert left polarity" }));
    fireEvent.click(screen.getByRole("checkbox", { name: "Invert right polarity" }));
    fireEvent.change(screen.getByLabelText("Balance slider"), { target: { value: "-25" } });
    fireEvent.change(screen.getByLabelText("Stereo width slider"), { target: { value: "140" } });
    expect(screen.getByLabelText("Output processing save state")).toHaveTextContent("Unsaved");
    fireEvent.click(screen.getByRole("button", { name: "Save output" }));
    expect(await screen.findByText("Output processing saved for power-on")).toBeInTheDocument();
    expect(audioMock.outputState.saved).toMatchObject({ mono: false, swap: true, invertLeft: true, invertRight: true, balancePercent: -25, widthPercent: 140 });
    const calls = request.mock.calls.filter(([opcode]) => [Opcode.SetOutputProcessing, Opcode.SaveOutputProcessing].includes(opcode));
    expect(calls.at(-2)?.[0]).toBe(Opcode.SetOutputProcessing);
    expect(decodeOutputProcessing(calls.at(-2)?.[1] as Uint8Array)).toMatchObject({ swap: true, balancePercent: -25, widthPercent: 140 });
    expect(calls.at(-1)?.[0]).toBe(Opcode.SaveOutputProcessing);
  });

  it("keeps output processing unsaved after a storage failure", async () => {
    mockConnectedPumper(defaultConfig, (opcode) => opcode === Opcode.SaveOutputProcessing ? new Error("Storage error") : null, [3, 0]);
    render(<App />);
    const swap = await screen.findByRole("checkbox", { name: "Swap channels" });
    await waitFor(() => expect(swap).toBeEnabled());
    fireEvent.click(swap);
    fireEvent.click(screen.getByRole("button", { name: "Save output" }));
    expect(await screen.findByText("Save output processing: Storage error")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Save output" })).toBeEnabled();
    expect(screen.getByLabelText("Output processing save state")).toHaveTextContent("Unsaved");
  });

  it("offers firmware 3 filters with Q-only controls and disabled gain", async () => {
    const { request } = mockConnectedPumper(defaultConfig, undefined, [3, 0]);
    render(<App />);
    await screen.findByRole("button", { name: "Band 1 filter type: Peaking" });
    const filterMenu = document.querySelector<HTMLElement>('[role="listbox"][aria-label="Band 1 filter type"]')!;
    expect(within(filterMenu).getByRole("option", { name: "Low-pass", hidden: true })).toBeInTheDocument();
    expect(within(filterMenu).getByRole("option", { name: "High-pass", hidden: true })).toBeInTheDocument();
    expect(within(filterMenu).getByRole("option", { name: "Notch", hidden: true })).toBeInTheDocument();
    expect(within(filterMenu).getByRole("option", { name: "Band-pass", hidden: true })).toBeInTheDocument();
    request.mockClear();
    fireEvent.click(within(filterMenu).getByRole("option", { name: "Low-pass", hidden: true }));
    expect(screen.getByLabelText("Gain slider for band 1")).toBeDisabled();
    expect(screen.getByLabelText("Gain for band 1")).toHaveAttribute("readonly");
    expect(screen.getByLabelText("Q for band 1")).toHaveAttribute("aria-valuemax", "20");
    await waitFor(() => expect(request.mock.calls.some(([opcode, payload]) => opcode === Opcode.SetBand && decodeBand(payload as Uint8Array).band.type === FilterType.LowPass)).toBe(true));
    const sent = request.mock.calls.find(([opcode, payload]) => opcode === Opcode.SetBand && decodeBand(payload as Uint8Array).band.type === FilterType.LowPass);
    expect(decodeBand(sent?.[1] as Uint8Array).band.widthMode).toBe(WidthMode.Q);
  });

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

  it("coalesces continuous band edits without losing the latest value", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    request.mockClear();
    vi.useFakeTimers();

    const gainSlider = screen.getByLabelText("Gain slider for band 1");
    fireEvent.change(gainSlider, { target: { value: "1" } });
    fireEvent.change(gainSlider, { target: { value: "2" } });
    fireEvent.change(gainSlider, { target: { value: "3" } });
    await act(() => vi.advanceTimersByTimeAsync(54));
    expect(request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand)).toHaveLength(0);

    await act(() => vi.advanceTimersByTimeAsync(1));
    let bandCalls = request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand);
    expect(bandCalls).toHaveLength(1);
    expect(decodeBand(bandCalls[0][1] as Uint8Array).band.gainDb).toBe(3);

    fireEvent.change(gainSlider, { target: { value: "4" } });
    await act(() => vi.advanceTimersByTimeAsync(30));
    fireEvent.change(gainSlider, { target: { value: "5" } });
    await act(() => vi.advanceTimersByTimeAsync(25));
    bandCalls = request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand);
    expect(bandCalls).toHaveLength(2);
    expect(decodeBand(bandCalls[1][1] as Uint8Array).band.gainDb).toBe(5);

    fireEvent.change(gainSlider, { target: { value: "6" } });
    await act(() => vi.advanceTimersByTimeAsync(55));
    bandCalls = request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand);
    expect(bandCalls).toHaveLength(3);
    expect(decodeBand(bandCalls[2][1] as Uint8Array).band.gainDb).toBe(6);
  });

  it("orders auto-preamp updates to preserve headroom", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    vi.useFakeTimers();

    fireEvent.click(screen.getByRole("button", { name: "Auto" }));
    await act(() => vi.advanceTimersByTimeAsync(55));
    request.mockClear();

    const gainSlider = screen.getByLabelText("Gain slider for band 1");
    fireEvent.change(gainSlider, { target: { value: "6" } });
    await act(() => vi.advanceTimersByTimeAsync(55));
    let previewCalls = request.mock.calls.filter(([opcode]) => opcode === Opcode.SetGlobal || opcode === Opcode.SetBand);
    expect(previewCalls.map(([opcode]) => opcode)).toEqual([Opcode.SetGlobal, Opcode.SetBand]);
    expect(decodeGlobal(previewCalls[0][1] as Uint8Array).preampDb).toBeLessThan(0);
    expect(decodeBand(previewCalls[1][1] as Uint8Array).band.gainDb).toBe(6);

    request.mockClear();
    fireEvent.change(gainSlider, { target: { value: "0" } });
    await act(() => vi.advanceTimersByTimeAsync(55));
    previewCalls = request.mock.calls.filter(([opcode]) => opcode === Opcode.SetGlobal || opcode === Opcode.SetBand);
    expect(previewCalls.map(([opcode]) => opcode)).toEqual([Opcode.SetBand, Opcode.SetGlobal]);
    expect(decodeBand(previewCalls[0][1] as Uint8Array).band.gainDb).toBe(0);
    expect(decodeGlobal(previewCalls[1][1] as Uint8Array).preampDb).toBe(0);
  });

  it("discards a pending preview before restoring defaults", async () => {
    const { request } = mockConnectedPumper();
    render(<App />);
    const profileMenu = await screen.findByRole("button", { name: "Profile: Profile 1 (default)" });
    await waitFor(() => expect(profileMenu).toBeEnabled());
    request.mockClear();
    vi.useFakeTimers();

    fireEvent.change(screen.getByLabelText("Gain slider for band 1"), { target: { value: "6" } });
    fireEvent.click(screen.getByRole("button", { name: "Defaults" }));
    fireEvent.click(within(screen.getByRole("dialog")).getByRole("button", { name: "Restore defaults" }));
    await act(() => vi.advanceTimersByTimeAsync(55));

    expect(request.mock.calls.filter(([opcode]) => opcode === Opcode.SetBand)).toHaveLength(0);
    expect(request).toHaveBeenCalledWith(Opcode.RestoreDefaults);
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
