#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Audits the Mesa reference files WS031 transcribes from. Only the specific
# src/intel files are checked (not the whole checkout). Records SHA-256 and
# confirms each carries an MIT SPDX tag or MIT permission notice.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../mesa-refs" && pwd)
mesa="$root/mesa"
out="$(dirname -- "$0")/../i915-vk-license-audit.md"
files="
src/intel/genxml/gen120.xml
src/intel/genxml/gen110.xml
src/intel/compiler/brw/brw_eu_defines.h
src/intel/compiler/brw/brw_inst.h
src/intel/compiler/brw/brw_eu.h
src/intel/compiler/brw/brw_eu.c
src/intel/isl/isl_format.c
src/intel/isl/isl_surface_state.c
"
{
  echo "# WS031 Mesa reference audit"
  echo
  echo "取得元: https://gitlab.freedesktop.org/mesa/mesa （src/intel は MIT）。"
  echo "転記対象ファイルのみ監査。SPDX MIT または MIT permission notice を確認し SHA-256 を記録。"
  echo
  echo "| file | MIT | sha256 |"
  echo "| --- | --- | --- |"
} > "$out"
fail=0
missing=0
for rel in $files; do
  f="$mesa/$rel"
  if [ ! -f "$f" ]; then
    echo "| $rel | MISSING | - |" >> "$out"
    missing=1
    continue
  fi
  case "$rel" in
  *.xml)
    # genxml register data has no per-file SPDX; it is core Mesa (MIT per
    # docs/license.rst) and the values are Intel PRM hardware facts.
    if grep -qi "core Mesa library is licensed according to the terms of the MIT" "$mesa/docs/license.rst"; then
      mit="MIT(repo,data)"
    else
      mit=NO; fail=1
    fi ;;
  *)
    if head -60 "$f" | grep -qiE "SPDX-License-Identifier:[[:space:]]*MIT|Permission is hereby granted, free of charge"; then
      mit=yes
    else
      mit=NO; fail=1
    fi ;;
  esac
  sha=$(sha256sum "$f" | cut -d" " -f1)
  echo "| $rel | $mit | $sha |" >> "$out"
done
[ "$missing" != 0 ] && { echo "audit: 対象ファイルが未 checkout。clone 完了を待つ。" >&2; exit 2; }
[ "$fail" != 0 ] && { echo "audit: 非 MIT あり。転記中止。" >&2; exit 1; }
echo "audit: 全対象ファイル MIT。記録: $out"
