# 基于 Qt 与 S5P6818 的全自动追踪安防系统

> Fully‑automatic face‑tracking security system on the **S5P6818** (Cortex‑A53)
> embedded Linux platform, with **Qt** desktop and **Vue 3 / Capacitor** mobile
> clients.

The embedded **terminal** captures camera video over **V4L2**, detects faces
with **OpenCV (Haar cascade)**, drives a **pan/tilt servo gimbal (PWM via sysfs)**
to keep the face centered, renders a local UI to the **Linux framebuffer**, and
streams the annotated video over **TCP** to multiple clients that can also
remote‑control the gimbal and browse recorded snapshots.

---

## Features

- **V4L2 capture** — raw `ioctl` + `mmap` pipeline, negotiates MJPEG (preferred) or YUYV.
- **Face detection & tracking** — Haar cascade on a downscaled gray frame; proportional
  control with deadzone + step clamp keeps the largest face centered.
- **Servo gimbal** — pan/tilt via the kernel PWM sysfs API (50 Hz, 1.0–2.0 ms → 0–180°).
- **Local UI** — annotated video + status bar (FPS, mode, angles, face, client count)
  blitted straight to `/dev/fb0`.
- **TCP + WebSocket streaming + control** — custom framed protocol (`SEC1`): JPEG video
  out, JSON control in; one thread per client multiplexes both directions with `poll()`.
  Served over raw TCP (PC client) and natively over WebSocket (browser/Capacitor mobile
  client) — one SEC1 frame per binary message, no external bridge.
- **Optional shared-token auth** — set `Config::authToken` to require every client to
  present a matching token in its `HELLO` before any video or control is served;
  constant-time compare, never logged. Empty (default) leaves the LAN open.
- **Optional TLS / wss** — build with `make TLS=1` (OpenSSL) and point
  `Config::tlsCertFile`/`tlsKeyFile` at a self-signed cert to also serve SEC1 over
  **TLS** (PC, `:8443`) and **wss** (mobile, `:8444`). Doubly opt-in (build flag +
  cert); plaintext ports stay open otherwise. The PC client *pins* the cert, the
  mobile device *trusts* it; generate one with [`tools/gen-cert.sh`](tools/gen-cert.sh).
- **File manager** — list / download (chunked) / delete / snapshot, with a path‑traversal guard.
- **Host‑dev mode** — `-DDEV_HOST` swaps the three hardware classes for desktop stubs
  (`cv::VideoCapture`, `cv::imshow`, logging servo) so ~90% of the logic can be developed
  on an Ubuntu‑x86 box with a USB webcam, no board required.

## Architecture (terminal)

Four long‑running threads plus one thread per connected client, glued by small
thread‑safe shared objects (`FrameHub`, `TrackingState`, `CommandQueue`,
`ClientRegistry`, `StatusBoard`):

```
 ┌──────────────────┐  publish   ┌───────────┐   read   ┌──────────────────┐
 │ T1 VisionThread  │ ─────────▶ │  FrameHub  │ ───────▶ │ T2 DisplayThread │
 │ V4L2 → Haar →    │            │ (Mat+JPEG, │          │ status overlay → │
 │ overlay + encode │            │  shared)   │          │ /dev/fb0 (fb)    │
 └────────┬─────────┘            └─────┬─────┘           └──────────────────┘
          │ face center                │ latest frame
          ▼                            ▼
 ┌──────────────────┐           ┌──────────────────────┐
 │  TrackingState   │           │ T4 TcpServer (accept)│
 └────────┬─────────┘           └───────────┬──────────┘
          │ read                            │ spawn per client
          ▼                                 ▼
 ┌──────────────────┐  CommandQueue  ┌──────────────────────────┐
 │ T3 BusinessThread│ ◀───────────── │ Tn ClientSession         │
 │ P‑control → PWM  │                │ JPEG out @FPS / ctrl+file │
 │ servo (gimbal)   │                │ in  (one poll() loop)    │
 └──────────────────┘                └──────────────────────────┘
```

A hardware‑abstraction layer (`ICamera` / `IDisplay` / `IServo`) is selected at
compile time in [`terminal/src/app/Hardware.h`](terminal/src/app/Hardware.h):

| Interface | Board build (default)        | Host‑dev build (`DEV_HOST=1`) |
|-----------|------------------------------|-------------------------------|
| camera    | `V4L2Camera` (ioctl + mmap)  | `HostCamera` (`cv::VideoCapture`) |
| display   | `Framebuffer` (`/dev/fb0`)   | `WindowDisplay` (`cv::imshow`) |
| servo     | `ServoController` (PWM sysfs) | `StubServo` (logs target angles) |

## Repository layout

