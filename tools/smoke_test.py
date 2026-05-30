#!/usr/bin/env python3
"""Minimal smoke test for the S5P6818 terminal TCP stream.

Connects to the terminal, parses the framed wire protocol, and on the first
VIDEO_FRAME writes the JPEG payload to disk (smoke.jpg). This confirms the
header framing + video subheader + JPEG encode end-to-end, without needing the
Qt PC client or the mobile client.

Usage:
    tools/smoke_test.py [host] [port]      # defaults: 127.0.0.1 8888

Run the terminal first (e.g. `make -C terminal host && terminal/build/terminal`
on a dev box with a webcam), then run this script.
"""
import socket
import struct
import sys

MAGIC = 0x53454331                 # 'S','E','C','1'
HEADER = struct.Struct(">IHHI")    # magic, type, flags, length
VSUB = struct.Struct(">IIIQ")      # seq, width, height, ts_ms

T_VIDEO_FRAME = 0x0010


def recvn(sock, n):
    """Read exactly n bytes or raise on early close."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed mid-message")
        buf.extend(chunk)
    return bytes(buf)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8888

    with socket.create_connection((host, port), timeout=10) as s:
        print(f"connected to {host}:{port}")
        while True:
            magic, mtype, flags, length = HEADER.unpack(recvn(s, HEADER.size))
            if magic != MAGIC:
                print(f"bad magic 0x{magic:08x}; not our protocol", file=sys.stderr)
                return 1
            payload = recvn(s, length)
            print(f"msg type=0x{mtype:04x} flags=0x{flags:04x} len={length}")

            if mtype == T_VIDEO_FRAME:
                seq, w, h, ts = VSUB.unpack(payload[:VSUB.size])
                jpeg = payload[VSUB.size:]
                with open("smoke.jpg", "wb") as f:
                    f.write(jpeg)
                print(f"VIDEO_FRAME seq={seq} {w}x{h} ts_ms={ts} "
                      f"jpeg={len(jpeg)} bytes -> wrote smoke.jpg")
                return 0


if __name__ == "__main__":
    sys.exit(main())
