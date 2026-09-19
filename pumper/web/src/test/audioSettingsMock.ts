import { vi } from "vitest";
import { crossfeedConfigsEqual, decodeCrossfeed, decodeOutputProcessing, defaultCrossfeed, defaultOutputProcessing, encodeCrossfeed, encodeOutputProcessing, Opcode, outputProcessingConfigsEqual, ProtocolStatus, type CrossfeedState, type OutputProcessingState, type ResponsePacket } from "../protocol";

export function crossfeedPayload(state: CrossfeedState): Uint8Array {
  const payload = new Uint8Array(17);
  payload.set(encodeCrossfeed(state.live));
  payload.set(encodeCrossfeed(state.saved), 8);
  payload[16] = state.dirty ? 1 : 0;
  return payload;
}

export function outputProcessingPayload(state: OutputProcessingState): Uint8Array {
  const payload = new Uint8Array(17);
  payload.set(encodeOutputProcessing(state.live));
  payload.set(encodeOutputProcessing(state.saved), 8);
  payload[16] = state.dirty ? 1 : 0;
  return payload;
}

export function audioSettingsMock() {
  let state: CrossfeedState = { live: { ...defaultCrossfeed }, saved: { ...defaultCrossfeed }, dirty: false };
  let outputState: OutputProcessingState = { live: { ...defaultOutputProcessing }, saved: { ...defaultOutputProcessing }, dirty: false };
  const audio = new Uint8Array(9);
  const handle = vi.fn(async (opcode: Opcode, payload = new Uint8Array()): Promise<ResponsePacket> => {
    if (opcode === Opcode.SetCrossfeed) {
      const live = decodeCrossfeed(payload);
      state = { ...state, live, dirty: !crossfeedConfigsEqual(live, state.saved) };
    } else if (opcode === Opcode.SaveCrossfeed) {
      state = { live: { ...state.live }, saved: { ...state.live }, dirty: false };
    } else if (opcode === Opcode.SetOutputProcessing) {
      const live = decodeOutputProcessing(payload);
      outputState = { ...outputState, live, dirty: !outputProcessingConfigsEqual(live, outputState.saved) };
    } else if (opcode === Opcode.SaveOutputProcessing) {
      outputState = { live: { ...outputState.live }, saved: { ...outputState.live }, dirty: false };
    }
    return {
      opcode: opcode | 0x80,
      requestId: 1,
      status: ProtocolStatus.Ok,
      payload: opcode === Opcode.GetAudioControls ? audio.slice() :
        [Opcode.GetOutputProcessing, Opcode.SetOutputProcessing, Opcode.SaveOutputProcessing].includes(opcode) ? outputProcessingPayload(outputState) : crossfeedPayload(state),
    };
  });
  return {
    handle,
    audio,
    get state() { return state; },
    set state(next: CrossfeedState) { state = next; },
    get outputState() { return outputState; },
    set outputState(next: OutputProcessingState) { outputState = next; },
  };
}
