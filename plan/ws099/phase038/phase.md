# ws099-p038: 最大化（dock）の時に compositor が中身の rect を四方に 4 px ずつ空ける

Status: planned（2026-10-06 Q1 が作成）
WS: [WS099](../ws.md)
Related: [ws090-p021](../../ws090/phase021/phase.md)（app の padding を 0）

## 出典

2026-10-06 ユーザー:「最大化したとき、コンポジタがウィンドウのコンテントRectを四方向で4pxずつパディングするようにしてほしいです。バランスを取りたいのでスクショを見せてください。あと、Pixel単位で話していますが、あとでDPIスケーリングを導入したいので、デフォルトDPIでの論理Pixelだと理解してください。」

## 範囲

- DOCKED（最大化）の時、compositor が窓の中身の rect を上下左右に 4 論理 px ずつ内側に置く（app に渡す大きさもその分小さく）。app の padding は 0 のまま（ws090-p021）。
- 値は compositor の定数（論理 px、既定の DPI。後で DPI scaling を入れる時に倍率を掛ける所を 1 か所に）。
- 全画面（fullscreen・game mode）は対象外（端まで）。
- 試験: host か QEMU で最大化した窓（Files・Settings・Text Editor・Terminal）の撮影をユーザーに見せる（`build/review/` に複写して相対 path で）。AAT・guest の試験の窓の大きさの期待（1256x680 など）が変わる物を直す。
- WS131 p021（compositor の一括の改名）の merge の後に着手（衝突を避ける）。
