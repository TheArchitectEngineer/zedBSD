#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Fetches the Linux i915 reference files at one fixed tag, records their SHA-256 and verifies that
# every file carries an MIT/X11 license notice. Exits non-zero when any file is not MIT.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tag=${1:-v6.19}
base="https://raw.githubusercontent.com/torvalds/linux/$tag/drivers/gpu/drm/i915"
out="$repo/plan/ws029/temp/linux/$tag"
audit="$repo/plan/ws029/i915-license-audit.md"
mkdir -p "$out"
files="i915_reg.h i915_reg_defs.h intel_uncore.c intel_pci_config.h i915_pci.c
gt/intel_gt_regs.h gt/intel_engine_regs.h gt/intel_lrc_reg.h gt/intel_gpu_commands.h gt/intel_gtt.h
gt/intel_lrc.c gt/intel_execlists_submission.c gt/intel_reset.c gt/gen8_ppgtt.c gt/intel_ggtt.c
gt/intel_gt_irq.c gt/intel_engine_cs.c gt/gen8_engine_cs.c gt/gen8_engine_cs.h gt/intel_mocs.c gt/intel_engine_types.h gt/intel_context_types.h
gt/intel_lrc.h gt/intel_engine.h i915_irq.c gt/intel_gtt.c"
extra="include/drm/intel/pciids.h include/drm/intel/i915_drm.h include/uapi/drm/i915_drm.h"

# The tag must exist upstream; a missing tag is a planning error, not something to guess around.
if ! git ls-remote --tags https://github.com/torvalds/linux "refs/tags/$tag" | grep -q "refs/tags/$tag"; then
    echo "tag $tag does not exist upstream" >&2
    exit 2
fi

{
    echo "# Linux i915 参照ファイルのライセンス監査（tag $tag）"
    echo
    echo "取得元 \`https://raw.githubusercontent.com/torvalds/linux/$tag/\`。判定は先頭 40 行の SPDX 行または MIT permission notice の機械判定。MIT 以外が 1 つでもあれば本 script は失敗する。"
    echo
    echo "| path | SHA256 | 判定 | 根拠 |"
    echo "| --- | --- | --- | --- |"
} > "$audit"
status=0
for f in $files $extra; do
    case $f in
        include/*) url="https://raw.githubusercontent.com/torvalds/linux/$tag/$f" ;;
        *) url="$base/$f" ;;
    esac
    mkdir -p "$out/$(dirname "$f")"
    if ! curl -fsSL "$url" -o "$out/$f"; then
        echo "| $f | (fetch failed) | FAIL | HTTP error |" >> "$audit"
        status=1
        continue
    fi
    sha=$(sha256sum "$out/$f" | cut -d' ' -f1)
    head40=$(head -40 "$out/$f")
    if echo "$head40" | grep -q 'SPDX-License-Identifier: MIT'; then
        verdict=MIT; reason="SPDX MIT"
    elif echo "$head40" | grep -q 'Permission is hereby granted, free of charge'; then
        verdict=MIT; reason="MIT/X11 permission notice"
    else
        verdict=OTHER; reason=$(echo "$head40" | grep -m1 -i 'SPDX\|license\|GPL' || echo "no notice found")
        status=1
    fi
    echo "| $f | \`$sha\` | $verdict | $reason |" >> "$audit"
done
{
    echo
    echo "生成: $(date -u +%Y-%m-%dT%H:%M:%SZ)、script plan/ws029/tests/fetch-linux-refs.sh、結果 $([ $status = 0 ] && echo '全ファイル MIT' || echo 'MIT 以外あり: 参照元から除外すること')。"
} >> "$audit"
cat "$audit"
exit $status
