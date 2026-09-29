<!-- awesome-plan project=zedbsd record=ws090-p001 -->

# ws090-p001: 設計

Status: cleared（2026-09-29、subagent の worktree `wt/ws090`、main a85ea4cc から）
Disposition: normal
Parent: [WS090](../ws.md)
Queue: main の依頼（2026-09-29「WS090 の ws090-p001（設計）」）

## 範囲と受け入れ

libkeiland の既存の部品（scroller・gesture・glass・titlebar・menu・file chooser）と WS092 の内部の canvas（`paint.c`・`paint-text.c`）を部品の
library にまとめる設計: 部品の一覧（実際の使われ方から）、API の形と版、既存の app を移す順番、Phase への分け方。source は変えない。
受け入れ: 設計の文書と、誤り・欠落・矛盾の観点での自己の見直し。

## 成果物

[design.md](../design.md)。要点:
- 新しい共有 library `libkeiui`（`<keiui.h>`、`kui_`、`KUI_VERSION` は 1 から）。libkeiland は広げず、scroller・gesture は libkeiland に残して包む（libkeiui → libkeiland の一方向）。
- 即時の描画の部品（描きながら入力を消費し結果を返す）と app が持つ小さな状態（field・scroll・list）。theme は Files の値の 1 つ。
- 窓の土台（`kui_window`）を含め、見せ方を Vulkan（既定）・shm（副の窓）・無し（Terminal・Notes の自分の Vulkan）に分ける。
- scroll view（touch の慣性・rubber band・wheel の glide・key・scroll bar）をユーザーの最低限の要望として先に（p003）。
- file chooser は `kui_file_chooser_*` として libkeiui へ移し、libkeiland から消して KEILAND_VERSION を上げる（p006、13 → 14 の見込み）。
- 移す順: Settings の描画 → Text Editor → Settings の部品 → PDF Viewer・Image Viewer → Files（2 段）→ Terminal・Notes。Phase は p002〜p012。
- 判断の点 J1〜J8 は既定を選んだ（design.md §13）。

## 調べた事実（main a85ea4cc）

- 窓の土台の重なり: `window.c` 約 1200 行 × 6 app、`present.c` 1212〜1218 行 × 4 app（Files と Text Editor の差は接頭辞をそろえて 94 行）と Image Viewer 1699 行、
  `touch.c` 392〜1216 行 × 6 app。canvas は 4 app で全て違う実装（Files 1396 行が上位集合）、text は 4 つ、icon は 3 か所、US の key の表は 5 か所。
- Settings は Files の `canvas.c`・`text.c`・`icons.c` を source のまま compile（F-038）。Settings の部品: header・card・row・toggle・button・field・dot・signal。
- Terminal と Notes は自分の Vulkan で描く（`render.c`）。CPU の canvas を使わない。
- zdesktop は `wl_keyboard.repeat_info` を preference の `keyboard.repeat.rate`・`.delay` から送る（`wayland/seat.c`・`preferences.c`）。
- ws035 の合成の設計（承認済み）: GPU の経路が主、wl_shm は補助。
- WS095 は text-input の helper を libkeiland に作る計画（ws095-p006）。部品の field はその上に IME を結ぶ。
- KEILAND_VERSION は 13（WS089 の preferences）。

## 自己の見直し（design-reviewer の観点: 誤り・欠落・矛盾）

直したもの:
1. 誤り: 「present.c は同じ 1212 行」→ 行数が同じだけで中身は違う（md5）。接頭辞をそろえた差 94 行と書き直した。
2. 欠落: Terminal・Notes は自分の Vulkan で描くので CPU の canvas の窓は合わない → 窓と見せ方を分け、「無し」の見せ方を足した（§2・§5・p011）。
3. 欠落: pointer が動くたびに frame 全体を描き直す恐れ → 前の frame の hit で hover の変化を先に判定し、変わった時だけ描く（§4）。
4. 欠落: 部品の外の入力（本文・画像・頁）の行き先 → 前の frame の部品に当たらない press・down は app の listener へ（§4）。
5. 誤りの恐れ: list の行の id を hash にまとめると衝突しうる → `(id, index)` の組を鍵に（§4）。
6. 大きさ: Files（2.9 万行）を 1 Phase で移すのは大きすぎる → p009・p010 に分けた。
7. 矛盾: p004 で Text Editor を移すのに dialog・chip の部品は p005 → p004 は窓・present・touch・clipboard だけ、dialog・chip・chooser は p006（§10）。
8. 欠落: p002 は Settings の source を書き換えるのに WS089 が作業中 → 依存に「WS089 の合間、main が時期を決める」（§10）。
9. 欠落: 範囲外（倍率・右から左・読み上げ・thread・drag and drop）→ §12 に明記。描画の比較の許す差（channel の差 2）を §11 に。
10. 明記: libkeiland から関数を取り除くのは破壊的で、KEILAND_VERSION の約束では守れない → in-tree の唯一の使い手を同じ Phase で移すので許す（§7）。

残る判断（main・ユーザー）: J1（新しい library か libkeiland を広げるか）、J5（デモの前に移す範囲）。既定で進められる。
