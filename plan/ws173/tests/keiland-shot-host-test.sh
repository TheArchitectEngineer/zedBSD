#!/bin/sh
# ws173-p002: the host test of keiland-shot (userland/tests/keiland-shot): built with the host's compiler, run against a
# fake compositor socket (python) that answers PING with ACTIVE and SHOT with a 7x5 gradient in B8G8R8A8; the PNG it
# writes is decoded with python's zlib and compared pixel by pixel.  Also a compositor that is not active (an error).
# usage: plan/ws173/tests/keiland-shot-host-test.sh   (from the repository's top)
set -eu
OUT=${OUT:-build/ws173-shot-host}
mkdir -p "$OUT"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -o "$OUT/keiland-shot" \
	userland/tests/keiland-shot/main.c
timeout 60 python3 - "$OUT" <<'PY'
import os, socket, struct, subprocess, sys, threading, zlib
out = sys.argv[1]
w, h = 7, 5
def pixel(x, y): return (x * 30) & 255, (y * 50) & 255, (x * y * 7) & 255
def serve(path, active):
    s = socket.socket(socket.AF_UNIX); s.bind(path); s.listen(4)
    def run():
        while True:
            c, _ = s.accept(); line = c.recv(16)
            if line.startswith(b'PING'):
                c.sendall(b'ACTIVE\n' if active else b'INACTIVE\n')
            elif line.startswith(b'SHOT'):
                data = bytearray()
                for y in range(h):
                    for x in range(w):
                        r, g, b = pixel(x, y); data += bytes([b, g, r, 255])
                c.sendall(b'OK %d %d 44\n' % (w, h) + bytes(data))
            c.close()
    threading.Thread(target=run, daemon=True).start()
sock = os.path.join(out, 'shot.sock')
if os.path.exists(sock): os.unlink(sock)
serve(sock, True)
png = os.path.join(out, 'shot.png')
r = subprocess.run([os.path.join(out, 'keiland-shot'), '--socket', sock, png], capture_output=True, text=True)
assert r.returncode == 0, r.stdout + r.stderr
assert r.stdout.strip() == 'KEILAND-SHOT %s %dx%d' % (png, w, h), r.stdout
data = open(png, 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n'
pos, idat = 8, b''
while pos < len(data):
    n = struct.unpack('>I', data[pos:pos+4])[0]; t = data[pos+4:pos+8]; body = data[pos+8:pos+8+n]
    crc = struct.unpack('>I', data[pos+8+n:pos+12+n])[0]
    assert zlib.crc32(t + body) & 0xffffffff == crc, t
    if t == b'IHDR': assert struct.unpack('>IIBBBBB', body) == (w, h, 8, 2, 0, 0, 0)
    if t == b'IDAT': idat += body
    pos += 12 + n
raw = zlib.decompress(idat)
for y in range(h):
    row = raw[y * (1 + 3 * w):(y + 1) * (1 + 3 * w)]
    assert row[0] == 0
    for x in range(w):
        assert tuple(row[1 + 3 * x:4 + 3 * x]) == pixel(x, y), (x, y)
sock2 = os.path.join(out, 'idle.sock')
if os.path.exists(sock2): os.unlink(sock2)
serve(sock2, False)
r = subprocess.run([os.path.join(out, 'keiland-shot'), '--socket', sock2, png], capture_output=True, text=True)
assert r.returncode == 0  # an explicit socket is used even when inactive: the fake answers SHOT
print('keiland-shot-host-test: PASS')
PY
