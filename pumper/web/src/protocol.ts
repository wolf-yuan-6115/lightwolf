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
  GetAudioControls = 0x06,
  GetCrossfeed = 0x07,
  GetOutputProcessing = 0x08,
  SetGlobal = 0x10,
  SetBand = 0x11,
  SetCrossfeed = 0x12,
  SetOutputProcessing = 0x13,
  WriteFlash = 0x20,
  RestoreDefaults = 0x21,
  LoadProfile = 0x22,
  SaveProfile = 0x23,
  SetDefaultProfile = 0x24,
  DeleteProfile = 0x25,
  SaveCrossfeed = 0x26,
  SaveOutputProcessing = 0x27,
  MeterStart = 0x30,
  MeterKeepalive = 0x31,
  MeterStop = 0x32,
  MeterLevel = 0x33,
  RestartDevice = 0x40,
  EnterBootsel = 0x41,
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
  LowPass = 3,
  HighPass = 4,
  Notch = 5,
  BandPass = 6,
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

export interface AudioChannelControl {
  volumeDb: number;
  muted: boolean;
}

export interface AudioControls {
  master: AudioChannelControl;
  left: AudioChannelControl;
  right: AudioChannelControl;
}

export enum CrossfeedMode {
  Off = 0,
  Low = 1,
  Medium = 2,
  High = 3,
  Custom = 4,
}

// Parameter fields always retain Custom values, including in preset modes.
export interface CrossfeedConfig {
  mode: CrossfeedMode;
  strengthPercent: number;
  cutoffHz: number;
  delayMs: number;
}

export interface CrossfeedState {
  live: CrossfeedConfig;
  saved: CrossfeedConfig;
  dirty: boolean;
}

export const defaultCrossfeed: CrossfeedConfig = {
  mode: CrossfeedMode.Off,
  strengthPercent: 20,
  cutoffHz: 700,
  delayMs: 0.25,
};

export enum OutputProcessingFlag {
  Mono = 0x01,
  Swap = 0x02,
  InvertLeft = 0x04,
  InvertRight = 0x08,
}

export interface OutputProcessingConfig {
  mono: boolean;
  swap: boolean;
  invertLeft: boolean;
  invertRight: boolean;
  balancePercent: number;
  widthPercent: number;
}

export interface OutputProcessingState {
  live: OutputProcessingConfig;
  saved: OutputProcessingConfig;
  dirty: boolean;
}

export const defaultOutputProcessing: OutputProcessingConfig = {
  mono: false,
  swap: false,
  invertLeft: false,
  invertRight: false,
  balancePercent: 0,
  widthPercent: 100,
};

function firmwareAtLeast(firmwareVersion: string, requiredMajor: number, requiredMinor: number): boolean {
  const match = /^(\d+)\.(\d+)$/.exec(firmwareVersion);
  if (!match) return false;
  const major = Number(match[1]);
  return major > requiredMajor || (major === requiredMajor && Number(match[2]) >= requiredMinor);
}

export function supportsAudioControls(firmwareVersion: string): boolean {
  return firmwareAtLeast(firmwareVersion, 2, 2);
}

export function supportsFirmware3Controls(firmwareVersion: string): boolean {
  return firmwareAtLeast(firmwareVersion, 3, 0);
}

export function effectiveAudioControl(master: AudioChannelControl, channel: AudioChannelControl): AudioChannelControl {
  return { volumeDb: master.volumeDb + channel.volumeDb, muted: master.muted || channel.muted };
}

export function decodeAudioControls(payload: Uint8Array): AudioControls {
  if (payload.length !== 9) throw new Error("Invalid USB audio control response");
  const view = viewFor(payload);
  const channels = [0, 1, 2].map((index): AudioChannelControl => {
    const volumeDb = view.getInt16(index * 2, true) / 256;
    if (volumeDb < -50 || volumeDb > 0 || payload[6 + index] > 1) {
      throw new Error("Invalid USB audio control response");
    }
    return { volumeDb, muted: payload[6 + index] === 1 };
  });
  return { master: channels[0], left: channels[1], right: channels[2] };
}

