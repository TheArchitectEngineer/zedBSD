import sys, re, hashlib, json
sys.path.insert(0, sys.argv[2])
from vkdemo_oracle import verify_pixels
W, H = 320, 240
img = bytearray(W * H * 3)
seen = set()
pat = re.compile(rb'vkdump (\d+) (\d+) ([0-9a-f]{192})\s*$')
for line in open(sys.argv[1], 'rb'):
    m = pat.search(line)
    if not m:
        continue
    y, x, hx = int(m.group(1)), int(m.group(2)), m.group(3)
    img[(y * W + x) * 3:(y * W + x + 32) * 3] = bytes.fromhex(hx.decode())
    seen.add((y, x))
print('chunks', len(seen), 'of', H * 10)
print('sha256', hashlib.sha256(bytes(img)).hexdigest())
open(sys.argv[3], 'wb').write(b'P6\n%d %d\n255\n' % (W, H) + bytes(img))
r = verify_pixels(bytes(img), int(sys.argv[4]))
print(json.dumps(r)[:1500])
