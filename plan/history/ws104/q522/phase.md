<!-- awesome-plan project=zedbsd record=ws104-p008 -->

# ws104-p008: 規約の全文の見直し、境界の確かめの script、回帰

Status: cleared
Disposition: normal
Parent: [WS104](../../../ws104/ws.md)
Queue: q522 / q522-i01
依存: p001〜p007
実行者: Codex Q1 / main（現在の Master の N=0 と今回の自律実行指示に従う）。script・Master Tools・WS104 完了の処理も main

## 目的

WS104 の完了の前の、規約の全文による見直し（Awesome Plan の code を作る WS の必須の Phase）と、OS の境界が今後崩れないようにする確かめの script。

## 手順（`<W>` は `ws104-p008`）

1. **見直し**: WS104 の全ての source の変更を [coding-style.md](../../../coding-style.md) の全文に照らす。対象の差分:
   ```
   git log --format=%H --reverse -- plan/ws104 | head -1          # WS104 の計画の最初の commit（0eb5e118）
   git diff 0eb5e118..HEAD -- userland/ toolchain/ > build/ws104-p008/ws104.diff
   ```
   見る点: 注釈の量と書き方（関数の先頭の注釈、段落ごとの注釈）、名前、include の並び、error の扱い、`(void)` の未使用の引数、C89 の header（host の試験の物）。
   機械の確かめ: `python3 plan/tools/style-check.py <変えた .c・.h>`（使い方は script の先頭）。違反を直す。周りの既存の code と同じ形のため残す物は理由を記録する。
2. **境界の script** `plan/tools/keiland-os-boundary/check.sh` を作る（POSIX の sh、`set -eu`、先頭に使い方の注釈、repo の root で走る）。確かめること:
   - C1: `userland/desktop/libkeiland/` と `userland/desktop/wayland/` の共通の source（`zedbsd/`・`linux/`・`wpa/` の下でない `*.c`・`*.h`）が
     `#include <uapi/`・`#include "userland/base/` を含まない。例外は `wayland/zwl-evdev.h` の `#include <uapi/input.h>` の 1 行だけ。
     ```
     find userland/desktop/libkeiland userland/desktop/wayland \( -path '*/zedbsd' -o -path '*/linux' -o -path '*/wpa' \) -prune -o -name '*.[ch]' -print \
       | xargs grep -n -e '#include <uapi/' -e '#include "userland/base/' | grep -v 'wayland/zwl-evdev.h:.*uapi/input.h'
     ```
   - C2: 共通の source に `ioctl(` が無い（同じ find）。
   - C3: `zwl_buffer_layout` が `wayland/zedbsd/` の外に無い。
   - C4: desktop の install の path の直書きが無い（p007 の手順 3 の grep）。
   - C5: `include/libc/` に desktop の header（`keiland.h`・`keiui.h`・`truetype.h`・`browser.h`・`wayland*`・`xdg-shell*`・`primary-selection*`・`tablet-unstable*`）が無い。
   - 出力: 各項目の `check: C1 PASS` か `check: C1 FAIL <file:line>`、最後に `keiland-os-boundary: PASS` か `keiland-os-boundary: FAIL`（exit 1）。
   - WS105 で `linux/` の source ができたら、同じ規則で Linux の側も見るように直す（WS105 の p011）。
3. script を走らせて PASS。わざと壊した複写で FAIL になることを確かめる（検出の確かめ）:
   ```
   sh plan/tools/keiland-os-boundary/check.sh
   cp userland/desktop/wayland/display.c /tmp/display.c.orig && echo '#include <uapi/gpu.h>' >> userland/desktop/wayland/display.c
   sh plan/tools/keiland-os-boundary/check.sh; echo "exit=$?"      # FAIL と exit=1
   cp /tmp/display.c.orig userland/desktop/wayland/display.c && git diff --quiet userland/desktop/wayland/display.c && echo RESTORED
   ```
4. master の Tools 節に登録する（main）: 「Keiland の OS の境界の確かめ（WS104）: `plan/tools/keiland-os-boundary/check.sh`」。
5. **回帰**（WS104 の全体、[commands.md](../../../tools/keiland-linux/zedbsd-commands.md) のとおり。約 1.5 時間）:
   - §1 build、warning 0。
   - §6 GPU の境界の試験の一式。
   - §5 compositor の基準 C1・C2・C9。
   - §7 `zdesktop-p059.sh`、pen の `plan/ws079/tests/notes-pen.sh`。
   - §8 Settings（`settings-regress.sh`）と音（`host-audio.sh`・`volume-p004.sh`・`volume-p005.sh`）。
   - §4 boot test（PNG をユーザーに見せる）。