export function validateCrossfeed(config: CrossfeedConfig): void {
  if (!Number.isInteger(config.mode) || config.mode < CrossfeedMode.Off || config.mode > CrossfeedMode.Custom) {
    throw new RangeError("Crossfeed mode is not supported.");
  }
  for (const [label, value, minimum, maximum, unit] of [
    ["Crossfeed strength", config.strengthPercent, 0, 40, "%"],
    ["Crossfeed cutoff", config.cutoffHz, 300, 2000, "Hz"],
    ["Crossfeed delay", config.delayMs, 0, 0.6, "ms"],
  ] as const) {
    if (!Number.isFinite(value) || value < minimum || value > maximum) {
      throw new RangeError(`${label} must be between ${minimum.toLocaleString("en-US")} and ${maximum.toLocaleString("en-US")} ${unit}.`);
    }
  }
}

export function encodeCrossfeed(config: CrossfeedConfig): Uint8Array {
  validateCrossfeed(config);
  const payload = new Uint8Array(8);
  const view = viewFor(payload);
  payload[0] = config.mode;
  view.setUint16(2, Math.round(config.strengthPercent * 100), true);
  view.setUint16(4, Math.round(config.cutoffHz), true);
  view.setUint16(6, Math.round(config.delayMs * 1000), true);
  return payload;
}

export function decodeCrossfeed(payload: Uint8Array): CrossfeedConfig {
  if (payload.length !== 8 || payload[1] !== 0) throw new Error("Invalid crossfeed response");
  const view = viewFor(payload);
  const config = {
    mode: payload[0] as CrossfeedMode,
    strengthPercent: view.getUint16(2, true) / 100,
    cutoffHz: view.getUint16(4, true),
    delayMs: view.getUint16(6, true) / 1000,
  };
  validateCrossfeed(config);
  return config;
}

export function decodeCrossfeedState(payload: Uint8Array): CrossfeedState {
  if (payload.length !== 17 || payload[16] > 1) throw new Error("Invalid crossfeed state response");
  return {
    live: decodeCrossfeed(payload.subarray(0, 8)),
    saved: decodeCrossfeed(payload.subarray(8, 16)),
    dirty: payload[16] === 1,
  };
}

export function crossfeedConfigsEqual(left: CrossfeedConfig, right: CrossfeedConfig): boolean {
  return left.mode === right.mode && left.strengthPercent === right.strengthPercent &&
    left.cutoffHz === right.cutoffHz && left.delayMs === right.delayMs;
}

export function validateOutputProcessing(config: OutputProcessingConfig): void {
  if ([config.mono, config.swap, config.invertLeft, config.invertRight].some((value) => typeof value !== "boolean")) {
    throw new RangeError("Output processing flags are invalid.");
  }
  if (!Number.isFinite(config.balancePercent) || config.balancePercent < -100 || config.balancePercent > 100) {
    throw new RangeError("Balance must be between -100 and 100 %.");
  }
  if (!Number.isFinite(config.widthPercent) || config.widthPercent < 0 || config.widthPercent > 200) {
    throw new RangeError("Stereo width must be between 0 and 200 %.");
  }
}

export function encodeOutputProcessing(config: OutputProcessingConfig): Uint8Array {
  validateOutputProcessing(config);
  const payload = new Uint8Array(8);
  const view = viewFor(payload);
  payload[0] = (config.mono ? OutputProcessingFlag.Mono : 0) |
    (config.swap ? OutputProcessingFlag.Swap : 0) |
    (config.invertLeft ? OutputProcessingFlag.InvertLeft : 0) |
    (config.invertRight ? OutputProcessingFlag.InvertRight : 0);
  view.setInt16(2, Math.round(config.balancePercent * 100), true);
  view.setUint16(4, Math.round(config.widthPercent * 100), true);
  return payload;
}

export function decodeOutputProcessing(payload: Uint8Array): OutputProcessingConfig {
  if (payload.length !== 8 || payload[1] !== 0 || payload[6] !== 0 || payload[7] !== 0 ||
      (payload[0] & ~0x0f) !== 0) {
    throw new Error("Invalid output processing response");
  }
  const view = viewFor(payload);
  const flags = payload[0];
  const config = {
    mono: (flags & OutputProcessingFlag.Mono) !== 0,
    swap: (flags & OutputProcessingFlag.Swap) !== 0,
    invertLeft: (flags & OutputProcessingFlag.InvertLeft) !== 0,
    invertRight: (flags & OutputProcessingFlag.InvertRight) !== 0,
    balancePercent: view.getInt16(2, true) / 100,
    widthPercent: view.getUint16(4, true) / 100,
  };
  validateOutputProcessing(config);
  return config;
}

