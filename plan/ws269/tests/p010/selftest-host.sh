#!/bin/sh
# Host check of gpu-selftest (ws269-p010a, p010c): the stages' order, the skips
# after a failure, the time limit, and the total, with stand-ins for the
# stages text, the render node, vkcompute and vkimage.
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
chmod +x "$out"/vk-* "$out"/vki-*
fails=0
# check NAME EXPECTED-FILE ARGS...: the output must be exactly the expected lines.
check() {
	name=$1; shift
	"$out/gpu-selftest" "$@" > "$out/$name.out" 2>&1 || true
	if diff -u "$out/$name.expect" "$out/$name.out"; then
		echo "PASS $name"
	else
		echo "FAIL $name"; fails=$((fails + 1))
	fi
}
rest_lines='gpu-selftest: clear: skip(not built)
gpu-selftest: copy: skip(not built)
gpu-selftest: draw: skip(not built)
gpu-selftest: sample: skip(not built)
gpu-selftest: depth: skip(not built)
gpu-selftest: present: skip(not built)
gpu-selftest: frame: skip(not built)'
cat > "$out/pass.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: ok
$rest_lines
gpu-selftest: 6 ok, 0 fail, 7 skip
END
check pass -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass"
cat > "$out/firmware.expect" <<END
gpu-selftest: stages: firmware=fail:enoent render-node=- compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: fail(firmware=fail:enoent)
gpu-selftest: node: skip(kernel)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$rest_lines
gpu-selftest: 0 ok, 1 fail, 12 skip
END
check firmware -s "$out/stages-fw" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass"
cat > "$out/nonode.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: fail(enoent)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$rest_lines
gpu-selftest: 1 ok, 1 fail, 11 skip
END
check nonode -s "$out/stages-ok" -n "$out/missing-node" -i "$out/vki-pass" -v "$out/vk-pass"
cat > "$out/nodev.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: fail(vkcompute: no Vulkan device (vkEnumeratePhysicalDevices 0))
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$rest_lines
gpu-selftest: 3 ok, 1 fail, 9 skip
END
check nodev -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-nodev"
cat > "$out/compute.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: fail(vkcompute: X: FAIL, vkWaitForFences -4)
gpu-selftest: image: ok
$rest_lines
gpu-selftest: 5 ok, 1 fail, 7 skip
END
check compute -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-compute"
cat > "$out/hang.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: fail(timeout)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$rest_lines
gpu-selftest: 2 ok, 1 fail, 10 skip
END
start=$(date +%s)
check hang -t 1 -s "$out/stages-ok" -n "$out/node" -i "$out/vki-pass" -v "$out/vk-hang"
echo "hang took $(( $(date +%s) - start )) s (limit 1 s)"
cat > "$out/nosysctl.expect" <<END
gpu-selftest: kernel: fail(no stages)
gpu-selftest: node: skip(kernel)
gpu-selftest: instance: skip(node)
gpu-selftest: device: skip(instance)
gpu-selftest: compute: skip(device)
gpu-selftest: image: skip(device)
$rest_lines
gpu-selftest: 0 ok, 1 fail, 12 skip
END
check nosysctl -n "$out/node" -i "$out/vki-pass" -v "$out/vk-pass"
cat > "$out/image.expect" <<END
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
gpu-selftest: compute: ok
gpu-selftest: image: fail(vkimage: X: FAIL, external image -3)
$rest_lines
gpu-selftest: 5 ok, 1 fail, 7 skip
END
check image -s "$out/stages-ok" -n "$out/node" -i "$out/vki-fail" -v "$out/vk-pass"
echo "selftest-host: $fails failed"
[ "$fails" -eq 0 ]
