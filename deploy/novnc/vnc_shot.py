#!/usr/bin/env python3
"""u2-shot — grab one frame from the engine's X display and write it as a PNG.

Why this exists. The noVNC container runs Xvfb + the engine + x11vnc + websockify and
deliberately carries nothing else, so there is no xwd, no ImageMagick and no ffmpeg in
it: there was no way at all to ask a running session "what is on the screen right now".
That mattered because the engine's own surfaces cannot answer it. The play API returns
`"screen"`, which is the 40x25 TEXT grid -- enough to see that a message was printed,
not enough to see that it was printed in the wrong colour, under the star field, or
clipped by the scroll window. `--dump-screen` writes pixels but only the FIRST presented
frame, so it captures the splash and never the live game. Every defect in this area that
the project has chased (the space text pane, the dive sweep, the readout being shaved a
column per scroll) was found by a human looking at pixels, and the gates that missed them
were reading text. This closes that gap for the running container.

It is a minimal RFB 3.8 client: handshake, security type None, SetEncoding(Raw), one
non-incremental FramebufferUpdateRequest, and the raw pixels out. Standard library only,
because the image installs no extra Python for a screenshot path that did not exist.
It connects to 127.0.0.1 with `shared=1`, so it never disconnects the browser.

Anything it cannot decode it refuses instead of guessing: a half-decoded framebuffer is a
plausible image, and a plausible wrong image is the failure mode this repo keeps being
bitten by.

Run it inside the container (`podman exec u2-novnc u2-shot --out /tmp/shot.png`), or
locally against any VNC server that offers security type None.
"""
import argparse
import socket
import struct
import sys
import time
import zlib

CHUNK = 1 << 20


def rd(sock, n, why):
    """Read exactly n bytes. A short read is an error, never a partial frame."""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(min(CHUNK, n - len(buf)))
        if not chunk:
            sys.exit("u2-shot: connection closed while reading %s (got %d of %d bytes)"
                     % (why, len(buf), n))
        buf += chunk
    return bytes(buf)


