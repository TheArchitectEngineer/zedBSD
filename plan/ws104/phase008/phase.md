<!-- awesome-plan project=zedbsd record=ws104-p008 -->

# ws104-p008: 規約の全文の見直し、境界の確かめの script、回帰

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）。script を `plan/tools/` に置くのと master の Tools 節への登録は main

## 目的

WS104 の完了の前の、規約の全文による見直し（Awesome Plan の code を作る WS の必須の Phase）と、OS の境界が今後崩れないようにする確かめの script。

## 手順

1. **見直し**: `git diff <WS104 の最初の commit の前>..HEAD -- userland/ toolchain/` の全ての変更を、[coding-style.md](../../coding-style.md) の全文に照らす
   （注釈の量と書き方、関数の先頭の注釈、名前、include の並び、error の扱い）。違反を直す。周りの既存の code と同じ形のため残す物は理由を記録する。
2. **境界の script** `plan/tools/keiland-os-boundary/check.sh` を作る（POSIX の sh、`set -eu`、先頭に使い方の注釈）。確かめること:
   - C1: `userland/desktop/libkeiland/` と `userland/desktop/wayland/` の共通の source（`zedbsd/`・`linux/`・`wpa/` の下でない `*.c`・`*.h`）が
     `<uapi/`・`"userland/base/` を include しない。例外は `wayland/zwl-evdev.h` の 1 行だけ。
   - C2: 共通の source に `ioctl(` が無い（`wayland/` と `libkeiland/`）。
   - C3: `zwl_buffer_layout` が `wayland/zedbsd/` の外に無い。
   - C4: desktop の install の path の直書きが無い（p007 の grep。sessiond と `/bin/sh` を除く）。
   - C5: `include/libc/` に desktop の header（`keiland.h`・`keiui.h`・`truetype.h`・`browser.h`・`wayland*`・`xdg-shell*`・`primary-selection*`・`tablet-unstable*`）が無い。
   - 出力: 各項目の `check: C1 PASS` か `check: C1 FAIL <file:line>`、最後に `keiland-os-boundary: PASS` か `FAIL`。
   - WS105 で `linux/` の source ができたら、同じ規則で Linux の側も見るように直す（WS105 の最後の Phase）。
3. script を走らせて PASS。わざと共通の file に `#include <uapi/gpu.h>` を足した複写で FAIL になることを確かめる（検出できることの確かめ。元に戻す）。
4. master の Tools 節に登録する（main）: 「Keiland の OS の境界の確かめ（WS104）: `plan/tools/keiland-os-boundary/check.sh`」。
5. **回帰**（WS104 の全体）:
   - build（`make -j16 BUILD=build/amd64 disk-image`）、warning 0。
   - `plan/tools/gpu-boundary/` の試験一式（p004 と同じ: v1-check、host の 2 本、forge-guest、fence-guest）。
   - `criteria.sh build/ws099-criteria.img build/ws104/p008-criteria C1 C2 C9`、`zdesktop-p059.sh`（tablet）。
   - `plan/ws089/tests/settings-regress.sh`（Settings）、`plan/ws100/tests/host-audio.sh`。
   - boot test（PNG をユーザーに見せる）。
6. WS104 を完了の形にする（AGENTS.md の「WS が完了したら」）: ws.md を書き直し、Phase の directory を削除し、Master の registry・Past Log を更新する（main）。

## 完了の条件

- 見直しの違反が 0（または理由つきの例外）。check.sh が PASS し、壊した複写で FAIL する。回帰が全て PASS。

## 結果

（実行の後に書く）
