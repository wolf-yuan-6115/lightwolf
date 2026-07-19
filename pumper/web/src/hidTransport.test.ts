import { afterEach, describe, expect, it, vi } from "vitest";
import { METER_REPORT_EVENT, PumperHidTransport } from "./hidTransport";
import { createRequest, Opcode, USB_PRODUCT_ID, USB_VENDOR_ID } from "./protocol";

const originalHid = Object.getOwnPropertyDescriptor(navigator, "hid");

afterEach(() => {
  vi.restoreAllMocks();
  if (originalHid) {
    Object.defineProperty(navigator, "hid", originalHid);
  } else {
    Reflect.deleteProperty(navigator, "hid");
  }
});

describe("Pumper HID transport", () => {
  it("discovers the DAC by its exact USB identity without collection filtering", async () => {
    const device = {
      vendorId: USB_VENDOR_ID,
      productId: USB_PRODUCT_ID,
      collections: [],
    } as unknown as HIDDevice;
    const requestDevice = vi.fn().mockResolvedValue([device]);
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: { requestDevice },
    });

    await expect(PumperHidTransport.requestDevice()).resolves.toBe(device);
    expect(requestDevice).toHaveBeenCalledWith({
      filters: [{ vendorId: USB_VENDOR_ID, productId: USB_PRODUCT_ID }],
    });
  });

  it("emits unsolicited meter reports without a pending request", async () => {
    const listeners: { input?: EventListener } = {};
    const device = {
      vendorId: USB_VENDOR_ID,
      productId: USB_PRODUCT_ID,
      opened: true,
      addEventListener: vi.fn((type: string, listener: EventListener) => {
        if (type === "inputreport") listeners.input = listener;
      }),
      removeEventListener: vi.fn(),
      close: vi.fn(),
    } as unknown as HIDDevice;
    Object.defineProperty(navigator, "hid", {
      configurable: true,
      value: { addEventListener: vi.fn(), removeEventListener: vi.fn() },
    });

    const transport = new PumperHidTransport();
    const meterListener = vi.fn();
    transport.addEventListener(METER_REPORT_EVENT, meterListener);
    await transport.open(device);

    const report = createRequest(Opcode.MeterLevel, 0, new Uint8Array(28));
    report[3] |= 0x80;
    const event = { device, data: new DataView(report.buffer) } as HIDInputReportEvent;
    const listener = listeners.input;
    if (!listener) throw new Error("Input report listener was not registered");
    listener(event as unknown as Event);

    expect(meterListener).toHaveBeenCalledOnce();
    expect((meterListener.mock.calls[0][0] as CustomEvent).detail).toMatchObject({
      opcode: Opcode.MeterLevel | 0x80,
      requestId: 0,
    });
  });
});
