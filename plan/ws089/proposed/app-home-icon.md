# 案: App Home の Settings の絵とデモの image（ws089-p001、未適用）

## apps.conf（依頼が許した最小の追記、p002 で行う）

`plan/ws035/demo/apps.conf`（デモの image が `/etc/keiland/apps.conf` として入れる）に:
`Settings|/bin/settings|settings preferences control panel system network wifi display sound wallpaper about|6b7a8f|settings`

## 歯車の絵（WS035 の所有、p006 で main の許可の後）

`userland/desktop/wayland/icons.h` の enum、`icons.c` の picture の名前の表と `icon_app_ids`（無いと App Home の tile だけでなく titlebar・
system bar・Wiseview の app の印も出ない、ws035-p124）、atlas の容量の確認。約 60〜80 行。無い間は tile が頭文字「S」を出す（既存の動き）。
i915 の atlas の新しい行の残課題（master）に触れうるので、実機で見るまでは危険として扱う。

## デモの image（WS075 の所有、main に依頼、D10）

- `plan/ws075/demo/config-demo-hdmi.mk` の `ZEDBSD_USER_PROGRAMS` に `settings` を足す（無ければ App Home は entry を出さない）。
- Sound をデモで働かせるなら `audiod` も足し、rc で起動することを確かめる（今の明示の一覧に audiod は無い）。
- 同梱の壁紙を足すなら `plan/ws075/demo/build-demo-image.sh` の `--file`（`/usr/share/keiland/wallpapers/*.ppm`）。
