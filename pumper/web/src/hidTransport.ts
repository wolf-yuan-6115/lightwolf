import {
  assertResponse,
  createRequest,
  Opcode,
  parseResponse,
  ResponsePacket,
  USB_PRODUCT_ID,
  USB_VENDOR_ID,
} from "./protocol";

interface PendingRequest {
  opcode: Opcode;
  resolve: (response: ResponsePacket) => void;
  reject: (error: Error) => void;
  timeout: ReturnType<typeof setTimeout>;
}

export const METER_REPORT_EVENT = "meterreport";

export class PumperHidTransport extends EventTarget {
  private device: HIDDevice | null = null;
  private requestId = 0;
  private pending: PendingRequest | null = null;
  private requestChain: Promise<unknown> = Promise.resolve();

  static supported(): boolean {
    return typeof navigator !== "undefined" && "hid" in navigator && navigator.hid !== undefined;
  }

  static async grantedDevice(): Promise<HIDDevice | null> {
    if (!this.supported()) return null;
    const devices = await navigator.hid.getDevices();
    return devices.find((device) => this.matches(device)) ?? null;
  }

  static async requestDevice(): Promise<HIDDevice | null> {
    if (!this.supported()) return null;
    const devices = await navigator.hid.requestDevice({
      filters: [{ vendorId: USB_VENDOR_ID, productId: USB_PRODUCT_ID }],
    });
    return devices.find((device) => this.matches(device)) ?? null;
  }

  private static matches(device: HIDDevice): boolean {
    return device.vendorId === USB_VENDOR_ID && device.productId === USB_PRODUCT_ID;
  }

  async open(device: HIDDevice): Promise<void> {
    this.device = device;
    try {
      if (!device.opened) await device.open();
    } catch (reason) {
      this.device = null;
      if (navigator.userAgent.includes("Linux")) {
        throw new Error("Linux denied access to Pumper.", {
          cause: reason,
        });
      }
      throw reason;
    }
    device.addEventListener("inputreport", this.handleInputReport as EventListener);
    navigator.hid.addEventListener("disconnect", this.handleDisconnect as EventListener);
  }

  async close(): Promise<void> {
    const device = this.device;
    this.device = null;
    if (device) {
      device.removeEventListener("inputreport", this.handleInputReport as EventListener);
      if (device.opened) await device.close();
    }
    navigator.hid?.removeEventListener?.("disconnect", this.handleDisconnect as EventListener);
    this.rejectPending(new Error("Pumper disconnected"));
  }

  get productName(): string {
    return this.device?.productName ?? "Pumper USB DAC";
  }

  request(
    opcode: Opcode,
    payload: Uint8Array<ArrayBufferLike> = new Uint8Array(),
    timeoutMs = 1500,
  ): Promise<ResponsePacket> {
    const result = this.requestChain.then(() => this.performRequest(opcode, payload, timeoutMs));
    this.requestChain = result.catch(() => undefined);
    return result;
  }

  private performRequest(opcode: Opcode, payload: Uint8Array<ArrayBufferLike>, timeoutMs: number): Promise<ResponsePacket> {
    if (!this.device?.opened) return Promise.reject(new Error("Pumper is not connected"));
    const requestId = (this.requestId = (this.requestId + 1) & 0xffff);
    return new Promise<ResponsePacket>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending = null;
        reject(new Error("Pumper did not respond"));
      }, timeoutMs);
      this.pending = { opcode, resolve, reject, timeout };
      const encoded = createRequest(opcode, requestId, payload);
      const report = new Uint8Array(encoded.length);
      report.set(encoded);
      this.device?.sendReport(0, report).catch((error: unknown) => {
        this.rejectPending(error instanceof Error ? error : new Error("Unable to send HID report"));
      });
    });
  }

  private handleInputReport = (event: HIDInputReportEvent): void => {
    if (event.device !== this.device) return;
    try {
      const bytes = new Uint8Array(event.data.buffer, event.data.byteOffset, event.data.byteLength);
      const response = parseResponse(bytes);
      if (response.requestId === 0) {
        if (response.opcode === (Opcode.MeterLevel | 0x80) && response.status === 0) {
          this.dispatchEvent(new CustomEvent<ResponsePacket>(METER_REPORT_EVENT, { detail: response }));
        }
        return;
      }
      if (!this.pending || response.requestId !== this.requestId) return;
      assertResponse(response, this.pending.opcode);
      const pending = this.pending;
      this.pending = null;
      clearTimeout(pending.timeout);
      pending.resolve(response);
    } catch (error) {
      if (this.pending) this.rejectPending(error instanceof Error ? error : new Error("Malformed HID response"));
    }
  };

  private handleDisconnect = (event: HIDConnectionEvent): void => {
    if (event.device !== this.device) return;
    this.device = null;
    this.rejectPending(new Error("Pumper disconnected"));
    this.dispatchEvent(new Event("disconnect"));
  };

  private rejectPending(error: Error): void {
    if (!this.pending) return;
    const pending = this.pending;
    this.pending = null;
    clearTimeout(pending.timeout);
    pending.reject(error);
  }
}
