# Wire Protocol (SEC1)

Single source of truth for the protocol between the **terminal** and every
client (Qt PC client, Vue/Capacitor mobile client). The C++ definition lives in
`terminal/src/net/Protocol.h`; this document mirrors it for non-C++ clients.

The same SEC1 message format is carried over **two transports** — raw TCP and
WebSocket — described under [Transports](#transports). The message layout below
is identical on both.

All multi-byte integers are **big-endian (network order)**.

## Framing

Every message is a 12-byte header followed by `length` payload bytes:

| offset | size | field  | notes                                  |
|--------|------|--------|----------------------------------------|
| 0      | 4    | magic  | `0x53454331` = ASCII `"SEC1"`          |
| 4      | 2    | type   | message type (below)                   |
| 6      | 2    | flags  | reserved, 0                            |
| 8      | 4    | length | number of payload bytes that follow    |

A reader must: read 12 bytes, validate magic, read `length` payload bytes,
dispatch on `type`, repeat. (TCP is a stream — always loop until you have the
full header, then the full payload.)

## Transports

The terminal listens on two ports; both speak the SEC1 messages above.

| transport | default port | client            | notes |
|-----------|--------------|-------------------|-------|
| TCP       | 8888         | Qt PC client      | SEC1 bytes straight on the socket. |
| WebSocket | 8889         | Vue/Capacitor app | SEC1 carried in WebSocket frames; a browser/WebView can't open raw TCP. |

**WebSocket carrier.** Standard RFC 6455: the client opens `ws://<host>:8889`,
the terminal completes the HTTP `Upgrade` handshake, then **each SEC1 message is
carried as exactly one binary WebSocket message** (terminal→client) — so a
client decodes one SEC1 frame per `onmessage` and never reassembles across
messages. Client→terminal messages are de-framed (and unmasked) back into the
SEC1 byte stream. Ping/pong and close are handled by the transport and never
surface to the SEC1 layer. There is no separate bridge process — the terminal
serves WebSocket natively (`terminal/src/net/WebSocketConn.{h,cpp}`, codec in
`WebSocketProto.h`); disable it with `Config::wsEnabled = false`.

## Message types

| type   | name           | dir | payload |
|--------|----------------|-----|---------|
| 0x0001 | HELLO          | ↔   | JSON `{"role":..,"ver":1}`; client adds `"token":".."` if required, terminal adds `"auth":true|false` (see [Authentication](#authentication-shared-token)) |
| 0x0010 | VIDEO_FRAME    | T→C | 20-byte subheader + JPEG (see below) |
| 0x0020 | SERVO_CMD      | C→T | JSON `{"mode":"step"|"abs","pan":<deg>,"tilt":<deg>}` |
| 0x0021 | MODE_CMD       | C→T | JSON `{"autoTrack":true|false}` |
| 0x0030 | FILE_LIST_REQ  | C→T | empty |
| 0x0031 | FILE_LIST_RESP | T→C | JSON `[{"name":..,"size":..,"mtime":..}, ...]` |
| 0x0032 | FILE_GET_REQ   | C→T | JSON `{"name":".."}` |
| 0x0033 | FILE_DATA      | T→C | `[u32 chunkIndex][u32 totalChunks][u8 nameLen][name][bytes]` |
| 0x0034 | FILE_DELETE_REQ| C→T | JSON `{"name":".."}` |
| 0x0035 | FILE_DELETE_RESP| T→C| JSON `{"name":..,"ok":true|false}` |
| 0x0040 | SNAPSHOT_REQ   | C→T | empty (terminal saves a JPEG, then sends FILE_LIST_RESP) |
| 0x00F0 | HEARTBEAT      | ↔   | empty |

### VIDEO_FRAME payload

| offset | size | field  |
|--------|------|--------|
| 0      | 4    | seq    (frame counter) |
| 4      | 4    | width  |
| 8      | 4    | height |
| 12     | 8    | ts_ms  (capture time, ms) |
| 20     | …    | JPEG bytes (length = header.length − 20) |

`mode:"step"` SERVO_CMD nudges the gimbal by `pan`/`tilt` degrees (relative);
`mode:"abs"` sets absolute target angles. Sending any SERVO_CMD switches the
terminal to manual mode for a few seconds (see `Config::manualOverrideMs`),
then auto-tracking resumes.

## Authentication (shared token)

The terminal can require a **shared token** before it serves a client. When
`Config::authToken` is non-empty, every client must present that exact token in
its **HELLO** before the terminal sends any video or accepts any control/file
message:

- Client→terminal `HELLO`: `{"role":"pc"|"mobile","ver":1,"token":"<token>"}`.
- Terminal→client `HELLO` advertises whether a token is needed —
  `{"role":"terminal","ver":1,"auth":true|false}` — sent right after the
  TCP connect / WebSocket upgrade, before any video.
- A connection that presents a wrong or missing token, sends any non-HELLO
  message before authenticating, or sends no valid HELLO within a few seconds is
  dropped (and, pre-auth, counts against `Config::maxClients` only briefly).

When `authToken` is empty (the default) the check is disabled: clients may omit
the token and the terminal behaves exactly as before. The token is compared in
constant time and is never written to the terminal logs.

## JS reader sketch (mobile/web)

```js
// data: ArrayBuffer accumulated from the socket
const dv = new DataView(data);
const magic = dv.getUint32(0);            // expect 0x53454331
const type  = dv.getUint16(4);
const len   = dv.getUint32(8);
const payload = new Uint8Array(data, 12, len);
// VIDEO_FRAME: jpeg = payload.subarray(20); show via Blob -> <img>
```

> WebView JS cannot open raw TCP sockets, so the mobile client connects to the
> terminal's **WebSocket** port (8889) and reads one SEC1 message per binary
> `onmessage` event — see [Transports](#transports).
