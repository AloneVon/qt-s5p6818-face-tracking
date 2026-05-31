# SEC Mobile — Phase 3 client (Vue 3 + Capacitor)

Mobile client for the S5P6818 face‑tracking security terminal. Live video, a
gimbal d‑pad with tap‑to‑aim, auto/manual toggle, and a snapshot file gallery —
all over the same **SEC1** wire protocol the terminal and PC client speak.

## Transport: SEC1 over WebSocket

A browser / Capacitor WebView **cannot open a raw TCP socket**, so this client
talks to the terminal over a **WebSocket that carries SEC1 frames**. The terminal
sends **exactly one SEC1 frame per WebSocket binary message**, which means —
unlike the TCP clients — there is *no* cross‑message stream reassembly: each
`onmessage` `ArrayBuffer` is a complete frame. File transfers are still chunked
at the protocol level (`FILE_DATA`) and reassembled in the store.

The frame format is unchanged (12‑byte big‑endian header + payload; video =
20‑byte subheader + JPEG; control/file = JSON). See
[`../protocol/PROTOCOL.md`](../protocol/PROTOCOL.md) and the TypeScript port in
[`src/protocol.ts`](src/protocol.ts), which mirrors
[`terminal/src/net/Protocol.h`](../terminal/src/net/Protocol.h).

> The terminal serves WebSocket **natively** on port `8889` (alongside TCP on
> `8888`) — no external bridge needed. Point this client at the board's LAN IP
> and port `8889`. For desktop development, the mock below is the same WebSocket
> server with synthetic frames.

## Structure

```
mobile-client/
├─ index.html              # mobile viewport meta, #app mount
├─ capacitor.config.ts     # appId/appName; webDir=dist (wraps the built web app)
├─ src/
│  ├─ main.ts              # createApp + Pinia
│  ├─ protocol.ts          # SEC1 encode/decode (DataView, big‑endian)
│  ├─ stores/terminal.ts   # Pinia store: WebSocket, frame dispatch, controls
│  ├─ components/
│  │  ├─ ConnectionBar.vue # host/port + connect, status dot
│  │  ├─ VideoView.vue     # <img> JPEG view + HUD + tap‑to‑aim (letterbox math)
│  │  ├─ ControlPad.vue    # d‑pad (▲▼◀▶⌂), step slider, auto‑track checkbox
│  │  └─ FileGallery.vue   # refresh/snapshot + per‑file save/delete
│  ├─ App.vue              # shell: connection, video, Control/Files tabs
│  └─ style.css            # mobile‑first dark theme
```

All UI state lives in the `useTerminal` Pinia store: it owns the socket,
decodes each frame (`VIDEO_FRAME` → object‑URL `<img>` with an fps counter,
`FILE_LIST_RESP`/`FILE_DATA`/`FILE_DELETE_RESP`), and exposes the control
senders (`nudge`, `home`, `aim`, `setMode`, `refreshFiles`, `downloadFile`,
`deleteFile`, `snapshot`). Tap‑to‑aim converts a tap into a `[-1,1]` offset from
frame center (accounting for the `object-fit: contain` letterbox) and sends a
proportional `SERVO_CMD` step.

## Develop & build (this Mac — no board needed)

```bash
cd mobile-client
npm install
npm run dev        # Vite dev server; open the printed URL
npm run build      # vue-tsc typecheck + production build → dist/
```

### Test against the mock terminal

The repo ships a **WebSocket** mock that serves synthetic SEC1 frames (no board,
no OpenCV). It reuses the TCP mock's renderer/handlers and only swaps the
transport, so the d‑pad, tap‑to‑aim, auto/manual toggle and file actions all
produce visible results.

```bash
# terminal A — start the mock (needs Pillow: pip install pillow)
tools/mock_terminal_ws.py 127.0.0.1 8889

# terminal B — run the client
cd mobile-client && npm run dev
```

Then connect to **`127.0.0.1` / `8889`** (the defaults in the connection bar).
The TCP mock stays on `8888`, so both mocks can run at once.

## Package as a native app (on a machine with the SDKs)

Capacitor wraps the built `dist/` into a native shell:

```bash
npm run build
npx cap add android        # and/or: npx cap add ios
npx cap sync
npx cap open android       # build/run from Android Studio / Xcode
```

Use the device's LAN IP (not `127.0.0.1`) for the terminal host. For a plain
`ws://` (non‑TLS) connection, Android needs cleartext enabled for that host.
