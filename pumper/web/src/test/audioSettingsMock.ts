import { vi } from "vitest";
import { crossfeedConfigsEqual, decodeCrossfeed, defaultCrossfeed, encodeCrossfeed, Opcode, ProtocolStatus, type CrossfeedState, type ResponsePacket } from "../protocol";

export function crossfeedPayload(state: CrossfeedState): Uint8Array {
  const payload = new Uint8Array(17);
  payload.set(encodeCrossfeed(state.live));
  payload.set(encodeCrossfeed(state.saved), 8);
  payload[16] = state.dirty ? 1 : 0;
  return payload;
}

export function audioSettingsMock() {
  let state: CrossfeedState = { live: { ...defaultCrossfeed }, saved: { ...defaultCrossfeed }, dirty: false };
  const audio = new Uint8Array(9);
  const handle = vi.fn(async (opcode: Opcode, payload = new Uint8Array()): Promise<ResponsePacket> => {
    if (opcode === Opcode.SetCrossfeed) {
      const live = decodeCrossfeed(payload);
      state = { ...state, live, dirty: !crossfeedConfigsEqual(live, state.saved) };
    } else if (opcode === Opcode.SaveCrossfeed) {
      state = { live: { ...state.live }, saved: { ...state.live }, dirty: false };
    }
    return {
      opcode: opcode | 0x80,
      requestId: 1,
      status: ProtocolStatus.Ok,
      payload: opcode === Opcode.GetAudioControls ? audio.slice() : crossfeedPayload(state),
    };
  });
  return { handle, audio, get state() { return state; }, set state(next: CrossfeedState) { state = next; } };
}
