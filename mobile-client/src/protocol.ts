// SEC1 wire protocol for the browser/Capacitor client.
//
// Mirrors terminal/src/net/Protocol.h. Transport is a WebSocket in binary
// mode, and the terminal/bridge sends exactly one SEC1 frame per WS message,
// so — unlike the TCP clients — there is no cross-message stream reassembly:
// each `onmessage` ArrayBuffer is a complete frame.
//
//   Frame = Header(12 bytes, big-endian) + payload(length bytes)
//     magic u32 = 0x53454331 ('S','E','C','1'), type u16, flags u16, length u32
//   VIDEO_FRAME payload = subheader(20) + JPEG: seq u32, w u32, h u32, ts u64.
//   Control/file messages carry a UTF-8 JSON payload.

export const MAGIC = 0x53454331;
export const HEADER_SIZE = 12;
export const VIDEO_SUBHEADER_SIZE = 20;

// A const object (not a TS `enum`) so the module is pure erasable syntax —
// the vue-tsc template builds with `erasableSyntaxOnly`, which bans enums.
export const MsgType = {
  Hello: 0x0001,
  VideoFrame: 0x0010,
  ServoCmd: 0x0020,
  ModeCmd: 0x0021,
  FileListReq: 0x0030,
  FileListResp: 0x0031,
  FileGetReq: 0x0032,
  FileData: 0x0033,
  FileDeleteReq: 0x0034,
  FileDeleteResp: 0x0035,
  SnapshotReq: 0x0040,
  Heartbeat: 0x00f0,
} as const;
export type MsgType = (typeof MsgType)[keyof typeof MsgType];

export interface Header {
  type: number;
  flags: number;
  length: number;
}

export interface VideoFrame {
  seq: number;
  width: number;
  height: number;
  tsMs: number;
  jpeg: Uint8Array;
}

export interface RemoteFile {
  name: string;
  size: number;
  mtime: number; // seconds since epoch
}

export interface FileDataChunk {
  idx: number;
  total: number;
  name: string;
  bytes: Uint8Array;
}

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

// ---- decoding ------------------------------------------------------------

/** Parse the 12-byte header from a complete frame; null if short/bad magic. */
export function decodeHeader(frame: Uint8Array): Header | null {
  if (frame.byteLength < HEADER_SIZE) return null;
  const v = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
  if (v.getUint32(0, false) !== MAGIC) return null;
  return {
    type: v.getUint16(4, false),
    flags: v.getUint16(6, false),
    length: v.getUint32(8, false),
  };
}

/** Payload slice (no copy) that follows the header in a complete frame. */
export function framePayload(frame: Uint8Array, header: Header): Uint8Array {
  return frame.subarray(HEADER_SIZE, HEADER_SIZE + header.length);
}

export function parseVideoFrame(payload: Uint8Array): VideoFrame | null {
  if (payload.byteLength < VIDEO_SUBHEADER_SIZE) return null;
  const v = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const hi = v.getUint32(12, false);
  const lo = v.getUint32(16, false);
  return {
    seq: v.getUint32(0, false),
    width: v.getUint32(4, false),
    height: v.getUint32(8, false),
    tsMs: hi * 2 ** 32 + lo, // ms comfortably fits in a JS number
    jpeg: payload.subarray(VIDEO_SUBHEADER_SIZE),
  };
}

// FILE_DATA payload = [u32 idx][u32 total][u8 nameLen][name][bytes].
export function parseFileData(payload: Uint8Array): FileDataChunk | null {
  if (payload.byteLength < 9) return null;
  const v = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const idx = v.getUint32(0, false);
  const total = v.getUint32(4, false);
  const nameLen = v.getUint8(8);
  if (payload.byteLength < 9 + nameLen) return null;
  return {
    idx,
    total,
    name: textDecoder.decode(payload.subarray(9, 9 + nameLen)),
    bytes: payload.subarray(9 + nameLen),
  };
}

export function parseJson<T = unknown>(payload: Uint8Array): T | null {
  try {
    return JSON.parse(textDecoder.decode(payload)) as T;
  } catch {
    return null;
  }
}

// ---- encoding ------------------------------------------------------------

export function buildMessage(type: MsgType, payload?: Uint8Array): ArrayBuffer {
  const len = payload ? payload.byteLength : 0;
  const buf = new ArrayBuffer(HEADER_SIZE + len);
  const v = new DataView(buf);
  v.setUint32(0, MAGIC, false);
  v.setUint16(4, type, false);
  v.setUint16(6, 0, false);
  v.setUint32(8, len, false);
  if (payload && len) new Uint8Array(buf, HEADER_SIZE).set(payload);
  return buf;
}

function buildJson(type: MsgType, obj: unknown): ArrayBuffer {
  return buildMessage(type, textEncoder.encode(JSON.stringify(obj)));
}

export const hello = (token?: string) =>
  buildJson(MsgType.Hello, token ? { role: 'mobile', ver: 1, token } : { role: 'mobile', ver: 1 });
export const servoStep = (dPan: number, dTilt: number) =>
  buildJson(MsgType.ServoCmd, { mode: 'step', pan: dPan, tilt: dTilt });
export const servoAbs = (pan: number, tilt: number) =>
  buildJson(MsgType.ServoCmd, { mode: 'abs', pan, tilt });
export const modeCmd = (autoTrack: boolean) =>
  buildJson(MsgType.ModeCmd, { autoTrack });
export const fileListReq = () => buildMessage(MsgType.FileListReq);
export const fileGetReq = (name: string) =>
  buildJson(MsgType.FileGetReq, { name });
export const fileDeleteReq = (name: string) =>
  buildJson(MsgType.FileDeleteReq, { name });
export const snapshotReq = () => buildMessage(MsgType.SnapshotReq);
