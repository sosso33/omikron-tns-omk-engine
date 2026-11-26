#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""THE SWITCH OVER THE NETWORK - hbmenu's netloader (Y in hbmenu), driven
from the Mac: nxlink's protocol (switchbrew/switch-tools `src/nxlink.c`) in
plain Python, so nothing beyond the standard library is installed.

    python3 tools/switch_link.py 192.168.1.142 engine/build/switch/omk.nro \\
        [--path omk/omk.nro] [--log FILE] [-- ARGS...]

Sends the NRO (stored at `sdmc:/switch/<--path>`, launched at once), then
waits for the program's own output: libnx's `nxlinkStdio` connects back to
this machine on port 28771 and its stdout comes here, printed and, with
`--log`, written. The ARGS go to the program (`switch_main.cpp`: `--ls DIR`,
`--cat FILE`, `--b64 FILE`, `--env K=V`, `--rmtree DIR` and `--no-plan` are
the launcher's; any other is the game's).

`--push LOCAL=DEST` (repeatable) sends files TO the card: LOCAL a file or a
folder (walked, symlinks followed), DEST its path under sdmc:/switch/omk/.
The program is given `--recv` and connects back on port 28772; per file its
path's length (u32 LE), the path, its size (u64 LE), its bytes; a zero
length ends, and the program answers how many files it wrote.

The protocol: a TCP connection to the console's port 28280; the name's
length (int32 LE) and the name; the file's length; the console's answer
(int32, 0 = go on; -1 no file, -2 no space, -3 no memory); the file deflated
(zlib), each piece as its length then its bytes; the console's answer; the
command line - its length, then the arguments, each NUL-ended.
"""
import argparse
import os
import socket
import struct
import sys
import zlib

SERVER_PORT = 28280   # the netloader
CLIENT_PORT = 28771   # the program's stdout, back to us
PUSH_PORT = 28772     # the files we send, `--push`
CHUNK = 16 * 1024


def recv_exact(s, n):
    b = b""
    while len(b) < n:
        part = s.recv(n - len(b))
        if not part:
            raise ConnectionError("the console closed the connection")
        b += part
    return b


def answer(s):
    return struct.unpack("<i", recv_exact(s, 4))[0]


def send_nro(addr, nro, path, args):
    data = open(nro, "rb").read()
    name = path.encode()
    with socket.create_connection((addr, SERVER_PORT), timeout=30) as s:
        s.sendall(struct.pack("<i", len(name)) + name + struct.pack("<i", len(data)))
        r = answer(s)
        if r != 0:
            raise RuntimeError({-1: "the console could not create the file", -2: "not enough space on the card",
                                -3: "not enough memory"}.get(r, f"refused ({r})"))
        z = zlib.compressobj()
        sent = 0
        for at in range(0, len(data), CHUNK):
            piece = z.compress(data[at:at + CHUNK])
            if at + CHUNK >= len(data):
                piece += z.flush()
            # nxlink sends what deflate gives, at most a chunk at a time
            for k in range(0, len(piece), CHUNK):
                p = piece[k:k + CHUNK]
                s.sendall(struct.pack("<i", len(p)) + p)
                sent += len(p)
        answer(s)
        # the command line: the file's name first, as nxlink's own, then the arguments
        words = [os.path.basename(nro)] + list(args)
        cmd = b"".join(w.encode() + b"\0" for w in words)
        s.sendall(struct.pack("<i", len(cmd)) + cmd)
        s.shutdown(socket.SHUT_WR)
    print(f"switch_link: {nro} sent as sdmc:/switch/{path} - {len(data)} bytes, {sent} deflated", flush=True)


def push_files(pushes, wait):
    """The files of every LOCAL=DEST, to the program's `--recv`."""
    items = []
    for spec in pushes:
        local, _, dest = spec.partition("=")
        local = os.path.realpath(local)
        dest = dest.strip("/")
        if os.path.isdir(local):
            for root, _dirs, files in os.walk(local, followlinks=True):
                for n in sorted(files):
                    if n.startswith("._") or n == ".DS_Store":
                        continue
                    full = os.path.join(root, n)
                    items.append((full, dest + "/" + os.path.relpath(full, local)))
        else:
            items.append((local, dest))
    total = sum(os.path.getsize(f) for f, _ in items)
    print(f"switch_link: {len(items)} files, {total / 1e6:.1f} MB to push", flush=True)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as ls:
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(("", PUSH_PORT))
        ls.listen(1)
        ls.settimeout(wait)
        conn, _ = ls.accept()
    with conn:
        for f, rel in items:
            r = rel.encode()
            conn.sendall(struct.pack("<I", len(r)) + r + struct.pack("<Q", os.path.getsize(f)))
            with open(f, "rb") as fh:
                while True:
                    b = fh.read(1 << 20)
                    if not b:
                        break
                    conn.sendall(b)
        conn.sendall(struct.pack("<I", 0))
        ack = struct.unpack("<I", recv_exact(conn, 4))[0]
    print(f"switch_link: the console wrote {ack} of {len(items)} files", flush=True)
    return 0 if ack == len(items) else 1


def serve(log, wait):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as ls:
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ls.bind(("", CLIENT_PORT))
        ls.listen(1)
        ls.settimeout(wait)
        try:
            conn, peer = ls.accept()
        except socket.timeout:
            print(f"switch_link: no output connection in {wait} s (a firewall on this Mac? the program "
                  "not started?)", file=sys.stderr)
            return 1
    out = open(log, "wb") if log else None
    with conn:
        print(f"switch_link: the program's output, from {peer[0]}", flush=True)
        while True:
            try:
                b = conn.recv(65536)
            except ConnectionResetError:   # libnx's socketExit ends it without a close
                break
            if not b:
                break
            sys.stdout.buffer.write(b)
            sys.stdout.flush()
            if out:
                out.write(b)
                out.flush()
    if out:
        out.close()
    print("switch_link: the program ended its output", flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("address")
    ap.add_argument("nro")
    ap.add_argument("--path", default="omk/omk.nro", help="where under sdmc:/switch/ (default omk/omk.nro)")
    ap.add_argument("--log", help="also write the output to this file")
    ap.add_argument("--wait", type=float, default=60.0, help="seconds to wait for the output connection")
    ap.add_argument("--push", action="append", default=[], metavar="LOCAL=DEST",
                    help="send LOCAL (a file or folder) to sdmc:/switch/omk/DEST; repeatable")
    ap.add_argument("args", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    args = a.args[1:] if a.args[:1] == ["--"] else a.args
    # listen BEFORE sending: the program may connect back at once
    import threading
    rc = [1]
    t = threading.Thread(target=lambda: rc.__setitem__(0, serve(a.log, a.wait)))
    t.start()
    prc = [0]
    pt = None
    if a.push:
        args = args + ["--recv"]
        pt = threading.Thread(target=lambda: prc.__setitem__(0, push_files(a.push, a.wait)))
        pt.start()
    send_nro(a.address, a.nro, a.path, args)
    if pt:
        pt.join()
    t.join()
    return rc[0] or prc[0]


if __name__ == "__main__":
    sys.exit(main())
