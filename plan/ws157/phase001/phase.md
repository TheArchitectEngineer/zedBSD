<!-- awesome-plan project=zedbsd record=ws157-p001 -->

# ws157-p001: 写真の app の要件（既定案）

Status: planning（既定案で p002・p003 を進める。ユーザーの確認待ち）
Disposition: normal
Parent: [WS157](../ws.md)
Queue: q831（2026-10-07、P2）

## 経緯

ユーザーの指示（2026-10-05）は「要件の検討からスタート」。q831 の P2 の column で、人間の判断が要る所は止めずに Q1 に送り、
既定案で進める（2026-10-07、Q1 に範囲を送った）。下の P1〜P9 は**既定案**で、ユーザーが変えれば p002 以降を直す。

## 既定案（P1〜P9）

| # | 観点 | 既定案 | 範囲外（Future・後の Phase） |
| --- | --- | --- | --- |
| P1 | library | `~/Pictures` を起動時と Refresh（F5・menu）で深さ 4 まで読む。隠し folder は見ない | folder の見張り、複数の folder、取り込み先の指定 |
| P2 | 整理 | 日付（EXIF の DateTimeOriginal、無ければ DateTime、無ければ file の mtime）の新しい順。album = `~/Pictures` の 1 段目の folder。お気に入り | 場所（EXIF の GPS）・地図、人（顔の認識）、album の作成・名前の変更（Files で folder を作る） |
| P3 | 見る | 左に Timeline・Favorites・各 album、右に月ごとの見出しと正方形の縮小画像の grid。double click・tap で 1 枚を全面に、←→ で前後、Esc で戻る、slideshow（3 秒ごと） | 拡大・pan（Image Viewer の役）、動画（WS122 の Video Player） |
| P4 | 編集 | 回転（右・左）だけ。**非破壊**: 元の file は書き換えず、回転とお気に入りを `$XDG_CONFIG_HOME/keiland/photos.conf`（無ければ `~/.config/...`）に持つ | 切り抜き・明るさ等の補正、書き出し |
| P5 | 取り込み | なし（Files で `~/Pictures` に複写する） | camera・SD・USB（WS132 の PnP の通知）からの取り込み |
| P6 | 形式 | JPEG（EXIF の向き）・PNG・GIF（最初の frame）。既存の libjpeg-compat・libpng-compat・libgif-compat（`userland/desktop/picture`） | HEIC（codec の license）・RAW・WebP |
| P7 | 共有 | なし | クラウド（WS146・WS147）、mail への添付 |
| P8 | Image Viewer との分け方 | Photos は library、Image Viewer は 1 file の viewer。Files の画像の関連付けは Image Viewer のまま | Photos から Image Viewer で開く |
| P9 | 模倣 | 一般的な grid と時系列だけ。特定の製品の固有の UI は写さない | — |

縮小画像は memory だけに持つ（正方形 160 px、上限 400 枚、見える物から背景の thread で作る）。disk の cache は後（backlog）。

## Phase の切り方

- [p002](../phase002/phase.md): library（`photos.h`・`library.c`・`exif.c`・`store.c`）と host 試験。
- [p003](../phase003/phase.md): app（`userland/desktop/photos/` の view・縮小画像・window、App Home、AAT の scenario）、T1 に依頼。
