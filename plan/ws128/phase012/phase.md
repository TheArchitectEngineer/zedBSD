<!-- awesome-plan project=zedbsd record=ws128-p012 -->
# ws128-p012: App Home（Apps の menu）の app の icon をデザインした物に差し替える

Parent: [WS128](../ws.md)
Status: planning（2026-10-05 夜 ユーザーの追加。2026-10-06 方向は決定: 角の丸い色の地に白い記号）
Disposition: normal

## 由来

ユーザー（2026-10-05 夜）「AppsのメニューでAppアイコンをちゃんとデザインしたものし差し替えたい」

## 範囲（案）

- App Home に出る全ての標準 app の icon（Files・Settings・Terminal・Text Editor・Notes・PDF Viewer・Image Viewer・Browser・Calendar・Phone・Mailer・Music・Video・System Monitor ほか）を、統一したデザインの icon に差し替える。
- 置き場所と形式は今の icon の仕組み（WS094 の desktop の file の icon）に合わせる。大きさの段（bar・App Home・Wiseview）。
- light と dark（WS089 p017）の両方で読めること。

## ユーザーの決定（2026-10-06）

「アイコンはラウンドでカラーをベースにする意見を反映してほしいです。」→ **角の丸い四角の色の地に白い記号**（ws099-p034 の bar-1 の画像の形）。誰が描くか（エージェントの SVG か画像の生成か）は mock で示して確かめる。

## 未決（ユーザー、以前）

デザインの方向（例: カレンダーで決めた「落ち着いた知性」の glass・3D 寄りか、平らな線画か）、誰が描くか（エージェントが SVG で描いて見せるか、画像の生成か）。

## 案（2026-10-06 P2、ユーザーの確認待ち）

[montage-1.png](images/montage-1.png)（明るい地・暗い地、bar の 26 px・Wiseview の 32 px・App Home の 64 px・大きい 112 px）。

- 形は今の仕組みのまま: 角の丸い色の四角（半径は辺の 0.3）に白い線画（辺の 0.7）。絵は [icons.c](../../../userland/desktop/wayland/icons.c) の
  24 単位の格子の vector の部品（線・円・弧・点・角の丸い箱・穴、新しく右向きの三角）で書き、compositor の rasterizer がその大きさで描く
  （外の icon の集まり・font・画像の生成は使わない）。
- 今まで頭文字だけだった 5 つに絵を足した: Video Player（画面と再生の三角）、Phone（受話器）、Calendar（綴じ輪・月の帯・日の点）、
  Mail（封筒）、System Monitor（台の上の画面と脈の線）。既存の 13 の絵（Files・Notes・Settings・Terminal・PDF・Image・Text Editor・
  Browser・X terminal・Model・Gears・Lock・Log Out）はそのまま。
- 色: Calendar を Files と同じ青（2f7cf6）から赤（e8483f）に変えた（並べた時に Files と見分ける）。他は apps.conf の色のまま。
- 窓の app ID（videoplayer・phone・calendar・mailer・monitor）で bar・Wiseview の mark に絵が出る（`icon_app_ids`）。App Home は
  apps.conf の picture の名前（video・phone・calendar・mail・monitor）。
- atlas: app の絵 18 個 × (40+1) と小さい絵 18 × (14+1) で 1008 px（幅 1024 以内）。

確かめたいこと（ユーザー）: (1) この線画の方向でよいか（立体・gradient にはしない）、(2) Phone の受話器の形、(3) Calendar の赤、
(4) 他に絵を変えたい app。

道具: `plan/ws128/tests/icon-montage.sh [OUT.png]`（host で icons.c をそのまま build した icon-dump と icon-montage.py）。
