<!-- awesome-plan project=zedbsd record=ws157-p003 -->

# ws157-p003: Photos の app（grid・全面表示・slideshow・回転・お気に入り）

Status: uncleared（2026-10-07 q835: ユーザーの要件（~/Pictures/Library・取り込み・db）で置き換え。code は p004・p005 で作り直した）
Disposition: canceled（2026-10-07 ユーザーの要件で最初の既定案を置き換え。置き換え先 [p004](../phase004/phase.md)・[p005](../phase005/phase.md)）
Parent: [WS157](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)

## 範囲（正常系）

`userland/desktop/photos/` の `view.c`・`thumbs.c`・`main.c`・`Makefile`、`platform/amd64/vmunix.mk` の規則、
App Home（`userland/desktop/wayland/apps.conf`・`icons.c`・`icons.h`）、AAT の helper と `tests/scenarios/apps/photos/`。

## 実装（2026-10-07、P2）

- `decode.c`: file を読み JPEG（EXIF の向き）・PNG・GIF の最初の frame を decode（`userland/desktop/picture`）、中央の正方形の縮小画像、窓に合わせた縮小（長辺 2048）、1/4 回転。
- `thumbs.c`: 画像を作る thread（pthread）。全面の写真の job を先に、縮小画像は後。同じ job を 2 度入れない。frame ごとに見えなくなった縮小画像の job を捨てる。
- `view.c`: 左に Timeline・Favorites・album（最初の写真）、右に月ごとの見出しと grid（約 150 px の正方形、4 列〜）。click で選ぶ、double click・tap・Enter で全面。
  全面: Back・名前と日付・Slideshow・Rotate Left・Rotate Right・Favorite、左右の丸い button、「N of M」。key: ←→（前後）、Esc（戻る）、Space（slideshow 3 秒）、F（お気に入り）、R・Shift+R（回転）、F5（読み直し）。
  縮小画像は 400 枚まで（最も前に描いた物から捨てる）。glass では 2 枚の card、全面は不透明。
- `main.c`: window・menu（File: Refresh・Quit、Photo: Favorite・Rotate Left・Rotate Right・Slideshow・Back to Photos）、`photos [FILE]`、印の保存、`PHOTOS` の log。
- build: `Makefile`（package `photos`）、`platform/amd64/vmunix.mk`。App Home の「Photos」（`apps.conf`、`icons.c` の重なった 2 枚の写真、橙）。
- AAT: `aatlib.py`（APPS・PROGRAMS）、`helpers_apps.py`（open-from-home）、`helpers_photos.py`、`tests/scenarios/apps/photos/{open-from-home,browse}.md`、`plan/ws157/tests/config-amd64-photos.mk`。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws157/tests/run-host-photos.sh` → PASS 26（本物の thread で縮小画像・全面の画像を作る: 壊れた file の縮小画像は EINVAL で灰色、8 枚、4 列、
  Favorites・album・Timeline の切り替え、1 click は選ぶだけ・double click で全面、EXIF の向きの JPEG が 640x480、→、R（450x800 に回る・縮小画像も）、F・save の印、
  Shift+R、Esc で戻り選んだまま、↓・Enter で GIF、Favorites の 1 枚、slideshow の開始・進み・端から最初へ・停止、壊れた file の全面、glass、空の library）。
  PNG: `build/review/ws157/host-photos-*.png`、App Home の tile: `build/review/ws157/app-icons.png`。
- zedBSD: `make -j16 ZEDBSD_CONFIG=plan/ws157/tests/config-amd64-photos.mk BUILD=build/ws157-zed build/ws157-zed/bin/photos build/ws157-zed/bin/wayland` warning 0。
- style-check 0（photos の全部の C、試験の C）。`check-scenarios.py` PASS。
- QEMU: 未実施（T1 に依頼: `--only 'apps\.photos\.'`）。
