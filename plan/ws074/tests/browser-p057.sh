#!/bin/sh
# ws074-p057: the engine as libbrowser.so on the Venus guest (build-browser-image.sh).
# Checks:
#  1. On the host, from the image's build: libbrowser.so exports only the calls of <browser.h> (browser_*); /bin/browser
#     needs libbrowser.so and not the engine's libraries (libtruetype, the image libraries); browser-probe needs only
#     libbrowser.so and the C library (no Wayland, no Vulkan of its own).
#  2. In the guest: browser-probe draws test pages with the CPU and with the engine's offscreen GPU image (Venus), each
#     byte for byte the same as /bin/browser --render and --render-gpu of the same page; with --tab=1 on keys.html it
#     writes the page's console lines (keydown, focusin) and draws the focus ring (probe-keys.png).
#  3. /bin/browser's script mode runs through the library (--js of a test script prints its lines).
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p057.sh [OUTDIR] [BUILD]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p057}
build=${2:-build/amd64}
mkdir -p "$out"
guest() { timeout 180 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
pages=/usr/share/browser-tests
readelf=build/llvm/bin/llvm-readelf
nm=build/llvm/bin/llvm-nm
status=0

# 1. The library's exports and the programs' needs.
$nm -D --defined-only "$build/dynamic/libbrowser.so" | awk '{print $NF}' | sort > "$out/exported.txt"
sed -n 's/^[a-z].*[ *]\(browser_[a-z_]*\)(.*/\1/p' include/libc/browser.h | sort > "$out/declared.txt"
if cmp -s "$out/exported.txt" "$out/declared.txt"; then
	echo "exports: the $(wc -l < "$out/declared.txt") calls <browser.h> declares, and nothing else, ok"
else
	echo "exports: differ from <browser.h>:"
	diff "$out/declared.txt" "$out/exported.txt"
	status=1
fi
needs() { $readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p' | tr '\n' ' '; }
echo "needs: browser: $(needs "$build/bin/browser")"
echo "needs: browser-probe: $(needs "$build/bin/browser-probe")"
echo "needs: libbrowser.so: $(needs "$build/dynamic/libbrowser.so")"
case " $(needs "$build/bin/browser-probe")" in
" libbrowser.so libc.so ") echo "needs: browser-probe ok" ;;
*) echo "needs: browser-probe needs more than libbrowser.so and libc.so"; status=1 ;;
esac
case " $(needs "$build/bin/browser")" in
*libtruetype*|*jpeg*|*png*|*gif*) echo "needs: browser still needs the engine's libraries"; status=1 ;;
*libbrowser.so*) echo "needs: browser ok" ;;
*) echo "needs: browser does not need libbrowser.so"; status=1 ;;
esac

# 2. The probe against the program, with the CPU and the GPU.
for page in first blocks script; do
	result=$(guest "cd /tmp; /bin/browser --render --width=800 --height=600 --output=/tmp/$page-main-cpu.ppm $pages/$page.html 2>/dev/null;
/bin/browser-probe --width=800 --height=600 $pages/$page.html /tmp/$page-probe-cpu.ppm > /tmp/$page-probe-cpu.txt;
/bin/browser --render-gpu --width=800 --height=600 --output=/tmp/$page-main-gpu.ppm $pages/$page.html 2>/dev/null;
/bin/browser-probe --gpu --width=800 --height=600 $pages/$page.html /tmp/$page-probe-gpu.ppm > /tmp/$page-probe-gpu.txt;
cmp /tmp/$page-main-cpu.ppm /tmp/$page-probe-cpu.ppm && echo cpu-same; cmp /tmp/$page-main-gpu.ppm /tmp/$page-probe-gpu.ppm && echo gpu-same")
	case $result in *cpu-same*) echo "probe: $page cpu same ok" ;; *) echo "probe: $page cpu differs: $result"; status=1 ;; esac
	case $result in *gpu-same*) echo "probe: $page gpu same ok" ;; *) echo "probe: $page gpu differs: $result"; status=1 ;; esac
done
guest "cat /tmp/script-probe-gpu.txt"
lines=$(guest "/bin/browser-probe --tab=1 --width=800 --height=600 $pages/keys.html /tmp/probe-keys.ppm")
echo "$lines"
case $lines in *"focusin three"*) echo "probe: Tab ok" ;; *) echo "probe: no focusin after Tab"; status=1 ;; esac
python3 plan/tools/guest/guest.py get /tmp/probe-keys.ppm "$out/probe-keys.ppm" >/dev/null 2>&1
python3 plan/tools/guest/guest.py get /tmp/first-probe-gpu.ppm "$out/first-probe-gpu.ppm" >/dev/null 2>&1
python3 -c "
from PIL import Image
import sys
for name in ('probe-keys', 'first-probe-gpu'):
    Image.open(sys.argv[1] + '/' + name + '.ppm').save(sys.argv[1] + '/' + name + '.png')
" "$out" || status=1

# 3. The script mode through the library.
script=$(guest "printf 'print(\"from \" + (1 + 2));\n' > /tmp/p057.js; /bin/browser --js /tmp/p057.js; echo status=\$?")
case $script in *"from 3"*"status=0"*) echo "script: --js ok" ;; *) echo "script: --js said $script"; status=1 ;; esac

echo "browser-p057: status $status (pictures in $out)"
exit $status
