<!-- awesome-plan project=zedbsd record=ws101p011 -->

# ws101-p011: L1 Noct の toolchain の差分と G3 の見本・測り方

Phase ID: `ws101-p011`
Parent: [WS101](../ws.md)
Status: in-progress（2026-09-30。toolchain の差分を用意して main へ報告。main の適用と build を待つ）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。D1・D2・D3・D5 はユーザーが決定）

## 範囲（[design.md](../design.md) §4、main の指示）

1. **toolchain の差分**（WS101 は変えず build もしない。main が当てる）: Noct の patch 0004、`userland/base/noct/Makefile`・`version.mk`・
   `zedbsd.cmake`、構成（demo と CI の amd64）の `ZEDBSD_NOCT_ACCEL := y`。共有の host の Noct（`build/NoctLang`）を作り直さない形。
2. **WS101 の分**: G3 の見本 `mix.nct`（N = 4,000,000、整数）、CPU と GPU の時間の測り方、offload の確認の方法、試験の script。
3. 1 ができたら main に報告（済み）。main が当てて build が通ったら、2 と p014（実機の G3）へ。

## 1. toolchain の差分（済み）

[`plan/ws101/p011-toolchain/`](../p011-toolchain/README.md): `0004-accel-opengles-on-zedbsd.patch`、`toolchain.diff`、`config.diff`、README（形、
当てる手順、当てた後の確認、作った側の確認）。要点:

- target だけの patch の一覧（`ZEDBSD_NOCT_TARGET_PATCHES` = 0001〜0004）と level（`ZEDBSD_NOCT_TARGET_PATCH_LEVEL` = `zedbsd12-t1`）。
  host の stamp・build は `zedbsd12` のまま（使い捨ての worktree で `make -n -p` を見て確かめた）。
- target の tree は同じ release の別の level なら展開し直す（他の worktree が merge 後に手で消さなくてよい）。host の tree は置き換えない。
- `ZEDBSD_NOCT_ACCEL := y` の amd64 だけ `-DNOCT_ENABLE_ACCEL=ON`、stamp に `-accel`、libEGL・libGLESv2 への依存と link。
- 確認: 0001〜0004 が検証済みの tarball に `--fuzz=0` で当たる、差分が `git apply --check` で当たる、使い捨ての worktree で target の tree の
  展開と level の置き換えが動く、accel の source が zedBSD の clang で `-fsyntax-only` を通る。link と実行は未確認（main の build）。

## 2. WS101 の分（未着手）

main の適用の後に行う。
