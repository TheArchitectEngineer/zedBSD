#!/bin/sh
# Host check of gpu-selftest (ws269-p010a, p010c): the stages' order, the skips
# after a failure, the time limit, and the total, with stand-ins for the
# stages text, the render node, vkcompute, vkimage and vkstage.
#   sh plan/ws269/tests/p010/selftest-host.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
out=$1/selftest
mkdir -p "$out"
cc -std=c11 -D_GNU_SOURCE -DGPU_SELFTEST_HOST -O2 -Wall -Wextra -Werror \
	"$root/userland/tests/gpu-selftest/main.c" -o "$out/gpu-selftest"
printf 'firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-\n' > "$out/stages-ok"
printf 'firmware=fail:enoent render-node=- compute=- ta=- 3d=- transfer=- present=-\n' > "$out/stages-fw"
: > "$out/node"
printf '#!/bin/sh\necho "vkcompute: PowerVR B-Series BXE-4-32: PASS, 4096/4096 words"\n' > "$out/vk-pass"
printf '#!/bin/sh\necho "vkcompute: no Vulkan device (vkEnumeratePhysicalDevices 0)"\nexit 1\n' > "$out/vk-nodev"
printf '#!/bin/sh\necho "vkcompute: X: FAIL, vkWaitForFences -4"\nexit 1\n' > "$out/vk-compute"
printf '#!/bin/sh\nsleep 30\n' > "$out/vk-hang"
printf '#!/bin/sh\necho "vkimage: X: PASS"\n' > "$out/vki-pass"
printf '#!/bin/sh\necho "vkimage: X: FAIL, external image -3"\nexit 1\n' > "$out/vki-fail"
printf '#!/bin/sh\necho "$1: X: PASS, seen"\n' > "$out/vks-pass"
printf '#!/bin/sh\nif [ "$1" = draw ]; then echo "draw: X: FAIL, pixel (32,32) 0xffff0000, expected 0xff00ff00"; exit 1; fi\necho "$1: X: PASS, seen"\n' > "$out/vks-draw"
printf '#!/bin/sh\nif [ "$1" = present ]; then sleep 30; fi\necho "$1: X: PASS, seen"\n' > "$out/vks-present"
python3 -I -c 'import sys
w, h = 320, 240
px = bytearray(b"\x10\x18\x28" * (w * h))
i = (120 * w + 160) * 3
px[i:i + 3] = bytes((0xd0, 0x70, 0x90))
open(sys.argv[1], "wb").write(b"P6\n320 240\n255\n" + bytes(px))' "$out/frame.ppm"
printf '#!/bin/sh\nfor a in "$@"; do case "$a" in --output=*) cp "%s" "${a#--output=}";; esac; done\necho "vkdemo: X: $1"\n' "$out/frame.ppm" > "$out/vkd-pass"
python3 -I -c 'import sys
open(sys.argv[1], "wb").write(b"P6\n320 240\n255\n" + b"\x00\x00\x00" * (320 * 240))' "$out/black.ppm"
printf '#!/bin/sh\nfor a in "$@"; do case "$a" in --output=*) cp "%s" "${a#--output=}";; esac; done\necho "vkdemo: X: $1"\n' "$out/black.ppm" > "$out/vkd-black"
chmod +x "$out"/vk-* "$out"/vki-* "$out"/vks-* "$out"/vkd-*
fails=0
# check NAME EXPECTED-FILE ARGS...: the output must be exactly the expected lines.
check() {
	name=$1; shift
	"$out/gpu-selftest" -d "$out/vkd-pass" -f "$out/frame-$name.ppm" "$@" > "$out/$name.out" 2>&1 || true
	if diff -u "$out/$name.expect" "$out/$name.out"; then
		echo "PASS $name"
	else
		echo "FAIL $name"; fails=$((fails + 1))
	fi
}
# The stages after image when image and device passed, and when they did not.
rest_lines='gpu-selftest: clear: ok
gpu-selftest: copy: ok
gpu-selftest: draw: ok
gpu-selftest: sample: ok
gpu-selftest: depth: ok
gpu-selftest: present-clear: ok
gpu-selftest: present: ok'
skip_image='gpu-selftest: clear: skip(image)
gpu-selftest: copy: skip(image)
gpu-selftest: draw: skip(image)
gpu-selftest: sample: skip(draw)
gpu-selftest: depth: skip(draw)'
skip_device="$skip_image
gpu-selftest: present-clear: skip(device)
gpu-selftest: present: skip(depth)"
cat > "$out/pass.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: ok
$rest_lines
gpu-selftest: 13 ok, 0 fail, 0 skip
END
check pass -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-pass"
cat > "$out/firmware.expect" <<END
gpu-selftest: stages: firmware=fail:enoent render-node=- compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: fail(firmware=fail:enoent)
gpu-selftest: node: skip(kernel)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$skip_device
gpu-selftest: 0 ok, 1 fail, 12 skip
END
check firmware -s "$out/stages-fw" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-pass"
cat > "$out/nonode.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: fail(enoent)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$skip_device
gpu-selftest: 1 ok, 1 fail, 11 skip
END
check nonode -s "$out/stages-ok" -n "$out/missing-node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-pass"
cat > "$out/nodev.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: fail(vkcompute: no Vulkan device (vkEnumeratePhysicalDevices 0))
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$skip_device
gpu-selftest: 3 ok, 1 fail, 9 skip
END
check nodev -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-nodev" -g "$out/vks-pass"
cat > "$out/compute.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: fail(vkcompute: X: FAIL, vkWaitForFences -4)
gpu-selftest: image: ok
$rest_lines
gpu-selftest: 12 ok, 1 fail, 0 skip
END
check compute -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-compute" -g "$out/vks-pass"
cat > "$out/hang.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: fail(timeout)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$skip_device
gpu-selftest: 2 ok, 1 fail, 10 skip
END
start=$(date +%s)
check hang -t 1 -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-hang" -g "$out/vks-pass"
echo "hang took $(( $(date +%s) - start )) s (limit 1 s)"
cat > "$out/nosysctl.expect" <<END
gpu-selftest: kernel: fail(no stages)
gpu-selftest: node: skip(kernel)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$skip_device
gpu-selftest: 0 ok, 1 fail, 12 skip
END
check nosysctl -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-pass"
cat > "$out/image.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: fail(vkimage: X: FAIL, external image -3)
$skip_image
gpu-selftest: present-clear: ok
gpu-selftest: present: skip(depth)
gpu-selftest: 6 ok, 1 fail, 6 skip
END
check image -s "$out/stages-ok" -n "$out/node" -i "$out/vki-fail" -v "$out/vk-pass" -g "$out/vks-pass"
cat > "$out/draw.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: ok
gpu-selftest: clear: ok
gpu-selftest: copy: ok
gpu-selftest: draw: fail(draw: X: FAIL, pixel (32,32) 0xffff0000, expected 0xff00ff00)
gpu-selftest: sample: skip(draw)
gpu-selftest: depth: skip(draw)
gpu-selftest: present-clear: ok
gpu-selftest: present: skip(depth)
gpu-selftest: 9 ok, 1 fail, 3 skip
END
check draw -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-draw"
cat > "$out/present.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: ok
gpu-selftest: clear: ok
gpu-selftest: copy: ok
gpu-selftest: draw: ok
gpu-selftest: sample: ok
gpu-selftest: depth: ok
gpu-selftest: present-clear: fail(timeout)
gpu-selftest: present: ok
gpu-selftest: 12 ok, 1 fail, 0 skip
END
start=$(date +%s)
check present -t 1 -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-present"
echo "present took $(( $(date +%s) - start )) s (limit 3 s)"
cat > "$out/frame.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: ok
gpu-selftest: clear: ok
gpu-selftest: copy: ok
gpu-selftest: draw: ok
gpu-selftest: sample: ok
gpu-selftest: depth: ok
gpu-selftest: present-clear: ok
gpu-selftest: present: fail(pixel (5,5) 000000, want 101828)
gpu-selftest: 12 ok, 1 fail, 0 skip
END
check frame -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass" -g "$out/vks-pass" -d "$out/vkd-black"
echo "selftest-host: $fails failed"
[ "$fails" -eq 0 ]
