<!-- awesome-plan project=zedbsd record=ws089-p004 -->

# ws089-p004: Appearance・Wallpaper・Display・Storage の頁

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（[design.md](../design.md) §2・§6.3・§6.5）

- Appearance: 窓の透明度（85〜100%）の slider。desktop に 1 秒以内に反映。accent の色は出さない（D2、「later version」の注記）。
- Wallpaper: 「Kei（既定）」と同梱の壁紙の thumbnail から選ぶと desktop の壁紙が替わる。既定を選ぶと key を消す。
- Display: 読むだけ（mode・拡大 100%・Graphics）。変更は「later version」の注記（D1、ユーザーの方針でスタブ）。
- Storage: 読むだけの file system の使用量（`statvfs`）。

## 実装

- 新しい `userland/desktop/settings/look.c`（backend）: `se_look_open`（`keiland_preferences_open`、home が無いと保存できないことを頁に出す）、
  `se_look_poll`（1 秒ごとの reload、drag 中は自分の値を保つ）、`se_look_set_opacity`（100 は key を消す）、`se_look_set_wallpaper`
  （既定は key を消す）、`se_look_scan`（既定 `/usr/share/keiland/wallpaper.ppm` と `/usr/share/keiland/wallpapers/*.ppm` を名前順、最大 8、
  PPM（P6）を読んで 240x150 の thumbnail に縮める）、`se_look_volumes`（`/`・`/home`・`/usr`・`/var`・`/tmp`・`/boot` を `statvfs`、
  同じ f_fsid は一度）。log `LOOK set key=... value=... error=`。
- 新しい `userland/desktop/settings/page-look.c`: 4 つの頁の描画、picture の click（`se_look_press`）、slider の drag（`se_look_drag`、離した
  ときに保存）。
- `widgets.c`: slider（`se_slider_draw`・`se_slider_fraction`）。`settings.h`: `struct se_look`・`se_wallpaper`・`se_volume`、頁の `drag`
  hook（`SE_DRAG_START`・`MOVE`・`END`）。`ui.c`: control の上で押すと drag が始まり、動きと離し（どこで離しても）を頁に渡す（検索中は渡さない）。
- `pages.c`: 4 頁を ready に。`search.c`: 設定の表に Window opacity・Desktop picture・Resolution・Scale・Disk usage。`page-home.c`: tile の状態
  （Opaque windows / Window opacity N%、壁紙の名前、画面の mode、最初の disk の空き）。`main.c`: look の open・poll・close。
- 試験: `host-build.sh`（libkeiland の preferences.c を足す）、`host-render.c`（HOME を `build/ws089-host/render-home` の絶対 path にする、
  `drag=X,Y,X2`）、`make-wallpapers.py`（試験の壁紙 Dusk・Mist を `build/ws089-wallpapers/` に作る、git に入れない）、
  `build-settings-image.sh`（それを `/usr/share/keiland/wallpapers/` に入れる）、`settings-p004.sh`（guest）。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、-Werror）: `build-settings-image.sh` → exit 0、desktop の warning 0。規約: `style-check.py` settings の全 file → 0。
  `git diff --check` → 0。
- host: `host-build.sh` → 成功。4 頁の描画（`build/ws089-shots/host-p004/grid.png`）、slider の drag（1100 → 500）で `window.opacity=88` が
  試験専用の home の file に書かれた、wallpaper の既定の click で key が消えた。本当の home には何も作っていない（`~/.config/keiland` は無い）。
- **QEMU（Venus の guest）**: `plan/ws089/tests/settings-p004.sh` → **PASS**（2 回目。下の注記）。
  1. Wallpaper: 3 枚（Kei（default）・Dusk・Mist）、Dusk を click → `LOOK set key=wallpaper value=.../Dusk.ppm`、zdesktop が
     `ZWL GLASS wallpaper path=.../Dusk.ppm`（壁紙とすりガラスが替わった、`wallpaper-dusk.png`）、既定を click → key が消え、session の壁紙に戻った。
  2. Appearance: slider を左端へ drag → `window.opacity=85`、zdesktop が applied value=85（`appearance-85.png` で窓が透ける）、右端へ → key を消し 100。
  3. Display（1280 x 800, 75 Hz・100%・llvmpipe）、Storage（`/` 130 MB used of 1.0 GB）、Home の tile の状態（`home.png`）。
  4. zdesktop の log に ERROR なし。画面は `build/ws089-shots/p004/`（`grid.png` は 6 枚、目で確かめた）。
  - 注記: 1 回目（guest の起動の直後）は Settings の窓が出ず FAIL（`window at 0,0`、`/tmp/zdesktop.log` が無い＝zdesktop の起動の段が
    効いていない）。直後に手で zdesktop と settings を起動すると正常（READY、`ZWL PREFERENCES open`）、変更なしの 2 回目は PASS。
    guest の起動の直後に greeter・session の起動と試験の `stop_all`・zdesktop の起動が重なったためと推測（未確認）。p007 の試験の
    guest の起動の直後は PASS した。再現の確認と原因の特定は未実施。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS。
- 未実施: 実機（i915 での透明度 85% の frame の率、D の危険の計測）、1 秒ごとの `stat` の負荷と frame の間隔の最大、デモの image
  への壁紙の追加（WS075 の所有、D8）。

## 残り

- 同梱の壁紙をデモの image に足すのは `plan/ws075/demo/build-demo-image.sh`（WS075、main に依頼）。
- 試験の 1 回目の失敗（guest の起動の直後）の原因は未確認。
