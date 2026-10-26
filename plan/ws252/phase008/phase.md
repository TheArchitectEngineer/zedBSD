<!-- awesome-plan project=zedbsd record=ws252-p008 -->

# ws252-p008: 規約の全文の見直し

Status: in-progress（2026-10-27 q1599 P4: 見直しと修正・build・host 試験まで。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）

## 範囲

WS252 が base 684a5243f から足した・変えた C（`src/drivers/gpu/i915/` の product・device・device-info・workarounds・mmio・context・engine・request・submit・tlb・reset・gt-power・verify-workarounds・compiler・render と `intel/` の表）を [coding-style.md](../../coding-style.md) の全文と照らした。動作は変えない（書き方だけ）。

## やった事

- 足した行を機械で拾い（関数の頭の注釈、段落ごとの注釈、行の長さ、K&R の関数の形、`unsigned` の接尾辞）、全部を読んで判定した。
- 直した所（注釈の不足だけ）: `device-info.c` の `i915_info_sseu_gen11`（subslice あたりの EU の段落）、`request.c` の `i915_request_emitter_of`（ICL と Gen12 の枝）と `flush_gen11_render`（2 つの PIPE_CONTROL）、`workarounds.c` の `init_tables`（Gen12 の engine の段落）。
- 直さなかった所（誤検出と判定）: `compile-compute.inc` の前方宣言（`compile.c` で宣言済み、include される断片）、`intel/lrc-offsets.h` のデータ表（既存の表と同じ形、段落の注釈は表の頭にある）。

## 確かめた事（2026-10-27）

- build: 既定（`config/ci/config-amd64.mk`）・`I915_TESTS=y I915_TEST_SET=execution`・`I915_TEST_SET=vke2` の vmunix、どれも rc 0・warning 0。
- `plan/ws252/tests/gen12-golden/run.sh`: Gen12 の 543 行が 684a5243f と同一、Linux 由来の照合 PASS。
- `plan/ws252/tests/render-host/run.sh` PASS、`plan/ws252/tests/eu-gen11/run.sh` PASS、`plan/ws245/tests/run-xd-host-tests.sh` all passed、`git diff --check` 差無し。
- 5330 は流していない（注釈だけの変更で、生成物の挙動は変わらない。直前の 5330 PASS は 557c9de05）。

## 残り

- 無し（p007 の実機は実機が無く未実施のまま）。