export function decodeOutputProcessingState(payload: Uint8Array): OutputProcessingState {
  if (payload.length !== 17 || payload[16] > 1) throw new Error("Invalid output processing state response");
  return {
    live: decodeOutputProcessing(payload.subarray(0, 8)),
    saved: decodeOutputProcessing(payload.subarray(8, 16)),
    dirty: payload[16] === 1,
  };
}

export function outputProcessingConfigsEqual(left: OutputProcessingConfig, right: OutputProcessingConfig): boolean {
  return left.mono === right.mono && left.swap === right.swap && left.invertLeft === right.invertLeft &&
    left.invertRight === right.invertRight && left.balancePercent === right.balancePercent &&
    left.widthPercent === right.widthPercent;
}

export interface DeviceStatus {
  firmwareVersion: string;
  bandCount: number;
  streaming: boolean;
  dirty: boolean;
  eqEnabled: boolean;
  sampleRateHz: number;
  bitDepth: number | null;
  configGeneration: number;
  savedGeneration: number;
  appliedGeneration: number;
  underrunFrames: number;
  backpressureEvents: number;
  temperatureC: number | null;
  systemClockMHz: number | null;
  maxDspBlockUs: number | null;
  i2sLowWaterFrames: number | null;
}

export interface ResponsePacket {
  opcode: number;
  requestId: number;
  status: ProtocolStatus;
  payload: Uint8Array;
}

export interface StereoMeterLevel {
  leftPeak: number;
  rightPeak: number;
  leftMeanSquare: number;
  rightMeanSquare: number;
}

export interface MeterLevel {
  sequence: number;
  preEq: StereoMeterLevel;
  postEq: StereoMeterLevel;
  limiterActive: boolean;
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
  if (payload.length !== 28 && payload.length !== 32 && payload.length !== 44 && payload.length !== 48) {
    throw new Error("Invalid status response");
  }
  const view = viewFor(payload);
  const flags = payload[3];
  const bitDepth = payload.length >= 48 ? payload[44] : null;
  if (bitDepth !== null && bitDepth !== 16 && bitDepth !== 24) {
    throw new Error("Invalid status bit depth");
  }
  return {
    firmwareVersion: `${payload[0]}.${payload[1]}`,
    bandCount: payload[2],
    streaming: (flags & 0x01) !== 0,
    dirty: (flags & 0x02) !== 0,
    eqEnabled: (flags & 0x04) !== 0,
    sampleRateHz: view.getUint32(4, true),
    bitDepth,
    configGeneration: view.getUint32(8, true),
    savedGeneration: view.getUint32(12, true),
    appliedGeneration: view.getUint32(16, true),
    underrunFrames: view.getUint32(20, true),
    backpressureEvents: view.getUint32(24, true),
    temperatureC: payload.length >= 32 ? view.getInt32(28, true) / 1000 : null,
    systemClockMHz: payload.length >= 44 ? view.getUint32(32, true) / 1_000_000 : null,
    maxDspBlockUs: payload.length >= 44 ? view.getUint32(36, true) : null,
    i2sLowWaterFrames: payload.length >= 44 ? view.getUint32(40, true) : null,
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
  if (payload.length !== 28 && payload.length !== 32) throw new Error("Invalid audio meter report");
  if (payload.length === 32 && ((payload[28] & 0xfe) !== 0 || payload[29] !== 0 || payload[30] !== 0 || payload[31] !== 0)) {
    throw new Error("Invalid audio meter flags");
  }
  const view = viewFor(payload);
  return {
    sequence: view.getUint32(0, true),
    preEq: {
      leftPeak: view.getUint16(4, true),
      rightPeak: view.getUint16(6, true),
      leftMeanSquare: view.getUint32(8, true),
      rightMeanSquare: view.getUint32(12, true),
    },
    postEq: {
      leftPeak: view.getUint16(16, true),
      rightPeak: view.getUint16(18, true),
      leftMeanSquare: view.getUint32(20, true),
      rightMeanSquare: view.getUint32(24, true),
    },
    limiterActive: payload.length === 32 && (payload[28] & 0x01) !== 0,
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
