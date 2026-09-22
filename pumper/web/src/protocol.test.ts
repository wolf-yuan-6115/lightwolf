import { describe, expect, it } from "vitest";
import {
  createRequest,
  defaultConfig,
  decodeBand,
  decodeGlobal,
  decodeMeterLevel,
  decodeProfileState,
  decodeStatus,
  encodeBand,
  encodeGlobal,
  encodeMeterConfig,
  FilterType,
  Opcode,
  parseResponse,
  ProtocolStatus,
  REPORT_SIZE,
  WidthMode,
  CrossfeedMode,
  defaultCrossfeed,
  decodeAudioControls,
  decodeCrossfeed,
  decodeCrossfeedState,
  effectiveAudioControl,
  encodeCrossfeed,
  supportsAudioControls,
  supportsFirmware3Controls,
  defaultOutputProcessing,
  decodeOutputProcessing,
  decodeOutputProcessingState,
  encodeOutputProcessing,
} from "./protocol";

describe("Pumper HID protocol", () => {
  it("gates new audio commands at firmware 2.2", () => {
    for (const version of ["1.8", "2.0", "2.1", "invalid", "2.2oops"]) expect(supportsAudioControls(version)).toBe(false);
    for (const version of ["2.2", "2.10", "3.0"]) expect(supportsAudioControls(version)).toBe(true);
    expect([Opcode.GetAudioControls, Opcode.GetCrossfeed, Opcode.SetCrossfeed, Opcode.SaveCrossfeed]).toEqual([6, 7, 0x12, 0x26]);
    expect(createRequest(Opcode.SaveCrossfeed, 1)[6]).toBe(0);
  });

  it("gates firmware 3 controls and assigns their wire values", () => {
    for (const version of ["2.2", "2.99", "invalid", "3.0beta"]) expect(supportsFirmware3Controls(version)).toBe(false);
    for (const version of ["3.0", "3.1", "4.0"]) expect(supportsFirmware3Controls(version)).toBe(true);
    expect([Opcode.GetOutputProcessing, Opcode.SetOutputProcessing, Opcode.SaveOutputProcessing]).toEqual([0x08, 0x13, 0x27]);
    expect([FilterType.LowPass, FilterType.HighPass, FilterType.Notch, FilterType.BandPass]).toEqual([3, 4, 5, 6]);
  });

  it("round-trips output processing and decodes live, saved, and dirty state", () => {
    const config = { mono: true, swap: true, invertLeft: true, invertRight: false, balancePercent: -25.5, widthPercent: 150.25 };
    expect(Array.from(encodeOutputProcessing(config))).toEqual([7, 0, 0x0a, 0xf6, 0xb1, 0x3a, 0, 0]);
    expect(decodeOutputProcessing(encodeOutputProcessing(config))).toEqual(config);
    const payload = new Uint8Array(17);
    payload.set(encodeOutputProcessing(config));
    payload.set(encodeOutputProcessing(defaultOutputProcessing), 8);
    payload[16] = 1;
    expect(decodeOutputProcessingState(payload)).toEqual({ live: config, saved: defaultOutputProcessing, dirty: true });
  });

  it("rejects invalid output processing records", () => {
    for (const patch of [{ balancePercent: -100.01 }, { balancePercent: 100.01 }, { widthPercent: -0.01 }, { widthPercent: 200.01 }, { widthPercent: NaN }]) {
      expect(() => encodeOutputProcessing({ ...defaultOutputProcessing, ...patch })).toThrow();
    }
    const payload = encodeOutputProcessing(defaultOutputProcessing);
    payload[0] = 0x10;
    expect(() => decodeOutputProcessing(payload)).toThrow();
    payload[0] = 0;
    payload[6] = 1;
    expect(() => decodeOutputProcessing(payload)).toThrow();
    expect(() => decodeOutputProcessing(new Uint8Array(7))).toThrow();
    expect(() => decodeOutputProcessingState(new Uint8Array(16))).toThrow();
  });

  it("decodes host Q8.8 controls and combines master/channel state", () => {
    const payload = new Uint8Array([0, 0xf6, 0, 0xfb, 0x80, 0xff, 0, 1, 0]);
    const audio = decodeAudioControls(payload);
    expect(audio).toEqual({ master: { volumeDb: -10, muted: false }, left: { volumeDb: -5, muted: true }, right: { volumeDb: -0.5, muted: false } });
    expect(effectiveAudioControl(audio.master, audio.left)).toEqual({ volumeDb: -15, muted: true });
    expect(effectiveAudioControl({ ...audio.master, muted: true }, audio.right)).toEqual({ volumeDb: -10.5, muted: true });
    expect(() => decodeAudioControls(payload.subarray(0, 8))).toThrow();
    payload[6] = 2;
    expect(() => decodeAudioControls(payload)).toThrow();
    payload[6] = 0;
    new DataView(payload.buffer).setInt16(0, -51 * 256, true);
    expect(() => decodeAudioControls(payload)).toThrow();
  });

  it("encodes crossfeed in an eight-byte record and retains Custom values in preset modes", () => {
    const custom = { mode: CrossfeedMode.Custom, strengthPercent: 40, cutoffHz: 2000, delayMs: 0.6 };
    expect(Array.from(encodeCrossfeed(custom))).toEqual([4, 0, 0xa0, 0x0f, 0xd0, 7, 0x58, 2]);
    expect(decodeCrossfeed(encodeCrossfeed(custom))).toEqual(custom);
    const preset = { ...custom, mode: CrossfeedMode.Low };
    expect(decodeCrossfeed(encodeCrossfeed(preset))).toEqual(preset);
    const payload = new Uint8Array(17);
    payload.set(encodeCrossfeed(custom));
    payload.set(encodeCrossfeed(defaultCrossfeed), 8);
    payload[16] = 1;
    expect(decodeCrossfeedState(payload)).toEqual({ live: custom, saved: defaultCrossfeed, dirty: true });
    payload[16] = 2;
    expect(() => decodeCrossfeedState(payload)).toThrow();
    expect(() => decodeCrossfeedState(payload.subarray(0, 16))).toThrow();
  });

  it("rejects invalid crossfeed modes, lengths, reserved fields, and parameters", () => {
    for (const patch of [{ mode: 5 }, { mode: 1.5 }, { strengthPercent: -1 }, { strengthPercent: 41 }, { cutoffHz: 299 }, { cutoffHz: 2001 }, { delayMs: -0.01 }, { delayMs: 0.61 }, { delayMs: NaN }, { strengthPercent: Infinity }]) {
      expect(() => encodeCrossfeed({ ...defaultCrossfeed, ...patch })).toThrow();
    }
    const payload = encodeCrossfeed(defaultCrossfeed);
    payload[1] = 1;
    expect(() => decodeCrossfeed(payload)).toThrow();
    expect(() => decodeCrossfeed(new Uint8Array(7))).toThrow();
    payload[1] = 0;
    payload[0] = 5;
    expect(() => decodeCrossfeed(payload)).toThrow();
  });

  it("uses a flat factory EQ", () => {
    expect(defaultConfig.preampDb).toBe(0);
    expect(defaultConfig.bands.every((band) => band.gainDb === 0)).toBe(true);
  });

  it("builds a fixed-size little-endian request", () => {
    const report = createRequest(Opcode.GetBand, 0x1234, new Uint8Array([7]));
    expect(report).toHaveLength(REPORT_SIZE);
    expect(Array.from(report.slice(0, 9))).toEqual([80, 69, 1, Opcode.GetBand, 0x34, 0x12, 1, 0, 7]);
  });

  it("assigns device reset commands without payloads", () => {
    expect(Opcode.RestartDevice).toBe(0x40);
    expect(Opcode.EnterBootsel).toBe(0x41);
    expect(createRequest(Opcode.EnterBootsel, 7)[6]).toBe(0);
  });

  it("round-trips global milli-decibel values", () => {
    expect(decodeGlobal(encodeGlobal({ enabled: true, preampDb: -5.321, bands: [] }))).toEqual({
      enabled: true,
      preampDb: -5.321,
    });
  });

  it("round-trips a complete band", () => {
    const band = {
      enabled: true,
      type: FilterType.Peaking,
      widthMode: WidthMode.Bandwidth,
      frequencyHz: 1234.5,
      gainDb: -2.75,
      q: 1.41,
      bandwidthOctaves: 0.82,
    };
    expect(decodeBand(encodeBand(4, band))).toEqual({ index: 4, band });
  });

  it("parses a response and preserves its status", () => {
    const report = createRequest(Opcode.WriteFlash, 9);
    report[3] |= 0x80;
    report[7] = ProtocolStatus.StorageError;
    expect(parseResponse(report)).toMatchObject({
      opcode: Opcode.WriteFlash | 0x80,
      requestId: 9,
      status: ProtocolStatus.StorageError,
    });
  });

  it("decodes performance diagnostics and bit depth while accepting older status payloads", () => {
    const payload = new Uint8Array(48);
    const view = new DataView(payload.buffer);
    payload.set([1, 9, 10, 0x04]);
    view.setUint32(4, 192000, true);
    view.setInt32(28, 42375, true);
    view.setUint32(32, 180000000, true);
    view.setUint32(36, 417, true);
    view.setUint32(40, 322, true);
    payload[44] = 24;

    expect(decodeStatus(payload)).toMatchObject({
      firmwareVersion: "1.9",
      temperatureC: 42.375,
      systemClockMHz: 180,
      maxDspBlockUs: 417,
      i2sLowWaterFrames: 322,
      bitDepth: 24,
    });
    expect(decodeStatus(payload.slice(0, 32))).toMatchObject({
      temperatureC: 42.375,
      systemClockMHz: null,
      maxDspBlockUs: null,
      i2sLowWaterFrames: null,
      bitDepth: null,
    });
    expect(decodeStatus(payload.slice(0, 44)).bitDepth).toBeNull();
    expect(decodeStatus(payload.slice(0, 28)).temperatureC).toBeNull();
    expect(() => decodeStatus(new Uint8Array(30))).toThrow("Invalid status response");
    payload[44] = 20;
    expect(() => decodeStatus(payload)).toThrow("Invalid status bit depth");
  });

  it("encodes meter timing and decodes pre- and post-EQ stereo levels", () => {
    expect(Array.from(encodeMeterConfig(40, 1250))).toEqual([40, 0, 0xe2, 0x04]);

    const payload = new Uint8Array(28);
    const view = new DataView(payload.buffer);
    view.setUint32(0, 17, true);
    view.setUint16(4, 32768, true);
    view.setUint16(6, 16384, true);
    view.setUint32(8, 536870912, true);
    view.setUint32(12, 134217728, true);
    view.setUint16(16, 24576, true);
    view.setUint16(18, 8192, true);
    view.setUint32(20, 301989888, true);
    view.setUint32(24, 33554432, true);
    expect(decodeMeterLevel(payload)).toEqual({
      sequence: 17,
      preEq: {
        leftPeak: 32768,
        rightPeak: 16384,
        leftMeanSquare: 536870912,
        rightMeanSquare: 134217728,
      },
      postEq: {
        leftPeak: 24576,
        rightPeak: 8192,
        leftMeanSquare: 301989888,
        rightMeanSquare: 33554432,
      },
      limiterActive: false,
    });
    const extended = new Uint8Array(32);
    extended.set(payload);
    extended[28] = 0x01;
    expect(decodeMeterLevel(extended).limiterActive).toBe(true);
    extended[28] = 0x02;
    expect(() => decodeMeterLevel(extended)).toThrow("Invalid audio meter flags");
    extended[28] = 0x01;
    extended[31] = 1;
    expect(() => decodeMeterLevel(extended)).toThrow("Invalid audio meter flags");
    expect(() => decodeMeterLevel(new Uint8Array(16))).toThrow("Invalid audio meter report");
  });

  it("decodes ten-slot profile state", () => {
    const payload = new Uint8Array(12);
    const view = new DataView(payload.buffer);
    payload[0] = 10;
    payload[1] = 3;
    payload[2] = 1;
    view.setUint16(4, 0x020b, true);
    view.setUint32(8, 27, true);
    expect(decodeProfileState(payload)).toEqual({
      count: 10,
      activeProfile: 3,
      persistedProfile: 1,
      presentMask: 0x020b,
      bankGeneration: 27,
    });
  });
});
