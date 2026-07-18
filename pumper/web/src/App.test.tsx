import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
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
const originalUserAgent = Object.getOwnPropertyDescriptor(navigator, "userAgent");

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  if (originalHid) Object.defineProperty(navigator, "hid", originalHid);
  else Reflect.deleteProperty(navigator, "hid");
  if (originalUserAgent) Object.defineProperty(navigator, "userAgent", originalUserAgent);
});

describe("Pumper controller", () => {
  it("shows a useful browser compatibility state without WebHID", () => {
    render(<App />);
    expect(screen.getByText(/WebHID is unavailable/i)).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Frequency response" })).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "Preamp mode" })).toBeInTheDocument();
    expect(screen.getByLabelText("Realtime stereo output level")).toBeInTheDocument();
    expect(screen.getByLabelText("Profile")).toHaveValue("0");
    expect(screen.getByRole("button", { name: "Make default" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Delete Profile 1" })).toBeDisabled();
  });

  it("shows the exact udev rule to Linux users", () => {
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: { getDevices: vi.fn().mockResolvedValue([]) },
    });
    Object.defineProperty(navigator, "userAgent", {
      configurable: true,
      value: "Mozilla/5.0 (X11; Linux x86_64) Chrome/140",
    });

    render(<App />);

    expect(screen.getByRole("heading", { name: "WebHID permission" })).toBeInTheDocument();
    expect(screen.getAllByText(/ATTRS\{idVendor\}=="2e8a"/)).toHaveLength(2);
    expect(screen.getByText(/\/etc\/udev\/rules\.d\/70-pumper-webhid\.rules/)).toBeInTheDocument();
    expect(screen.getByText("Cleared after reboot")).toBeInTheDocument();
    expect(screen.getByText(/\/run\/udev\/rules\.d\/70-pumper-webhid\.rules/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Copy permanent access command" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Copy temporary access command" })).toBeInTheDocument();
  });

  it("switches away from an empty profile without claiming the EQ was edited", async () => {
    const device = { vendorId: 0x2e8a, productId: 0xf10a } as HIDDevice;
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: { getDevices: vi.fn().mockResolvedValue([device]) },
    });
    vi.spyOn(PumperHidTransport.prototype, "open").mockResolvedValue();
    vi.spyOn(PumperHidTransport.prototype, "close").mockResolvedValue();

    let activeProfile = 0;
    vi.spyOn(PumperHidTransport.prototype, "request").mockImplementation(async (opcode, payload) => {
      const requestPayload = payload ?? new Uint8Array();
      let responsePayload: Uint8Array<ArrayBufferLike> = new Uint8Array();
      if (opcode === Opcode.Hello || opcode === Opcode.GetStatus) {
        responsePayload = new Uint8Array(28);
        const view = new DataView(responsePayload.buffer);
        responsePayload.set([1, 5, 10, 0x04]);
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

    render(<App />);
    const select = await screen.findByLabelText("Profile");
    await waitFor(() => expect(select).toBeEnabled());

    fireEvent.change(select, { target: { value: "2" } });
    expect(select).toHaveValue("2");
    fireEvent.change(select, { target: { value: "1" } });

    await waitFor(() => expect(select).toHaveValue("1"));
    expect(screen.queryByRole("heading", { name: "Discard unsaved changes?" })).not.toBeInTheDocument();
  });
});