def png_write(path, width, height, rgb):
    """Write an 8-bit truecolour PNG. Filter byte 0 on every scanline, one zlib stream."""
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        return (struct.pack("!I", len(data)) + tag + data
                + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(chunk(b"IHDR", struct.pack("!IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fh.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        fh.write(chunk(b"IEND", b""))


def ppm_write(path, width, height, rgb):
    with open(path, "wb") as fh:
        fh.write(b"P6\n%d %d\n255\n" % (width, height))
        fh.write(bytes(rgb))


def grab(host, port, timeout):
    # A container that has just come up refuses the connection for a second or two before
    # x11vnc has its display, so a refused connect is retried for the same reason the
    # greeting is: `up.sh --shot` immediately after `up.sh` is the normal sequence, and
    # "connection refused" there reads as a broken tool rather than a starting server.
    s = None
    for attempt in range(10):
        try:
            s = socket.create_connection((host, port), timeout=timeout)
            break
        except (ConnectionRefusedError, OSError):
            if attempt == 9:
                raise
            time.sleep(0.5)
    s.settimeout(timeout)

    ver = b""
    for attempt in range(6):
        try:
            ver = rd(s, 12, "server version")
            break
        except TimeoutError:
            # x11vnc intermittently accepts the socket and then does not send the greeting,
            # most often when the browser client is mid-update or the server has just come up.
            # Seen on the first attempt after a container start, twice in a row once. Retry
            # with a fresh socket rather than declaring the session dead.
            s.close()
            if attempt == 5:
                raise
            time.sleep(0.5)
            s = socket.create_connection((host, port), timeout=timeout)
            s.settimeout(timeout)
    if not ver.startswith(b"RFB "):
        sys.exit("u2-shot: not a VNC server on %s:%d (said %r)" % (host, port, ver))
    s.sendall(b"RFB 003.008\n")

    count = rd(s, 1, "security type count")[0]
    types = rd(s, count, "security types") if count else b""
    if 1 not in types:
        sys.exit("u2-shot: server offers no 'None' security type (%s); this client does "
                 "not do authentication" % " ".join(str(t) for t in types))
    s.sendall(struct.pack("!B", 1))
    result = struct.unpack("!I", rd(s, 4, "security result"))[0]
    if result != 0:
        sys.exit("u2-shot: security handshake rejected (%d)" % result)

    s.sendall(b"\x01")                       # shared=1: watch, do not evict the browser
    init = rd(s, 24, "ServerInit")
    width, height = struct.unpack("!HH", init[:4])
    bpp, depth, big_endian, truecolour = init[4], init[5], init[6], init[7]
    name = rd(s, struct.unpack("!I", init[20:24])[0], "desktop name")

    if bpp != 32 or depth != 24 or not truecolour:
        sys.exit("u2-shot: pixel format bpp=%d depth=%d truecolour=%d is not the 32-bit "
                 "true-colour this client decodes; refusing rather than guessing"
                 % (bpp, depth, truecolour))

    # SetEncoding: type 2, 1 pad byte, n-rectangles (u16), then n encoding-type ints.
    s.sendall(struct.pack("!BBH", 2, 0, 1) + struct.pack("!i", 0))
    # FramebufferUpdateRequest: type 3, incremental (u8), x,y,w,h (u16 each). Getting this
    # shape wrong is silent -- the server just never answers, and the client sits until it
    # times out, which is exactly what it looked like the first time this was written.
    s.sendall(struct.pack("!BBHHHH", 3, 0, 0, 0, width, height))

    while True:
        mtype = rd(s, 1, "message type")[0]
        if mtype == 0:
            break
        if mtype == 2:                                            # SetColourMapEntries
            rd(s, 1, "pad")
            count = struct.unpack("!HH", rd(s, 4, "cmap header"))[1]
            rd(s, count * 6, "cmap values")
        elif mtype in (1, 3):                                     # Bell, SetCursor
            continue
        else:
            sys.exit("u2-shot: unhandled server message type %d" % mtype)

    # FramebufferUpdate: type (u8), padding (u8), number-of-rectangles (u16), then the
    # rects. The rectangle count is easy to miss and the failure is not loud: everything
    # after it shifts by two bytes and the encoding reads as a nonsense number.
    rd(s, 1, "update padding")
    nrects = struct.unpack("!H", rd(s, 2, "rectangle count"))[0]
    if nrects != 1:
        sys.exit("u2-shot: server sent %d rectangles in one update; this client expects 1"
                 % nrects)
    x, y, w, h = struct.unpack("!HHHH", rd(s, 8, "rectangle header"))
    enc = struct.unpack("!i", rd(s, 4, "encoding type"))[0]
    if enc != 0:
        sys.exit("u2-shot: server chose encoding %d, not Raw" % enc)
    if (x, y, w, h) != (0, 0, width, height):
        sys.exit("u2-shot: server sent a partial rectangle %d,%d %dx%d of %dx%d"
                 % (x, y, w, h, width, height))

    raw = rd(s, w * h * 4, "framebuffer")
    # A 32-bit true-colour X display reaches x11vnc as B,G,R,x on the wire; swap to RGB.
    rgb = bytearray(w * h * 3)
    j = 0
    for i in range(0, len(raw), 4):
        rgb[j] = raw[i + 2]
        rgb[j + 1] = raw[i + 1]
        rgb[j + 2] = raw[i]
        j += 3
    return width, height, rgb, name.decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", default=5901, type=int,
                    help="VNC port (default 5901, or VNC_PORT if set)")
    ap.add_argument("--out", default="/tmp/u2-shot.png")
    ap.add_argument("--ppm", action="store_true", help="write PPM instead of PNG")
    ap.add_argument("--timeout", default=20.0, type=float)
    a = ap.parse_args()

    port = a.port
    if port == 5901:
        import os
        port = int(os.environ.get("VNC_PORT", "5901"))

    width, height, rgb, name = grab(a.host, port, a.timeout)
    if a.ppm:
        ppm_write(a.out, width, height, rgb)
    else:
        png_write(a.out, width, height, rgb)
    print("u2-shot: wrote %s %dx%d desktop=%r" % (a.out, width, height, name))


main()
