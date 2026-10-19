#!/bin/sh
# Host test of the sun4u loader's framebuffer and 8042 discovery (WS275 p002).
# Usage: plan/ws275/tests/host-handoff.sh [OUT_DIR]   (default build/ws275-p002/host)
set -eu
top=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$top/build/ws275-p002/host}
mkdir -p "$out"
# The only sparc-specific code: the physical register read becomes a fake.
python3 -I - "$top/bootloader/sparcv9/handoff.c" "$out/handoff-host.c" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
s, n = re.subn(r'\tunsigned int value;\n\n\t/\* Loads the word[^\n]*\n\t__asm__[^\n]*\n\n\t/\* Succeeded: the word. \*/\n\treturn value;',
               '\textern unsigned long fake_phys_read32_le(unsigned long long);\n\n\treturn fake_phys_read32_le(address);', s)
assert n == 1, "phys_read32_le not found"
open(sys.argv[2], 'w').write(s)
PY
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -I"$top/include" -I"$top/bootloader/sparcv9" \
  -o "$out/host-handoff" "$out/handoff-host.c" "$top/plan/ws275/tests/host-handoff-fake-ofw.c"
"$out/host-handoff"
