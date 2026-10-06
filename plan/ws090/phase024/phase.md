# ws090-p024: libkeiland に複数選択の list と icon の grid の部品を足し、Files の list・grid の view を置き換える

Status: in-progress（2026-10-06 q820、P1。実装・host の確認まで済み、T1 待ち）
WS: [WS090](../ws.md)

ws090-p023 で Files の list・grid の view が libkeiland の部品に移せなかった（libkeiland に複数選択の list・icon の grid が無い、P1 の報告）。ユーザーの「置き換える」（2026-10-06）の残り。部品を libkeiland に足し、Files の list・grid（desktop の icon を含むか設計で決める）を置き換える。前後の撮影をユーザーへ。

## 設計（2026-10-06、P1）

- libkeiland に足す物は**描画と配置の部品**（`ui/views.c`、KL_VERSION 53）。選択の状態（どの item が選ばれているか、Ctrl・Shift の click、矢印、rubber band の当たり）と hit の表は Files に残す（ws090-p023 の sidebar・button と同じ分け方: libkeiland が描き、app が自分の hit の表で動作を決める）。Files の選択は item の `selected`・`select.c`・drag と drop・rename と結び付いており、これを library の状態に置き換えるのは別の設計が要る。
- 部品: `kl_list_header`（列の title・sort の向き・pointer の下の明るさ・下の線・列の間の縁）、`kl_list_item`（行の地: keyboard を持つ時の選択は accent、持たない時は静かな色、pointer の下は淡い色。上の字の 2 つの ink を返す）、`kl_list_cell`（列の字、左寄せか右寄せ）、`kl_grid_layout`・`kl_grid_cell`・`kl_grid_icon`（入るだけの列で中央に、cell と icon の場所）、`kl_grid_item`（icon の後ろの選択の地・pointer の下の地・名前の 2 行と選択の accent の pill、最後の baseline を返す）、`kl_band`（rubber band）。状態は `KL_ITEM_SELECTED`・`KL_ITEM_FOCUSED`・`KL_ITEM_HOVER`、列は `KL_COLUMN_SORTED`・`_REVERSED`・`_HOVER`。
- Files の desktop の icon（`ui-desktop.c`、壁紙の上の白い字と影）は形が違うので置き換えない。

## 実装（2026-10-06、P1）

- `userland/desktop/libkeiland/ui/views.c`（新）、`include/keiland/keiland.h`（宣言、KL_VERSION 53、Q1 が merge の順で番号を確定）、`libkeiland/Makefile`・`.linux`・`.freebsd`、`exports.map`（exports.py で再生成、8 関数）。
- Files: `ui-list.c` の header・行の地・列の字を `kl_list_header`・`kl_list_item`・`kl_list_cell` に。`ui-grid.c` の格子の配置・cell・名前・band を `kl_grid_layout`・`kl_grid_cell`・`kl_grid_icon`・`kl_grid_item`・`kl_band` に（`grid_name` を消した）。hit の記録（`fm_ui_hit`）・rename の field・icon・cut の薄さ・detail の行は Files のまま。
- `plan/tools/files/host-build.sh` に `ui/views.c`。

## 確認（host、2026-10-06）

| 確認 | 結果 |
| --- | --- |
| zedBSD の `libkeiland.so`・`files`（`-Werror`）、`make keiland-linux` | exit 0、warning 0 |
| `exports.py --check`、`keiland-os-boundary/check.sh`、`style-check.py`（views.c・ui-list.c・ui-grid.c） | PASS、PASS、指摘 0 |
| 前後の描画（files-render、変更前は `git archive HEAD` の tree で build、同じ home・同じ操作）: grid、grid の選択 2 つ（Ctrl+click）と pointer の下、focus の無い時の選択、rubber band の途中（4 つ選択）、list の選択と pointer の下 | 全て**画素が同じ**（cmp）。PNG: `build/review/ws090-p024/new-grid-sel.png`・`new-grid-unfocused.png`・`new-band.png`・`new-list.png` |

未実施: QEMU（T1: Files の grid・list の表示、選択、rubber band、列の sort と幅の drag の回帰）。

## 残り

- 選択の状態と操作（Ctrl・Shift の click、矢印での選択、rubber band の当たり）を libkeiland の状態の部品にするのは別の設計（今は Files の `select.c`・`ui-input.c`）。
