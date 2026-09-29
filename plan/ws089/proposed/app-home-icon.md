# 案: App Home の Settings の絵（ws089-p001、未適用）

所有: `userland/desktop/wayland/icons.c`（App Home の picture）は WS035。`plan/ws035/demo/apps.conf` の 1 行の追記は依頼で許された範囲。

- `apps.conf` に追記（p002 で行う）:
  `Settings|/bin/settings|settings preferences control panel system network wifi display sound wallpaper about|6b7a8f|settings`
- `icons.c` の picture の表に `settings`（歯車: 8 つの歯の輪と中の丸、白の線、既存の絵と同じ太さ）を足す。約 40 行。
  無い間は tile が頭文字「S」を出す（既存の動き）ので、足すのはデモの見た目のため。p006 で main の許可の後に行う。
- 画像の build（`plan/ws035/demo/build-demo-image.sh`）の userland の一覧に `settings` を足すのも WS035 の所有（main に依頼）。
