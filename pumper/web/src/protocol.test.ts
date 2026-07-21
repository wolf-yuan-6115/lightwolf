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
} from "./protocol";

describe("Pumper HID protocol", () => {
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

  it("decodes performance diagnostics while accepting older status payloads", () => {
    const payload = new Uint8Array(44);
    const view = new DataView(payload.buffer);
    payload.set([1, 9, 10, 0x04]);
    view.setUint32(4, 192000, true);
    view.setInt32(28, 42375, true);
    view.setUint32(32, 180000000, true);
    view.setUint32(36, 417, true);
    view.setUint32(40, 322, true);

    expect(decodeStatus(payload)).toMatchObject({
      firmwareVersion: "1.9",
      temperatureC: 42.375,
      systemClockMHz: 180,
      maxDspBlockUs: 417,
      i2sLowWaterFrames: 322,
    });
    expect(decodeStatus(payload.slice(0, 32))).toMatchObject({
      temperatureC: 42.375,
      systemClockMHz: null,
      maxDspBlockUs: null,
      i2sLowWaterFrames: null,
    });
    expect(decodeStatus(payload.slice(0, 28)).temperatureC).toBeNull();
    expect(() => decodeStatus(new Uint8Array(30))).toThrow("Invalid status response");
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
    });
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
