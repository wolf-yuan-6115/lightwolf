export const USB_VENDOR_ID = 0x2e8a;
export const USB_PRODUCT_ID = 0xf10a;
export const HID_USAGE_PAGE = 0xff00;
export const HID_USAGE = 0x01;

export const REPORT_SIZE = 64;
export const HEADER_SIZE = 8;
export const PROTOCOL_VERSION = 1;
export const METER_REPORT_INTERVAL_MS = 40;
export const METER_HEARTBEAT_INTERVAL_MS = 500;
export const METER_TIMEOUT_MS = 1250;

export enum Opcode {
  Hello = 0x01,
  GetStatus = 0x02,
  GetGlobal = 0x03,
  GetBand = 0x04,
  GetProfiles = 0x05,
  SetGlobal = 0x10,
  SetBand = 0x11,
  WriteFlash = 0x20,
  RestoreDefaults = 0x21,
  LoadProfile = 0x22,
  SaveProfile = 0x23,
  SetDefaultProfile = 0x24,
  DeleteProfile = 0x25,
  MeterStart = 0x30,
  MeterKeepalive = 0x31,
  MeterStop = 0x32,
  MeterLevel = 0x33,
}

export enum ProtocolStatus {
  Ok = 0,
  InvalidPacket = 1,
  InvalidCommand = 2,
  InvalidLength = 3,
  InvalidIndex = 4,
  OutOfRange = 5,
  Busy = 6,
  StorageError = 7,
}

export enum FilterType {
  LowShelf = 0,
  Peaking = 1,
  HighShelf = 2,
}

export enum WidthMode {
  Q = 0,
  Bandwidth = 1,
}

export interface EqBand {
  enabled: boolean;
  type: FilterType;
  widthMode: WidthMode;
  frequencyHz: number;
  gainDb: number;
  q: number;
  bandwidthOctaves: number;
}

export interface EqConfig {
  enabled: boolean;
  preampDb: number;
  bands: EqBand[];
}

export interface DeviceStatus {
  firmwareVersion: string;
  bandCount: number;
  streaming: boolean;
  dirty: boolean;
  eqEnabled: boolean;
  sampleRateHz: number;
  configGeneration: number;
  savedGeneration: number;
  appliedGeneration: number;
  underrunFrames: number;
  droppedBlocks: number;
}

export interface ResponsePacket {
  opcode: number;
  requestId: number;
  status: ProtocolStatus;
  payload: Uint8Array;
}

export interface MeterLevel {
  sequence: number;
  leftPeak: number;
  rightPeak: number;
  leftMeanSquare: number;
  rightMeanSquare: number;
}

export interface ProfileState {
  count: number;
  activeProfile: number;
  persistedProfile: number;
  presentMask: number;
  bankGeneration: number;
}

const statusLabels: Record<ProtocolStatus, string> = {
  [ProtocolStatus.Ok]: "OK",
  [ProtocolStatus.InvalidPacket]: "The device rejected the packet",
  [ProtocolStatus.InvalidCommand]: "The command is not supported",
  [ProtocolStatus.InvalidLength]: "The command length is invalid",
  [ProtocolStatus.InvalidIndex]: "The EQ band index is invalid",
  [ProtocolStatus.OutOfRange]: "One or more EQ values are out of range",
  [ProtocolStatus.Busy]: "The device is busy",
  [ProtocolStatus.StorageError]: "The device could not write its flash storage",
};

function viewFor(data: Uint8Array): DataView {
  return new DataView(data.buffer, data.byteOffset, data.byteLength);
}

function milli(value: number): number {
  return Math.round(value * 1000);
}

export function createRequest(
  opcode: Opcode,
  requestId: number,
  payload: Uint8Array<ArrayBufferLike> = new Uint8Array(),
): Uint8Array {
  if (payload.length > REPORT_SIZE - HEADER_SIZE) throw new RangeError("Payload is too large");
  const report = new Uint8Array(REPORT_SIZE);
  const view = viewFor(report);
  report[0] = "P".charCodeAt(0);
  report[1] = "E".charCodeAt(0);
  report[2] = PROTOCOL_VERSION;
  report[3] = opcode;
  view.setUint16(4, requestId, true);
  report[6] = payload.length;
  report.set(payload, HEADER_SIZE);
  return report;
}

export function parseResponse(report: Uint8Array): ResponsePacket {
  if (
    report.length !== REPORT_SIZE ||
    report[0] !== "P".charCodeAt(0) ||
    report[1] !== "E".charCodeAt(0) ||
    report[2] !== PROTOCOL_VERSION ||
    report[6] > REPORT_SIZE - HEADER_SIZE
  ) {
    throw new Error("Malformed response from Pumper");
  }
  const view = viewFor(report);
  return {
    opcode: report[3],
    requestId: view.getUint16(4, true),
    status: report[7] as ProtocolStatus,
    payload: report.slice(HEADER_SIZE, HEADER_SIZE + report[6]),
  };
}

