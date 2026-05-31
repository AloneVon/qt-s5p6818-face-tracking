# PC Client — Qt desktop client (Phase 2)

A Qt Widgets desktop client for the S5P6818 face-tracking terminal. It connects
over the **SEC1** TCP protocol to view the live video stream, remote-control the
pan/tilt gimbal, and browse / download / delete recorded captures.

## Design: pure Qt, no OpenCV

The terminal streams **JPEG** video, and `QImage::loadFromData(bytes, "JPG")`
decodes JPEG natively — so the client needs **no OpenCV dependency** (which is
not installed on the authoring Mac). It links only `QtWidgets` + `QtNetwork`.

Framing is shared with the terminal: the client includes
[`terminal/src/net/Protocol.h`](../terminal/src/net/Protocol.h) directly (via
`INCLUDEPATH`), so the wire layout has a single source of truth.

| Class | File | Responsibility |
|-------|------|----------------|
| `TcpClient`   | `src/net/TcpClient.{h,cpp}`   | SEC1 framing over `QSslSocket` (plaintext or TLS); decodes frames, reassembles chunked downloads, builds JSON control messages; pins a self-signed cert in TLS mode |
| `VideoWidget` | `src/ui/VideoWidget.{h,cpp}`  | Paints the latest frame letterboxed; click emits a normalized aim offset |
| `ControlPanel`| `src/ui/ControlPanel.{h,cpp}` | Connection fields (host/port/token/TLS + cert), pan/tilt d-pad + step size, auto/manual toggle |
| `FileBrowser` | `src/ui/FileBrowser.{h,cpp}`  | Capture list with refresh / download / delete / snapshot |
| `MainWindow`  | `src/ui/MainWindow.{h,cpp}`   | Assembles the UI, wires widget intent to `TcpClient`, shows status (connection / resolution / client-side FPS) |

## Requirements

- **Qt 5.15** with `qmake` in `PATH` (`widgets` + `network` modules).
- A C++17 compiler.

> **macOS / Anaconda-Qt note:** Qt 5.15.2's mkspec links the `AGL` framework,
> which Apple removed from recent SDKs. The `.pro` overrides
> `QMAKE_LIBS_OPENGL = -framework OpenGL` so the app builds against the current
> Xcode SDK. Harmless on Qt builds that don't reference AGL.

## Build

Out-of-source (keeps generated `Makefile`, `moc_*`, `.app` out of the tree):

```bash
cd pc-client
mkdir -p build && cd build
qmake ..        # e.g. /opt/anaconda3/bin/qmake on the authoring Mac
make
```

## Run

```bash
# macOS
./sec-pc-client.app/Contents/MacOS/sec-pc-client
# Linux
./sec-pc-client
```

Enter the terminal's host/port (default `127.0.0.1:8888`) and click **Connect**.

## TLS (optional)

To encrypt the link, tick **TLS** and connect to the terminal's TLS port (default
`8443`). Toggling TLS auto-switches the port between the plaintext `8888` and TLS
`8443` defaults (it won't clobber a port you typed yourself).

Because the terminal uses a **self-signed** certificate, set the **Cert** field to
the matching `server-cert.pem` (from [`tools/gen-cert.sh`](../tools/gen-cert.sh)).
The client then **pins** that exact certificate — it proceeds only if the terminal
presents it, byte for byte, and refuses anything else. (A self-signed cert can't be
validated by a CA, so the pin *is* the trust anchor.) Leaving **Cert** empty still
encrypts the connection but does **not** authenticate the server — it's MITM-able,
a dev convenience only, and a warning is surfaced in the status bar.

> Qt's TLS is backed by **OpenSSL at runtime**. If `QSslSocket::supportsSsl()` is
> false (no OpenSSL found by Qt), the client reports that instead of connecting —
> install OpenSSL libs if you hit it.

## Test without a board

The real terminal needs OpenCV / V4L2 / framebuffer and won't build on macOS, so
a Python mock stands in for it: it streams synthetic SEC1 video and serves the
control + file channels, reflecting your servo/mode commands in the rendered
frame so you can *see* control take effect.

```bash
pip install pillow
tools/mock_terminal.py            # listens on 0.0.0.0:8888
```

Then launch the client, connect to `127.0.0.1:8888`, and:

- the video pane shows a moving "face" box with a HUD (frame #, mode, pan/tilt);
- the **d-pad** nudges pan/tilt by the **step** value; **⌂** re-centers (90°/90°);
- **clicking the video** aims toward that point (click-to-aim);
- the **Auto-track** checkbox flips the box between drifting (auto) and parked (manual);
- **Refresh / Download / Delete / Snapshot** drive the in-memory capture store.

## Wire protocol

`SEC1` — 12-byte big-endian header + payload; JPEG video, JSON control. Full
spec in [`protocol/PROTOCOL.md`](../protocol/PROTOCOL.md).
