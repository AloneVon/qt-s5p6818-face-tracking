// Central client state: owns the WebSocket to the terminal/bridge, decodes the
// SEC1 frames it pushes, and exposes control senders the UI binds to.
//
// Transport note: the bridge sends exactly one SEC1 frame per WS binary
// message, so each `onmessage` ArrayBuffer is a whole frame — no stream
// reassembly here (contrast the TCP clients). File transfers are still
// chunked at the protocol level and reassembled in `onFileData`.

import { defineStore } from 'pinia';
import { ref, computed } from 'vue';
import * as proto from '../protocol';
import type { RemoteFile } from '../protocol';

export type ConnStatus = 'disconnected' | 'connecting' | 'connected';

// A click/tap at the video edge nudges the gimbal at most this many degrees.
const AIM_MAX_DEG = 12;

interface Download {
  name: string;
  total: number;
  parts: Map<number, Uint8Array>;
  received: number;
}

export const useTerminal = defineStore('terminal', () => {
  const status = ref<ConnStatus>('disconnected');
  const error = ref('');
  const peer = ref('');

  const frameUrl = ref('');
  const resW = ref(0);
  const resH = ref(0);
  const fps = ref(0);

  const autoTrack = ref(true);
  const files = ref<RemoteFile[]>([]);

  // Non-reactive transport scratch (kept out of refs to avoid proxy overhead).
  let ws: WebSocket | null = null;
  let lastUrl = '';
  const downloads = new Map<string, Download>();
  let frameCount = 0;
  let fpsAnchor = 0;

  const connected = computed(() => status.value === 'connected');

  function connect(host: string, port: number, token = '') {
    disconnect();
    const url = `ws://${host}:${port}`;
    peer.value = `${host}:${port}`;
    error.value = '';
    status.value = 'connecting';
    let sock: WebSocket;
    try {
      sock = new WebSocket(url);
    } catch (e) {
      status.value = 'disconnected';
      error.value = String(e);
      return;
    }
    sock.binaryType = 'arraybuffer';
    ws = sock;
    sock.onopen = () => {
      status.value = 'connected';
      // HELLO first (carries the token, if any); the terminal gates everything
      // else until it authenticates, so the file list request must follow it.
      send(proto.hello(token));
      refreshFiles();
    };
    sock.onmessage = (ev) => {
      if (ev.data instanceof ArrayBuffer) onMessage(ev.data);
    };
    sock.onerror = () => {
      if (status.value === 'connecting') error.value = 'Connection failed';
    };
    sock.onclose = () => {
      if (ws === sock) ws = null;
      status.value = 'disconnected';
      fps.value = 0;
    };
  }

  function disconnect() {
    const sock = ws;
    ws = null;
    if (sock) {
      sock.onopen = sock.onmessage = sock.onerror = sock.onclose = null;
      try {
        sock.close();
      } catch {
        /* already closing */
      }
    }
    status.value = 'disconnected';
    fps.value = 0;
    frameCount = 0;
    fpsAnchor = 0;
    downloads.clear();
    if (lastUrl) {
      URL.revokeObjectURL(lastUrl);
      lastUrl = '';
    }
    frameUrl.value = '';
  }

  function send(buf: ArrayBuffer) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(buf);
  }

  function onMessage(data: ArrayBuffer) {
    const frame = new Uint8Array(data);
    const header = proto.decodeHeader(frame);
    if (!header) return;
    const payload = proto.framePayload(frame, header);
    switch (header.type) {
      case proto.MsgType.VideoFrame:
        onVideo(payload);
        break;
      case proto.MsgType.FileListResp:
        onFileList(payload);
        break;
      case proto.MsgType.FileData:
        onFileData(payload);
        break;
      case proto.MsgType.FileDeleteResp:
        onFileDeleteResp(payload);
        break;
      // Hello / Heartbeat: nothing to do on the client.
    }
  }

  function onVideo(payload: Uint8Array) {
    const vf = proto.parseVideoFrame(payload);
    if (!vf) return;
    resW.value = vf.width;
    resH.value = vf.height;
    // slice() detaches a standalone copy so the Blob is unaffected when the
    // backing ArrayBuffer is reused/GC'd.
    const blob = new Blob([vf.jpeg.slice()], { type: 'image/jpeg' });
    const url = URL.createObjectURL(blob);
    if (lastUrl) URL.revokeObjectURL(lastUrl);
    lastUrl = url;
    frameUrl.value = url;

    const now = performance.now();
    if (!fpsAnchor) fpsAnchor = now;
    frameCount++;
    const span = now - fpsAnchor;
    if (span >= 1000) {
      fps.value = Math.round((frameCount * 1000) / span);
      frameCount = 0;
      fpsAnchor = now;
    }
  }

  function onFileList(payload: Uint8Array) {
    const list = proto.parseJson<RemoteFile[]>(payload);
    files.value = Array.isArray(list) ? list : [];
  }

  function onFileData(payload: Uint8Array) {
    const chunk = proto.parseFileData(payload);
    if (!chunk) return;
    // total === 0 is the terminal's "no such file" sentinel.
    if (chunk.total === 0) {
      downloads.delete(chunk.name);
      error.value = `File not found: ${chunk.name}`;
      return;
    }
    let dl = downloads.get(chunk.name);
    if (!dl) {
      dl = { name: chunk.name, total: chunk.total, parts: new Map(), received: 0 };
      downloads.set(chunk.name, dl);
    }
    if (!dl.parts.has(chunk.idx)) {
      dl.parts.set(chunk.idx, chunk.bytes.slice());
      dl.received++;
    }
    if (dl.received >= dl.total) {
      let totalLen = 0;
      for (const part of dl.parts.values()) totalLen += part.byteLength;
      // Concatenate into one freshly-allocated buffer (typed Uint8Array<ArrayBuffer>,
      // which Blob accepts; a subarray view would be Uint8Array<ArrayBufferLike>).
      const merged = new Uint8Array(totalLen);
      let off = 0;
      for (let i = 0; i < dl.total; i++) {
        const part = dl.parts.get(i);
        if (part) {
          merged.set(part, off);
          off += part.byteLength;
        }
      }
      downloads.delete(chunk.name);
      saveBlob(chunk.name, new Blob([merged], { type: 'application/octet-stream' }));
    }
  }

  function onFileDeleteResp(payload: Uint8Array) {
    const resp = proto.parseJson<{ ok?: boolean }>(payload);
    if (resp && resp.ok) refreshFiles();
  }

  // ---- control senders ---------------------------------------------------

  function nudge(dPan: number, dTilt: number) {
    send(proto.servoStep(dPan, dTilt));
  }

  function home() {
    send(proto.servoAbs(90, 90));
  }

  // dx, dy are normalized [-1, 1] offsets from frame center (screen axes:
  // +y down). Convert to a servo step: +pan = right, +tilt = up.
  function aim(dx: number, dy: number) {
    send(proto.servoStep(dx * AIM_MAX_DEG, -dy * AIM_MAX_DEG));
  }

  function setMode(on: boolean) {
    autoTrack.value = on;
    send(proto.modeCmd(on));
  }

  function refreshFiles() {
    send(proto.fileListReq());
  }

  function downloadFile(name: string) {
    send(proto.fileGetReq(name));
  }

  function deleteFile(name: string) {
    send(proto.fileDeleteReq(name));
  }

  function snapshot() {
    send(proto.snapshotReq());
  }

  return {
    status,
    error,
    peer,
    frameUrl,
    resW,
    resH,
    fps,
    autoTrack,
    files,
    connected,
    connect,
    disconnect,
    nudge,
    home,
    aim,
    setMode,
    refreshFiles,
    downloadFile,
    deleteFile,
    snapshot,
  };
});

// Trigger a browser download for a fully-reassembled file.
function saveBlob(name: string, blob: Blob) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  // Defer revoke so the download has a chance to start.
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}