export function assertResponse(response: ResponsePacket, opcode: Opcode): void {
  if (response.opcode !== (opcode | 0x80)) throw new Error("Unexpected response from Pumper");
  if (response.status !== ProtocolStatus.Ok) throw new Error(statusLabels[response.status] ?? "Device error");
}

export function encodeGlobal(config: EqConfig): Uint8Array {
  const payload = new Uint8Array(8);
  payload[0] = config.enabled ? 1 : 0;
  viewFor(payload).setInt32(4, milli(config.preampDb), true);
  return payload;
}

export function decodeGlobal(payload: Uint8Array): Pick<EqConfig, "enabled" | "preampDb"> {
  if (payload.length !== 8) throw new Error("Invalid global EQ response");
  return { enabled: payload[0] !== 0, preampDb: viewFor(payload).getInt32(4, true) / 1000 };
}

export function encodeBand(index: number, band: EqBand): Uint8Array {
  const payload = new Uint8Array(21);
  const view = viewFor(payload);
  payload[0] = index;
  payload[1] = band.enabled ? 1 : 0;
  payload[2] = band.type;
  payload[3] = band.widthMode;
  view.setInt32(5, milli(band.frequencyHz), true);
  view.setInt32(9, milli(band.gainDb), true);
  view.setInt32(13, milli(band.q), true);
  view.setInt32(17, milli(band.bandwidthOctaves), true);
  return payload;
}

export function decodeBand(payload: Uint8Array): { index: number; band: EqBand } {
  if (payload.length !== 21) throw new Error("Invalid EQ band response");
  const view = viewFor(payload);
  return {
    index: payload[0],
    band: {
      enabled: payload[1] !== 0,
      type: payload[2] as FilterType,
      widthMode: payload[3] as WidthMode,
      frequencyHz: view.getInt32(5, true) / 1000,
      gainDb: view.getInt32(9, true) / 1000,
      q: view.getInt32(13, true) / 1000,
      bandwidthOctaves: view.getInt32(17, true) / 1000,
    },
  };
}

export function decodeStatus(payload: Uint8Array): DeviceStatus {
  if (payload.length !== 28) throw new Error("Invalid status response");
  const view = viewFor(payload);
  const flags = payload[3];
  return {
    firmwareVersion: `${payload[0]}.${payload[1]}`,
    bandCount: payload[2],
    streaming: (flags & 0x01) !== 0,
    dirty: (flags & 0x02) !== 0,
    eqEnabled: (flags & 0x04) !== 0,
    sampleRateHz: view.getUint32(4, true),
    configGeneration: view.getUint32(8, true),
    savedGeneration: view.getUint32(12, true),
    appliedGeneration: view.getUint32(16, true),
    underrunFrames: view.getUint32(20, true),
    droppedBlocks: view.getUint32(24, true),
  };
}

export function encodeMeterConfig(reportIntervalMs: number, timeoutMs: number): Uint8Array {
  const payload = new Uint8Array(4);
  const view = viewFor(payload);
  view.setUint16(0, reportIntervalMs, true);
  view.setUint16(2, timeoutMs, true);
  return payload;
}

export function decodeMeterLevel(payload: Uint8Array): MeterLevel {
  if (payload.length !== 16) throw new Error("Invalid audio meter report");
  const view = viewFor(payload);
  return {
    sequence: view.getUint32(0, true),
    leftPeak: view.getUint16(4, true),
    rightPeak: view.getUint16(6, true),
    leftMeanSquare: view.getUint32(8, true),
    rightMeanSquare: view.getUint32(12, true),
  };
}

export function decodeProfileState(payload: Uint8Array): ProfileState {
  if (payload.length !== 12) throw new Error("Invalid profile state response");
  const view = viewFor(payload);
  return {
    count: payload[0],
    activeProfile: payload[1],
    persistedProfile: payload[2],
    presentMask: view.getUint16(4, true),
    bankGeneration: view.getUint32(8, true),
  };
}

export const defaultConfig: EqConfig = {
  enabled: true,
  preampDb: 0,
  bands: [
    [FilterType.Peaking, 68, 0.71, 1.89, 0],
    [FilterType.LowShelf, 105, 0.71, 1.89, 0],
    [FilterType.Peaking, 260, 4, 0.36, 0],
    [FilterType.Peaking, 1300, 3, 0.48, 0],
    [FilterType.Peaking, 1650, 3, 0.48, 0],
    [FilterType.Peaking, 2600, 5, 0.29, 0],
    [FilterType.HighShelf, 3000, 0.35, 3.33, 0],
    [FilterType.Peaking, 3000, 1.4, 1.01, 0],
    [FilterType.Peaking, 5100, 4.5, 0.32, 0],
    [FilterType.HighShelf, 10000, 0.71, 1.89, 0],
  ].map(([type, frequencyHz, q, bandwidthOctaves, gainDb]) => ({
    enabled: true,
    type: type as FilterType,
    widthMode: type === FilterType.Peaking ? WidthMode.Bandwidth : WidthMode.Q,
    frequencyHz,
    q,
    bandwidthOctaves,
    gainDb,
  })),
};
