#!/bin/sh
# Host checks of the V3D 7.1 self-test's command lists and compute shaders (ws279-p003) and of the 7.1 native pass and
# record encoders (ws279-p005): the lists and records against the
# 7.1 packets of the pinned Mesa XML, the shader words against the unmodified MIT Mesa 7.1 packer.  Needs WS141's
# ignored Mesa snapshot under plan/ws141/temp/mesa, as plan/ws279/tests/qpu71-format-host-test.sh does.
# Usage: sh plan/ws279/tests/v3d71-host-test.sh [output-directory]
set -eu
out=${1:-build/ws279-v3d71-host}
mesa=plan/ws141/temp/mesa/src
mkdir -p "$out"
cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. \
	plan/ws279/tests/v3d71-cl-host-test.c src/drivers/gpu/bcm2711/v3d71-cl.c \
	-o "$out/v3d71-cl-host-test"
"$out/v3d71-cl-host-test" > "$out/v3d71-cl-host.log"
tail -1 "$out/v3d71-cl-host.log"
python3 plan/ws279/tests/v3d71-cl-packet-check.py "$mesa/broadcom/cle/v3d_packet.xml" "$out/v3d71-cl-host.log"
# The Mesa packer objects and the util shims of qpu71-format-host-test.sh.
sh plan/ws279/tests/qpu71-format-host-test.sh "$out/qpu71" > "$out/qpu71.log"
cc -std=gnu11 -Wall -Werror -Iinclude -Isrc -I. -I"$out/qpu71/mesa-host" -I"$mesa" \
	plan/ws279/tests/v3d71-shader-host-test.c src/drivers/gpu/bcm2711/v3d71-cl.c \
	"$out/qpu71/mesa-pack.o" "$out/qpu71/mesa-instr.o" -o "$out/v3d71-shader-host-test"
"$out/v3d71-shader-host-test"
# The 7.1 native pass and record encoders (ws279-p005) against the 7.1 packets and structures of the same XML.
cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. -DHAL_BOARD_RPI5 \
	plan/ws279/tests/native71-pass-host-test.c src/drivers/gpu/bcm2711/native71-pass.c \
	-o "$out/native71-pass-host-test"
"$out/native71-pass-host-test" > "$out/native71-pass-host.log"
tail -1 "$out/native71-pass-host.log"
python3 plan/ws279/tests/native71-pass-check.py "$mesa/broadcom/cle/v3d_packet.xml" "$out/native71-pass-host.log"
cc -std=c99 -Wall -Wextra -Werror -Iinclude -Isrc -I. -DHAL_BOARD_RPI5 \
	plan/ws279/tests/native71-state-host-test.c src/drivers/gpu/bcm2711/native71-bin.c \
	src/drivers/gpu/bcm2711/native71-shader.c src/drivers/gpu/bcm2711/native71-texture.c \
	src/drivers/gpu/bcm2711/native71-viewport.c -lm -o "$out/native71-state-host-test"
"$out/native71-state-host-test" > "$out/native71-state-host.log"
tail -1 "$out/native71-state-host.log"
python3 plan/ws279/tests/native71-state-check.py "$mesa/broadcom/cle/v3d_packet.xml" "$out/native71-state-host.log"
