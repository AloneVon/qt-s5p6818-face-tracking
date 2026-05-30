#!/usr/bin/env python3
"""Mock S5P6818 terminal over **WebSocket** — for exercising the Vue 3 / Capacitor
mobile client, whose browser/WebView transport can't open a raw TCP socket.

It serves the very same SEC1 frames as mock_terminal.py; the only difference is
the carrier. Each WebSocket *binary message* is exactly one complete SEC1 frame
(header + payload), which is the contract the mobile client relies on (no
cross-message reassembly — see mobile-client/src/protocol.ts).

Implementation reuses mock_terminal.Client wholesale and overrides only the
transport seam:
  * run()        — WS handshake, then HELLO + read thread + stream loop,
  * send()       — wrap each SEC1 frame in one binary WS frame,
  * read_loop()  — WS-deframe each client message back into a SEC1 frame.
Everything substantive (frame rendering, SERVO/MODE/FILE handling, the file
store) is inherited unchanged.

No third-party WebSocket lib is needed — the framing is hand-rolled to RFC 6455
(server side). Pillow is still required (inherited renderer).

Usage:
    tools/mock_terminal_ws.py [host] [port]     # defaults: 0.0.0.0 8889
Then point the mobile client at ws://127.0.0.1:8889  (TCP mock stays on 8888,
so both can run side by side).
"""
import base64
import hashlib
import socket
import struct
import sys
import threading

from mock_terminal import Client, FileStore, HEADER, MAGIC, T_HELLO, json_msg

# RFC 6455 magic GUID, concatenated with the client key to form the accept hash.
WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WSClient(Client):
    """A Client whose wire is a WebSocket. Inherits stream_loop()/handle()/
    send_file()/FileStore plumbing; replaces only the byte transport."""

    def __init__(self, sock, addr, store):
        super().__init__(sock, addr, store)
        self._rxbuf = bytearray()  # spillover past the HTTP handshake / partial frames

    # ---- lifecycle ------------------------------------------------------
    def run(self):
        print(f"[+] ws client {self.addr[0]}:{self.addr[1]}")
        try:
            if not self.handshake():
                print("  ! websocket handshake failed", file=sys.stderr)
                return
            self.send(json_msg(T_HELLO, {"role": "terminal", "ver": 1}))
            threading.Thread(target=self.read_loop, daemon=True).start()
            self.stream_loop()  # inherited: streams VIDEO_FRAMEs via self.send()
        except OSError:
            pass
        finally:
            self.alive = False
            try:
                self.sock.close()
            except OSError:
                pass
            print(f"[-] ws client {self.addr[0]}:{self.addr[1]}")

    # ---- transport overrides -------------------------------------------
    def send(self, data):
        # Inherited code hands us a complete SEC1 frame; ship it as one binary
        # WS message so the client sees exactly one frame per onmessage.
        self._ws_send(data, 0x2)

    def read_loop(self):
        try:
            while self.alive:
                frame = self._read_message()
                if frame is None:
                    break
                if len(frame) < HEADER.size:
                    continue
                magic, mtype, _flags, length = HEADER.unpack(frame[: HEADER.size])
                if magic != MAGIC:
                    print(f"  ! bad magic 0x{magic:08x}", file=sys.stderr)
                    break
                payload = frame[HEADER.size : HEADER.size + length]
                self.handle(mtype, payload)
        except OSError:
            pass
        finally:
            self.alive = False

    # ---- WebSocket framing ---------------------------------------------
    def handshake(self):
        data = bytearray()
        while b"\r\n\r\n" not in data:
            chunk = self.sock.recv(1024)
            if not chunk:
                return False
            data.extend(chunk)
            if len(data) > 16384:  # runaway / non-HTTP client
                return False
        head, _, rest = bytes(data).partition(b"\r\n\r\n")
        self._rxbuf.extend(rest)  # any bytes after the request belong to WS frames

        key = None
        for line in head.split(b"\r\n")[1:]:
            name, sep, val = line.partition(b":")
            if sep and name.strip().lower() == b"sec-websocket-key":
                key = val.strip()
                break
        if not key:
            return False

        accept = base64.b64encode(hashlib.sha1(key + WS_GUID).digest()).decode("ascii")
        resp = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
        )
        self.sock.sendall(resp.encode("ascii"))
        return True

    def _recv_exact(self, n):
        while len(self._rxbuf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                return None
            self._rxbuf.extend(chunk)
        out = bytes(self._rxbuf[:n])
        del self._rxbuf[:n]
        return out

    def _read_message(self):
        """Read one full application message (reassembling fragments), answering
        ping/close transparently. Returns bytes, or None on close/EOF."""
        message = bytearray()
        while True:
            hdr = self._recv_exact(2)
            if hdr is None:
                return None
            b0, b1 = hdr[0], hdr[1]
            fin = b0 & 0x80
            opcode = b0 & 0x0F
            masked = b1 & 0x80
            length = b1 & 0x7F
            if length == 126:
                ext = self._recv_exact(2)
                if ext is None:
                    return None
                length = struct.unpack(">H", ext)[0]
            elif length == 127:
                ext = self._recv_exact(8)
                if ext is None:
                    return None
                length = struct.unpack(">Q", ext)[0]
            mask = self._recv_exact(4) if masked else b""
            if masked and mask is None:
                return None
            payload = self._recv_exact(length) if length else b""
            if payload is None:
                return None
            if masked and length:  # client->server frames are always masked
                payload = bytes(payload[i] ^ mask[i % 4] for i in range(length))

            if opcode == 0x8:  # close
                return None
            if opcode == 0x9:  # ping -> pong
                self._ws_send(payload, 0xA)
                continue
            if opcode == 0xA:  # pong
                continue
            message.extend(payload)  # 0x0 continuation / 0x1 text / 0x2 binary
            if fin:
                return bytes(message)

    def _ws_send(self, payload, opcode):
        n = len(payload)
        if n < 126:
            header = struct.pack(">BB", 0x80 | opcode, n)
        elif n <= 0xFFFF:
            header = struct.pack(">BBH", 0x80 | opcode, 126, n)
        else:
            header = struct.pack(">BBQ", 0x80 | opcode, 127, n)
        # Server->client frames are never masked. One lock per whole frame so a
        # streamed VIDEO_FRAME and a FILE_* reply can't interleave their bytes.
        with self.send_lock:
            self.sock.sendall(header + payload)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "0.0.0.0"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8889

    store = FileStore()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(5)
    print(f"mock terminal (WebSocket) listening on ws://{host}:{port}  (Ctrl-C to stop)")
    try:
        while True:
            sock, addr = srv.accept()
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            WSClient(sock, addr, store).start()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
