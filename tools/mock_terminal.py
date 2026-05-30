#!/usr/bin/env python3
"""Mock S5P6818 terminal — a pure-Python SEC1 server for exercising the Qt PC
client on macOS, where the real terminal (OpenCV / V4L2 / framebuffer) can't be
built.

It speaks the same wire protocol as terminal/src/net/Protocol.h:
  * streams synthetic JPEG VIDEO_FRAMEs (~15 fps) with a moving "face" box,
  * reflects incoming SERVO_CMD / MODE_CMD in the rendered frame so you can SEE
    the d-pad, click-to-aim and auto/manual toggle take effect,
  * serves an in-memory capture store for FILE_LIST / FILE_GET / FILE_DELETE /
    SNAPSHOT.

Requires Pillow:  pip install pillow

Usage:
    tools/mock_terminal.py [host] [port]      # defaults: 0.0.0.0 8888
Then point the PC client at 127.0.0.1:8888.
"""
import io
import json
import math
import socket
import struct
import sys
import threading
import time

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

MAGIC = 0x53454331                 # 'S','E','C','1'
HEADER = struct.Struct(">IHHI")    # magic, type, flags, length
VSUB = struct.Struct(">IIIQ")      # seq, width, height, ts_ms
FDATA = struct.Struct(">IIB")      # idx, total, nameLen (FILE_DATA subheader)

# MsgType — mirror of terminal/src/net/Protocol.h.
T_HELLO            = 0x0001
T_VIDEO_FRAME      = 0x0010
T_SERVO_CMD        = 0x0020
T_MODE_CMD         = 0x0021
T_FILE_LIST_REQ    = 0x0030
T_FILE_LIST_RESP   = 0x0031
T_FILE_GET_REQ     = 0x0032
T_FILE_DATA        = 0x0033
T_FILE_DELETE_REQ  = 0x0034
T_FILE_DELETE_RESP = 0x0035
T_SNAPSHOT_REQ     = 0x0040
T_HEARTBEAT        = 0x00F0

W, H = 640, 480
FPS = 15
CHUNK = 16 * 1024


def clamp(v, lo=0.0, hi=180.0):
    return max(lo, min(hi, v))


def msg(mtype, payload=b""):
    return HEADER.pack(MAGIC, mtype, 0, len(payload)) + payload


def json_msg(mtype, obj):
    return msg(mtype, json.dumps(obj).encode("utf-8"))


