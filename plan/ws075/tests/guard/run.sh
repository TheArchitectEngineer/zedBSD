#!/bin/sh
# ws075-p021: the guarded texture messages of the i915 compiler, on the host.  Each module below is compiled with
# shader-dump.c, disassembled with Mesa's brw_disasm and assembled back with brw_asm: the disassembler must accept
# every instruction, the re-assembled program must be the same bytes, every IF must have its ENDIF, and every
# sampler send of a module whose sends are all in branches must be inside an IF (panel.frag of the compositor, the
# browser's display.frag); a module whose blocks every channel runs (quad.frag) must have none, and switch.frag
# (no texture, branches only) must still pass the disassembler.  loop.frag has a guarded send inside a loop: its
# ENDIF's JIP must point at the loop's WHILE (checked in the disassembly).
#   plan/ws075/tests/guard/run.sh           (BRW_TOOLS: a Mesa build's src/intel/compiler, default below)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
tools=${BRW_TOOLS:-/home/awe/p014-c/mesa/build-asm/src/intel/compiler}
work=$(mktemp -d "${TMPDIR:-/tmp}/ws075-guard.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
cc -std=gnu99 -O0 -w -I"$repo" -I"$repo/include" -DHAL_ARCH_AMD64 -o "$work/dump" "$repo/plan/ws075/tests/guard/shader-dump.c" -lm
# The SPIR-V of the compositor's and the browser's shaders, from their checked-in headers.
python3 - "$repo" "$work" <<'PY'
import re, struct, sys
repo, work = sys.argv[1], sys.argv[2]
for header, names in (('userland/desktop/wayland/shaders.h', ('zwl_panel_frag', 'zwl_quad_frag')),
                      ('userland/desktop/browser/paint/shaders.h', ('paint_display_frag',))):
    text = open(f'{repo}/{header}').read()
    for name in names:
        body = re.search(name + r'\[\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
        words = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', body)]
        open(f'{work}/{name}.spv', 'wb').write(struct.pack('<%dI' % len(words), *words))
PY
cp "$repo/plan/ws075/tests/switch/switch.frag.spv" "$repo/plan/ws075/tests/switch/switch-O.frag.spv" \
	"$repo/plan/ws075/tests/guard/loop.frag.spv" "$work/"
status=0
for module in zwl_panel_frag zwl_quad_frag paint_display_frag switch.frag switch-O.frag loop.frag; do
	"$work/dump" fragment "$work/$module.spv" "$work/$module.bin" > /dev/null
	"$tools/brw_disasm" --gen=adl --input-path="$work/$module.bin" > "$work/$module.asm" 2>&1
	if grep -q 'ERROR\|illegal' "$work/$module.asm"; then
		echo "$module: FAIL the disassembler rejects an instruction"; status=1; continue
	fi
	"$tools/brw_asm" --gen=adl -o "$work/$module.re" "$work/$module.asm" > /dev/null 2>&1
	cmp -s "$work/$module.bin" "$work/$module.re" || { echo "$module: FAIL re-assembled bytes differ"; status=1; continue; }
	result=$(python3 - "$work/$module.asm" <<'PY'
import sys
depth = 0; ifs = 0; guarded = 0; bare = 0; bad = 0
for line in open(sys.argv[1]):
    if ' if(8)' in line: depth += 1; ifs += 1
    elif line.startswith('endif('): depth -= 1; bad |= depth < 0
    if line.lstrip().startswith('sampler MsgDesc'):
        if depth > 0: guarded += 1
        else: bare += 1
bad |= depth != 0
print(ifs, guarded, bare, int(bad))
PY
)
	set -- $result
	echo "$module: $1 IF, $2 sampler sends inside an IF, $3 outside"
	[ "$4" = 0 ] || { echo "$module: FAIL IF and ENDIF do not pair"; status=1; }
	case $module in
	zwl_quad_frag) [ "$1" = 0 ] || { echo "$module: FAIL a module every channel runs has an IF"; status=1; } ;;
	switch*) ;;
	loop.frag)
		[ "$3" = 0 ] && [ "$2" != 0 ] || { echo "$module: FAIL a sampler send in a branch is not guarded"; status=1; }
		python3 - "$work/$module.asm" <<'PY' || { echo "$module: FAIL an ENDIF inside the loop does not jump to its WHILE"; status=1; }
import re, sys
lines = open(sys.argv[1]).read().split('\n')
label_of = {}
current = None
for line in lines:
    m = re.match(r'(LABEL\d+):', line)
    if m: current = m.group(1); continue
    if line.strip() and current: label_of.setdefault(current, line); current = None
endifs = [re.search(r'JIP:\s+(LABEL\d+)', l).group(1) for l in lines if l.startswith('endif(')]
sys.exit(0 if any('while' in label_of[t] for t in endifs) else 1)
PY
		;;
	*) [ "$3" = 0 ] && [ "$2" != 0 ] || { echo "$module: FAIL a sampler send in a branch is not guarded"; status=1; } ;;
	esac
done
[ $status = 0 ] && echo "ws075-p021 guard host test PASS"
exit $status