```
.
├─ terminal/                 # Phase 1 — embedded terminal (C++17, OpenCV, pthreads)
│  ├─ Makefile               # cross g++ / host‑dev build
│  ├─ terminal.pro           # optional Qt Creator project (indexing convenience)
│  ├─ models/                # haarcascade_frontalface_default.xml
│  └─ src/
│     ├─ core/    FrameHub, TrackingState, CommandQueue, ClientRegistry, Config.h, …
│     ├─ capture/ V4L2Camera, HostCamera (ICamera)
│     ├─ vision/  FaceDetector (Haar)
│     ├─ display/ Framebuffer, WindowDisplay (IDisplay)
│     ├─ servo/   ServoController, StubServo (IServo), PanTiltTracker
│     ├─ net/     Protocol.h, TcpServer, ClientSession, MiniJson, SocketUtil,
│     │            IConn + TcpConn/WebSocketConn (transports), WebSocketProto.h
│     ├─ file/    FileManager
│     ├─ app/     VisionThread, DisplayThread, BusinessThread, Hardware.h
│     └─ main.cpp
├─ pc-client/                # Phase 2 — Qt desktop client (pure Qt, no OpenCV)
│  └─ src/
│     ├─ net/     TcpClient (SEC1 over QTcpSocket)
│     └─ ui/      VideoWidget, ControlPanel, FileBrowser, MainWindow
├─ mobile-client/            # Phase 3 — Vue 3 + Pinia + Capacitor (SEC1 over WebSocket)
│  └─ src/
│     ├─ protocol.ts         # SEC1 encode/decode (TypeScript port of Protocol.h)
│     ├─ stores/terminal.ts  # Pinia store: WebSocket, frame dispatch, controls
│     └─ components/         # ConnectionBar, VideoView, ControlPad, FileGallery
├─ protocol/PROTOCOL.md      # wire protocol spec (mirrors terminal/src/net/Protocol.h)
└─ tools/
   ├─ cross-build/README.md  # toolchain + OpenCV armhf cross‑build + deploy notes
   ├─ gen-cert.sh            # generate a self-signed TLS cert+key for the terminal
   ├─ smoke_test.py          # connects, grabs one VIDEO_FRAME, writes smoke.jpg
   ├─ mock_terminal.py       # pure-Python SEC1 server (TCP, :8888) for the PC client
   └─ mock_terminal_ws.py    # SEC1 server (WebSocket, :8889) for the mobile client
```

## Build & run (terminal)

> The terminal targets **Linux** (V4L2 / framebuffer / sysfs) and needs OpenCV +
> pthreads. It does **not** build on macOS — author anywhere, build on an Ubuntu
> host or the cross‑toolchain. Full details in
> [`tools/cross-build/README.md`](tools/cross-build/README.md).

**Host‑dev (Ubuntu‑x86 + USB webcam, no board):**

```bash
sudo apt install build-essential pkg-config libopencv-dev
cd terminal
make host            # = make DEV_HOST=1
./bin/terminal       # window shows annotated video; servo angles printed to stdout
```

**Cross build for the board (`arm-linux-gnueabihf`):**

```bash
cd terminal
make CROSS=arm-linux-gnueabihf- \
     PKG_CONFIG_PATH=/opt/armhf/lib/pkgconfig \
     PKG_CONFIG_SYSROOT_DIR=/opt/armhf
# then scp bin/terminal + models/ + armhf OpenCV .so to the board and run.
```

**Smoke‑test the stream** (with the terminal running):

```bash
tools/smoke_test.py 127.0.0.1 8888   # writes the first video frame to smoke.jpg
```

All board‑specific knobs (camera device, resolution, PWM chip/channels, servo
limits, tracking gains, TCP port, stream FPS) live in one place:
[`terminal/src/core/Config.h`](terminal/src/core/Config.h).

## Wire protocol

`SEC1` — a 12‑byte big‑endian header (`magic, type, flags, length`) + payload.
Video frames are raw JPEG with a 20‑byte subheader; control/file messages are
JSON. The terminal serves SEC1 over **both raw TCP (`:8888`) and WebSocket
(`:8889`)** — the latter for the browser/Capacitor mobile client, one SEC1 frame
per binary message — and, when built with TLS and given a cert, over **TLS
(`:8443`) and wss (`:8444`)** as well. An optional **shared token**
(`Config::authToken`) gates every transport — clients send it in their `HELLO`.
Full spec: [`protocol/PROTOCOL.md`](protocol/PROTOCOL.md).

## Roadmap

- [x] **Phase 1 — Embedded terminal**: capture, detection, tracking, framebuffer UI,
      TCP streaming + control, file management, host‑dev mode.
- [x] **Phase 2 — Qt PC client**: `QTcpSocket` + `QImage` JPEG decode (no OpenCV),
      live view, click-to-aim, gimbal d-pad, auto/manual toggle, file browser, snapshot.
- [x] **Phase 3 — Mobile client**: Vue 3 + Pinia + Capacitor; SEC1 carried over a
      WebSocket (one frame per binary message), live view, tap‑to‑aim, gimbal d‑pad,
      auto/manual toggle, file gallery. See [`mobile-client/`](mobile-client/README.md).

## Tech stack

C++17 · OpenCV · V4L2 · Linux framebuffer · PWM sysfs · POSIX threads · TCP ·
Qt (PC client) · Vue 3 + Capacitor (mobile) · GCC cross‑compile (`arm-linux-gnueabihf`).
