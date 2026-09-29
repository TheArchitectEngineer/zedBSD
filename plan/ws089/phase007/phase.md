<!-- awesome-plan project=zedbsd record=ws089-p007 -->

# ws089-p007: desktop の設定の仕組み（libkeiland の preferences と zdesktop の反映）

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（[案](../proposed/desktop-preferences.md)、許可済み D4、[design.md](../design.md) §6.3）

- libkeiland に `keiland_preferences_*`（`~/.config/keiland/desktop.conf`、key 単位の書き込み: flock → 読み直し → その key だけ変える →
  `mkstemp` → `fsync` → `rename`。未知の key と注釈を保つ）。`KEILAND_VERSION` を上げる。
- zdesktop（greeter でないとき）が glass を作る前に読み、以後 1 秒ごとに file の inode・size・`st_mtim` を比べ、変わった key を当てる:
  `wallpaper`・`window.opacity`・`pointer.speed`・`pointer.natural`・`keyboard.repeat.rate`・`.delay`。key が無ければ command line の値に戻る。
- 反映は 1 秒以内（見込み）。壁紙の描き直しの時間を計る。

## 実装

- libkeiland（`KEILAND_VERSION` 12 → **13**。直前に `git merge main` で main の最新が 12 であることを確かめた。WS092 が他に上げていれば merge で main が調整）:
  - `include/libc/keiland.h`: preferences の節（`struct keiland_preferences`、`KEILAND_PREFERENCES_KEY_MAX` 64・`_VALUE_MAX` 256、
    `keiland_preferences_open`・`_close`・`_reload`・`_get`・`_get_int`・`_set`・`_unset`）。
  - 新しい `userland/desktop/libkeiland/preferences.c`（home は `$HOME`、無ければ `getpwuid`。file は最大 64 KiB、読む key は 64 まで。
    key は `[a-z0-9._-]`、value は制御文字なし。同じ key の行が二つあれば最初を使い、書き込みで一つにする）。`exports.map` に
    `keiland_preferences_*`、`Makefile` に preferences.c。
- zdesktop（WS035 の source、許可済み D4 の最小の追加）:
  - 新しい `userland/desktop/wayland/preferences.c`: `zwl_preferences_open`（greeter でないとき、glass より前、main.c）、
    `zwl_preferences_tick`（event loop の毎回、1 秒ごとに reload）、`zwl_preferences_close`。key を当てると
    `ZWL PREFERENCES key=K applied [value=N]` を log。key が消えると command line の `--wallpaper`・`--window-opacity` に戻る。
  - `glass.c`: `wallpaper_create` を image の作成と `wallpaper_fill`（描画）に、`blur_create` を作成と `blur_fill` に分けた。新しい
    `zwl_glass_wallpaper(server, path)` は `vkDeviceWaitIdle` の後に、mapped の同じ image に描き直す（大きさも descriptor も変わらない
    ので作り直さない）。全体を描き直す。log `ZWL GLASS wallpaper path=P ms=N`。
  - `input.c`: 相対の pointer の動きに `pointer_speed`（百分率、100 分の 1 pixel を持ち越す）、`pointer_natural` で wheel を逆に。
    絶対の pointer（tablet・touch）には効かない。
  - `seat.c`: `wl_keyboard.repeat_info` を server の `repeat_rate`・`repeat_delay_ms`（既定 25・400、定数を main.c の初期値へ移した）から。
  - `zwl.h`・`main.c`: 上の server の field、open・tick・close の呼び出し。
- 試験: `plan/ws089/tests/host-preferences.c`・`host-preferences.sh`（host、専用の home `build/ws089-host/prefs-home`）、
  `settings-p007.sh`（guest）。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、-Werror）: `build-settings-image.sh` → exit 0。libkeiland・zdesktop・settings の warning 0
  （log の warning は外部の package（LibreSSL の source）と noct の既存のもの）。
- 規約: `style-check.py` で libkeiland/preferences.c・wayland/preferences.c・glass.c・input.c・seat.c・main.c → 0。`git diff --check` → 0。
- host: `plan/ws089/tests/host-preferences.sh` → **PASS**（欠けた file、set で folder と file ができる、手の注釈と未知の key を保つ、
  二つの writer の交互の set で両方残る、reload が他の writer の変更を見る・変更なしで 0、範囲外の 400 を 300 に、同じ key は一行、
  unset・二度目の unset、不正な key と改行の値は EINVAL、小さい buffer は ERANGE）。
  - 注意（試験の不具合、直した）: 最初の版は相対の HOME を渡し、libkeiland が相対の HOME を無視して `getpwuid` の本当の home に
    書いた（host の `/home/awe/.config/keiland/desktop.conf` を作った。中身は試験の値だけで、消した）。script は絶対 path を渡し、
    試験は相対の HOME を拒む。
- **QEMU（Venus の guest）**: `plan/ws089/tests/settings-p007.sh` → **PASS**。
  1. file なし: `ZWL PREFERENCES open`、手続きの風景（`start.png`）。
  2. `wallpaper=/usr/share/keiland/wallpaper.ppm` を書く → 1〜2 秒で `key=wallpaper applied`、`ZWL GLASS wallpaper ... ms=185`
     （描き直しと blur で 185 ms、その間 compositor の loop は止まる）（`wallpaper.png`）。
  3. `window.opacity=85` → applied value=85（`opacity.png`）。
  4. `pointer.speed=200`・`pointer.natural=1`・`keyboard.repeat.rate=40`・`.delay=250` → それぞれ applied。
  5. file を消す → 風景に戻る（`path=-`）、opacity 100、speed 100（`removed.png`）。
  6. file に壁紙がある状態で zdesktop を起動 → glass の前に当たる（`key=wallpaper applied`、`ZWL GLASS wallpaper` の行なし、起動時の
     `STARTUP step=wallpaper ms=495`）（`restart.png`）。
  7. zdesktop の log に ERROR なし。
  画面は `build/ws089-shots/p007/`（`grid.png` は 4 枚を並べたもの、目で確かめた）。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS（`build/ws089-boot-test/login.png`）。
- 未実施: pointer の速さを実際の相対の mouse（QEMU の `usb-mouse`）で動かして確かめること、client が受ける repeat_info の値の確認、
  frame の間隔の最大の計測（壁紙の差し替えの 185 ms の間は止まる）。これらは Settings の頁で操作する p004・p005 で行う。実機は未実施。

## 残り

- Settings 側（Appearance・Wallpaper・Mouse・Keyboard の頁が `keiland_preferences_set` を呼ぶ）は p004・p005。
- 1 秒ごとの `stat` の負荷と frame の間隔の最大は未計測（p004）。
