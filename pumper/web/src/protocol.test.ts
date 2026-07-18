import { describe, expect, it } from "vitest";
import {
  createRequest,
  defaultConfig,
  decodeBand,
  decodeGlobal,
  decodeMeterLevel,
  decodeProfileState,
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

  it("encodes meter timing and decodes a stereo meter report", () => {
    expect(Array.from(encodeMeterConfig(40, 1250))).toEqual([40, 0, 0xe2, 0x04]);

    const payload = new Uint8Array(16);
    const view = new DataView(payload.buffer);
    view.setUint32(0, 17, true);
    view.setUint16(4, 32768, true);
    view.setUint16(6, 16384, true);
    view.setUint32(8, 536870912, true);
    view.setUint32(12, 134217728, true);
    expect(decodeMeterLevel(payload)).toEqual({
      sequence: 17,
      leftPeak: 32768,
      rightPeak: 16384,
      leftMeanSquare: 536870912,
      rightMeanSquare: 134217728,
    });
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
