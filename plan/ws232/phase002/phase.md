<!-- awesome-plan project=zedbsd record=ws232-p002 -->
# ws232-p002: 規約の全文の見直し（menuconfig・rtld の #if）

Status: cleared（2026-10-14 P3）
Disposition: normal
Parent: [WS232](../ws.md)
依存: [p001](../phase001/phase.md)

## 範囲

WS232 が変えた物: `tools/menuconfig.py`（i386・Retro・Desktop を外す部分）、`plan/tools/menuconfig-target-host-test.py`、
`src/rtld/rtld.c` の `tlsdesc_slot` の `#if`（差分は 966a95a0c の親〜7401f3fb8）。

## 結果

- `src/rtld/rtld.c`: 差分は prototype と定義を `#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64)` で囲むだけ（呼び出しは同じ条件の
  ブロックの中）。規約に合わない所は無し。直さず。
- `tools/menuconfig.py`・試験: 既存の書き方（注釈 `#`、docstring、短い関数）に揃っている。`builds_for` の `while True` は
  閉包を取る loop で、1 つの判断に comment が付いている。直す所は無し。
- 書き換えは 0 件（動作の変更も 0）。
- style-check（rtld.c）の指摘は WS232 が変えていない行（既存、例 6117・6129・6206 の call-in-condition）で、所有者の WS の見直しで扱う。

## 確かめ

- `timeout 300 make menuconfig-host-test` → `MAC-T001 menuconfig round-trip: PASS (6 targets, ... i386 PC/AT and PC-9800 with Retro and without the Desktop)`。
- rtld.o の amd64 の compile（`make -n` が出す command から出力先だけ `build/p3-ws232/rtld.o` に変えた、`-Wall -Wextra -Werror`）→ warning 0、rc 0。
- 未実施: QEMU・実機（担当は起動しない）。arm64 の rtld の compile（差分は条件に arm64 を含むだけで amd64 と同じ構造）。