def render_jpeg(seq, pan, tilt, auto, fps):
    """Draw one synthetic frame and return its JPEG bytes.

    The "face" box is positioned from pan/tilt (home 90/90 = centered); in auto
    mode it also drifts on its own, so the auto/manual toggle is visible.
    """
    img = Image.new("RGB", (W, H), (24, 26, 30))
    d = ImageDraw.Draw(img)

    # Center crosshair.
    d.line((W // 2, 0, W // 2, H), fill=(50, 54, 60))
    d.line((0, H // 2, W, H // 2), fill=(50, 54, 60))

    ax = (pan - 90.0) / 90.0
    ay = (tilt - 90.0) / 90.0
    if auto:
        t = seq / float(FPS)
        ax += 0.5 * math.sin(t * 1.3)
        ay += 0.35 * math.sin(t * 0.9 + 1.0)
    cx = int(W / 2 + ax * (W / 2 - 80))
    cy = int(H / 2 - ay * (H / 2 - 80))   # tilt+ = up = smaller y
    half = 60
    d.rectangle((cx - half, cy - half, cx + half, cy + half),
                outline=(80, 220, 120), width=3)
    d.line((cx - 8, cy, cx + 8, cy), fill=(80, 220, 120))
    d.line((cx, cy - 8, cx, cy + 8), fill=(80, 220, 120))

    for i, ln in enumerate([
        "MOCK TERMINAL  SEC1",
        f"frame {seq}",
        f"mode  {'AUTO' if auto else 'MANUAL'}",
        f"pan {pan:5.1f}  tilt {tilt:5.1f}",
        f"~{fps:.0f} fps",
    ]):
        d.text((10, 8 + i * 14), ln, fill=(220, 220, 220))

    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=80)
    return buf.getvalue()


class FileStore:
    """Thread-safe in-memory capture store, seeded with a couple of stills."""

    def __init__(self):
        self.lock = threading.Lock()
        self.files = {}   # name -> bytes
        self.mtime = {}   # name -> epoch seconds
        self.counter = 0
        for i in range(2):
            self.snapshot(age_hours=(i + 1))

    def snapshot(self, age_hours=0):
        with self.lock:
            self.counter += 1
            name = f"capture_{self.counter:04d}.jpg"
            self.files[name] = render_jpeg(self.counter * 30, 90, 90, False, FPS)
            self.mtime[name] = int(time.time()) - age_hours * 3600
        return name

    def list_json(self):
        with self.lock:
            arr = [{"name": n, "size": len(b), "mtime": self.mtime.get(n, 0)}
                   for n, b in sorted(self.files.items())]
        return json.dumps(arr).encode("utf-8")

    def get(self, name):
        with self.lock:
            return self.files.get(name)

    def delete(self, name):
        with self.lock:
            if name in self.files:
                del self.files[name]
                self.mtime.pop(name, None)
                return True
            return False


class Client(threading.Thread):
    """One connected client: a TX loop streams frames, an RX thread handles
    control/file messages. Both share one send lock (concurrent writes would
    interleave bytes and corrupt framing)."""

    def __init__(self, sock, addr, store):
        super().__init__(daemon=True)
        self.sock = sock
        self.addr = addr
        self.store = store
        self.send_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.pan = 90.0
        self.tilt = 90.0
        self.auto = True
        self.alive = True

    def send(self, data):
        with self.send_lock:
            self.sock.sendall(data)

    def recvn(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                return None
            buf.extend(chunk)
        return bytes(buf)

    def run(self):
        print(f"[+] client {self.addr[0]}:{self.addr[1]}")
        try:
            self.send(json_msg(T_HELLO, {"role": "terminal", "ver": 1}))
            threading.Thread(target=self.read_loop, daemon=True).start()
            self.stream_loop()
        except OSError:
            pass
        finally:
            self.alive = False
            self.sock.close()
            print(f"[-] client {self.addr[0]}:{self.addr[1]}")

    def stream_loop(self):
        seq = 0
        period = 1.0 / FPS
        last = time.time()
        fps = float(FPS)
        while self.alive:
            with self.state_lock:
                pan, tilt, auto = self.pan, self.tilt, self.auto
            self.send(msg(T_VIDEO_FRAME,
                          VSUB.pack(seq, W, H, int(time.time() * 1000)) +
                          render_jpeg(seq, pan, tilt, auto, fps)))
            seq += 1
            now = time.time()
            dt = now - last
            last = now
            if dt > 0:
                fps = 0.9 * fps + 0.1 * (1.0 / dt)
            rest = period - (time.time() - now)
            if rest > 0:
                time.sleep(rest)

    def read_loop(self):
        try:
            while self.alive:
                hdr = self.recvn(HEADER.size)
                if not hdr:
                    break
                magic, mtype, _flags, length = HEADER.unpack(hdr)
                if magic != MAGIC:
                    print(f"  ! bad magic 0x{magic:08x}", file=sys.stderr)
                    break
                payload = self.recvn(length) if length else b""
                if payload is None:
                    break
                self.handle(mtype, payload)
        except OSError:
            pass
        finally:
            self.alive = False

    def handle(self, mtype, payload):
        if mtype == T_HELLO:
            print(f"  HELLO {self._json(payload)}")
        elif mtype == T_SERVO_CMD:
            o = self._json(payload)
            mode = o.get("mode", "step")
            pan = float(o.get("pan", 0.0))
            tilt = float(o.get("tilt", 0.0))
            with self.state_lock:
                if mode == "abs":
                    self.pan, self.tilt = clamp(pan), clamp(tilt)
                else:
                    self.pan = clamp(self.pan + pan)
                    self.tilt = clamp(self.tilt + tilt)
                p, t = self.pan, self.tilt
            print(f"  SERVO {mode:4} -> pan={p:5.1f} tilt={t:5.1f}")
        elif mtype == T_MODE_CMD:
            with self.state_lock:
                self.auto = bool(self._json(payload).get("autoTrack", True))
                a = self.auto
            print(f"  MODE autoTrack={a}")
        elif mtype == T_FILE_LIST_REQ:
            self.send(msg(T_FILE_LIST_RESP, self.store.list_json()))
        elif mtype == T_FILE_GET_REQ:
            self.send_file(self._json(payload).get("name", ""))
        elif mtype == T_FILE_DELETE_REQ:
            name = self._json(payload).get("name", "")
            ok = self.store.delete(name)
            self.send(json_msg(T_FILE_DELETE_RESP, {"name": name, "ok": ok}))
            print(f"  DELETE {name} ok={ok}")
        elif mtype == T_SNAPSHOT_REQ:
            name = self.store.snapshot()
            print(f"  SNAPSHOT -> {name}")
            self.send(msg(T_FILE_LIST_RESP, self.store.list_json()))
        elif mtype != T_HEARTBEAT:
            print(f"  msg type=0x{mtype:04x} len={len(payload)}")

    def send_file(self, name):
        nb = name.encode("utf-8")[:255]
        data = self.store.get(name)
        if data is None:                       # not found: one empty chunk, total=0
            self.send(msg(T_FILE_DATA, FDATA.pack(0, 0, len(nb)) + nb))
            print(f"  GET {name} -> not found")
            return
        chunks = [data[i:i + CHUNK] for i in range(0, len(data), CHUNK)] or [b""]
        for idx, ch in enumerate(chunks):
            self.send(msg(T_FILE_DATA, FDATA.pack(idx, len(chunks), len(nb)) + nb + ch))
        print(f"  GET {name} -> {len(data)} bytes in {len(chunks)} chunk(s)")

    @staticmethod
    def _json(payload):
        try:
            return json.loads(payload or b"{}")
        except ValueError:
            return {}


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "0.0.0.0"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8888

    store = FileStore()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(5)
    print(f"mock terminal listening on {host}:{port}  (Ctrl-C to stop)")
    try:
        while True:
            sock, addr = srv.accept()
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            Client(sock, addr, store).start()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