6. WS104 を完了の形にする（main。AGENTS.md の「WS が完了したら」）: ws.md を書き直し（結果・制限・移管）、Phase の directory・`patches/`・`edits-compositor.md` を削除し
   （git の履歴に残る）、`commands.md` は WS105 も使うので `plan/tools/keiland-linux/zedbsd-commands.md` に移して master の Tools 節に登録、Master の registry・Past Log を更新する。

## 完了の条件

- 見直しの違反が 0（または理由つきの例外）。check.sh が PASS し、壊した複写で FAIL する。回帰が全て PASS（未実施の物は理由つき）。

## 結果

q522-i01 cleared（2026-10-01T05:44:23.565110+00:00）。

最終 amd64 build exit 0・自前 warning 0、全文規約の変更範囲違反 0（理由つきの保持と tool 誤検出は standards-review.md）。境界 C1〜C5 PASS、故意の uapi include は C1 FAIL / exit 1、同一に復元。GPU V1 54 source、dedicated host 18 case × ordinary / sanitize、decode host 17 case × ordinary / sanitize、forge / fence guest（600 fence、generation 1、600 frame / 64 s）PASS。compositor C1/C2/C9 は 13/13 PASS、glass p059・Notes pen/PDF・Settings host / guest 8 本・host audio 14/14・音量 p004/p005 PASS。必須 PNG は目視し、boot の login PNG をユーザーに提示した。実機・Linux compositor・他 platform は未実施。証拠と各試験の summary は plan/history/ws104/q522/。 WS の受け入れを確認済み、main の step 6（承認 snapshot / terminal Phase / 設計の保存、再利用 commands の移動、WS / Master / Past Log の完了処理）はこの clearance の直後に実行し、WS 受け入れ event に結果を残す。


Implementation: `cd48e74d110a1e504c2b87ac288d41cdc928367c`（WIP）。詳細 log: `build/ws104-p008/`（一時物）。実機・Linux は未実施。GitHub へは未公開。

Execution started UTC: 2026-10-01T04:42:00.826349+00:00。Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。

### Checkpoint 2026-10-01T04:53:10.487845+00:00 / q522

最終 source 6efb4f2b（WIP）を確定。83 C/header の全文 / 変更範囲 review を保存。変更範囲の機械違反 0、既存周辺 68 件・extern C 宣言の誤検出 52 件は分離し、移動した公開 header の宣言・従来注釈の保持と cleanup 順の理由を記録。literal は file ごとに一致、境界 C1〜C5 PASS、意図的 uapi include は C1 FAIL / exit 1、byte 同一に復元。review build exit 0・自前 warning 0。必須最終 suite を GPU → C1/C2/C9 → glass/pen → Settings/audio → volume の順に実行中。p008 は in-progress。

### Checkpoint 2026-10-01T05:00:49.526826+00:00 / q522

最終実装 6efb4f2b の amd64 build exit 0、自前 / 外部 warning 0。GPU V1 は 54 source、dedicated host 18 case × ordinary / sanitize、decode host 17 case × ordinary / sanitize、forge guest、fence guest（600 fence、generation 1、600 frame / 64 s）、boot 全て PASS。boot PNG を目視してユーザーに提示した。GPU の log / PNG / tools version と final source manifest は plan/history/ws104/q522 に保存。C1/C2/C9 は実行中、その他必須回帰は未完了、p008 は in-progress。

### Checkpoint 2026-10-01T05:27:20.127703+00:00 / q522

compositor C1/C2/C9 13/13、glass p059、Notes pen/eraser/undo/hover/PDF（qpdf OK・ZWL ERROR 0）、Settings host、audio host 14/14 PASS。p072 6 枚・glass 6 枚・Notes 5 枚を目視し、PNG / summary を history/ws104/q522 に保持。Settings guest 8 本を実行中、volume は後続。全文 review の全関数の最終 return 追加点検で handoff_descriptor の Succeeded 注釈が 1 箇所不足、コメントのみの修正を準備し、suite 終了後に ELF 同一性・build / boot と最終規約を確認する。p008 は in-progress、clearance はまだ行わない。
