<!-- awesome-plan project=zedbsd record=ws142-p008b -->

# ws142-p008b: 中央の窓の下の層を 1 枚の blur にまとめて描く

Status: in-progress（2026-10-06 q781-i01 P2: 実装・build まで。QEMU は p010 で T1 に依頼。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q781 / q781-i01（Q1 の分け方、2026-10-06: p008 から分けた）
Design: [ws142-p007](../phase007/phase.md) の「2026-10-06 ユーザーの決定（最大化の時の中央の窓と File Chooser）」

## 範囲と受け入れ

- 中央に描く窓（dock の領域より小さい固定の大きさの dock した窓、DOCKED の間の dock した親の上の dialog・sheet（File Chooser））の下の層（壁紙の地・同じ app の他の窓・dock した親）を 1 枚の blur にまとめ、中央の窓をその上に鮮明に描く。
- 上部の bar はぼかさない。他の app の窓はぼかしの下にも出さない（p008 の hidden がそのまま効く）。
- glass の blur の経路（backdrop.c）を流用する。

## 実装（2026-10-06）

| 所 | 内容 |
| --- | --- |
| `shell.c` | `window_centred_over`（中央の窓か）、`draw_centred_cover`（backdrop に描いた下の層を MODE_GLASS で出力の全面に、0.32 の暗い色を重ねる）。`zwl_glass_draw` の窓の loop で中央の窓の前に `draw_backdrop`（下の窓、hidden は除く）と cover。p008 の固定の大きさの窓の周りの不透明な暗い地は cover に置き換え（暗い地 → ぼかした暗い地）。bar は loop の後に描くので鮮明のまま。backdrop が作れない device は blur した壁紙（glass の既定） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor・Linux の Keiland の build | 成功、warning 0 |
| style-check（shell.c） | 指摘 0 |
| QEMU | 未実施。p010 で T1 に依頼（File Chooser の sheet を DOCKED の Text Editor から出して撮る、固定の大きさの窓） |
