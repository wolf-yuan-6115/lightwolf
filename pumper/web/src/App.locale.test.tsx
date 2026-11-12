import { cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// Set browser preferences before the app (including module-level options) loads.
vi.hoisted(() => {
  Object.defineProperty(navigator, "languages", { configurable: true, value: ["zh-TW", "en-US"] });
  Object.defineProperty(navigator, "language", { configurable: true, value: "zh-TW" });
});

import App from "./App";
import { PumperHidTransport } from "./hidTransport";
import { defaultConfig, encodeBand, encodeGlobal, Opcode, ProtocolStatus, assertResponse, type ResponsePacket } from "./protocol";
import { parseEqText, serializeEqText } from "./eqText";
import { audioSettingsMock } from "./test/audioSettingsMock";

beforeEach(() => {
  const audio = audioSettingsMock();
  vi.spyOn(PumperHidTransport, "supported").mockReturnValue(true);
  vi.spyOn(PumperHidTransport, "grantedDevice").mockResolvedValue({} as HIDDevice);
  vi.spyOn(PumperHidTransport.prototype, "open").mockResolvedValue();
  vi.spyOn(PumperHidTransport.prototype, "close").mockResolvedValue();
  vi.spyOn(PumperHidTransport.prototype, "request").mockImplementation(async (opcode, payload) => {
    let responsePayload: Uint8Array<ArrayBufferLike> = new Uint8Array();
    if ([Opcode.GetAudioControls, Opcode.GetCrossfeed, Opcode.SetCrossfeed, Opcode.SaveCrossfeed,
      Opcode.GetOutputProcessing, Opcode.SetOutputProcessing, Opcode.SaveOutputProcessing].includes(opcode)) {
      return audio.handle(opcode, new Uint8Array(payload ?? []));
    }
    if (opcode === Opcode.Hello || opcode === Opcode.GetStatus) {
      responsePayload = new Uint8Array(48);
      responsePayload.set([4, 2, 10, 0x04]);
      new DataView(responsePayload.buffer).setUint32(4, 48000, true);
      responsePayload[44] = 24;
    } else if (opcode === Opcode.GetTelemetry) {
      responsePayload = new Uint8Array(56);
      responsePayload[0] = 1;
    } else if (opcode === Opcode.GetGlobal) {
      responsePayload = encodeGlobal(defaultConfig);
    } else if (opcode === Opcode.GetBand) {
      const index = payload![0];
      responsePayload = encodeBand(index, defaultConfig.bands[index]);
    } else if (opcode === Opcode.GetProfiles) {
      responsePayload = new Uint8Array(12);
      responsePayload.set([10, 0, 0, 0, 1]);
    }
    return { opcode: opcode | 0x80, requestId: 1, status: ProtocolStatus.Ok, payload: responsePayload };
  });
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

async function renderConnected() {
  render(<App />);
  await waitFor(() => expect(screen.getByRole("button", { name: "匯入 EQ" })).toBeEnabled());
}

describe("Traditional Chinese controller", () => {
  it("localizes connected controls, dropdowns, accessible names, and document language", async () => {
    await renderConnected();
    expect(document.documentElement.lang).toBe("zh-TW");
    expect(screen.getByRole("heading", { name: "頻率響應" })).toBeVisible();
    expect(screen.getByRole("button", { name: "設定檔: 設定檔 1（預設）" })).toHaveTextContent("設定檔 1（預設）");
    expect(screen.getByRole("status", { name: "EQ 設定檔儲存狀態" })).toHaveTextContent("已儲存");
    expect(screen.getByRole("spinbutton", { name: "頻段 1 的頻率" })).toBeEnabled();
    expect(await screen.findByRole("checkbox", { name: "跟隨 USB 主機音量" })).toBeVisible();
    // jsdom does not implement the native Popover API.
    const menu = document.querySelector('ul[aria-label="頻段 1 濾波器類型"]')!;
    expect(within(menu as HTMLElement).getByText("Low-pass")).toBeInTheDocument();
    expect(within(menu as HTMLElement).getByRole("option", { name: "Low-pass", hidden: true })).toHaveAccessibleDescription("保留低頻，降低高頻音量。");
    expect(screen.getByRole("spinbutton", { name: "頻段 2 的轉折陡度" })).toBeEnabled();
    expect(screen.queryByRole("button", { name: /language|語言/i })).not.toBeInTheDocument();
  });

  it("localizes import errors while keeping the shared EQ text format", async () => {
    await renderConnected();
    fireEvent.click(screen.getByRole("button", { name: "匯入 EQ" }));
    const dialog = screen.getByRole("dialog", { name: "匯入參數式 EQ" });
    const input = within(dialog).getByRole("textbox", { name: "參數式 EQ 文字" });
    fireEvent.change(input, { target: { value: "not an EQ setting" } });
    fireEvent.click(within(dialog).getByRole("button", { name: "匯入並試聽" }));
    expect(within(dialog).getByRole("alert")).toHaveTextContent("第 1 行的 EQ 設定格式不受支援。");
    const text = serializeEqText(defaultConfig);
    expect(text).toContain("Equalizer: ON\nPreamp: 0 dB\nFilter 1: ON PK Fc");
    expect(serializeEqText(parseEqText(text))).toBe(text);
    fireEvent.change(input, { target: { value: text } });
    fireEvent.click(within(dialog).getByRole("button", { name: "匯入並試聽" }));
    expect(screen.getByText("已套用匯入的 EQ，可立即試聽")).toBeVisible();
  });

  it("localizes diagnostics and destructive confirmation without sending a reset", async () => {
    await renderConnected();
    fireEvent.click(screen.getByRole("button", { name: "DAC 資訊" }));
    expect(screen.getByRole("heading", { name: "訊號路徑" })).toBeVisible();
    expect(screen.getByLabelText("韌體：DAC 目前執行的韌體版本。")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "恢復原廠設定" }));
    const dialog = screen.getByRole("dialog", { name: "要將 Pumper 恢復原廠設定嗎？" });
    expect(within(dialog).getByText(/此操作無法復原/)).toBeVisible();
    fireEvent.click(within(dialog).getByRole("button", { name: "取消" }));
    expect(PumperHidTransport.prototype.request).not.toHaveBeenCalledWith(Opcode.FactoryReset);
  });

  it("localizes known device errors", () => {
    const response: ResponsePacket = { opcode: Opcode.SaveProfile | 0x80, requestId: 1,
      status: ProtocolStatus.StorageError, payload: new Uint8Array() };
    expect(() => assertResponse(response, Opcode.SaveProfile)).toThrow("裝置無法儲存設定");
  });
});
