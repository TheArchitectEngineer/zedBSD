<!-- awesome-plan project=zedbsd record=ws035p095 -->

# ws035-p095: zdesktop --greeter と --session（グラフィカルログインの g2）

Phase ID: `ws035-p095`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て、ユーザー承認のグラフィカルログイン。設計 [login-manager-design.md](../login-manager-design.md) §9 g2）

## 実装（2026-09-28）

- `userland/desktop/wayland/greeter.c`（新）: ログインの画面。ぼかした wallpaper（少し暗く）、上に時刻（SIZE_ICON）と日付、
  中央にすりガラスの card（選んだ user の円の avatar と頭文字、名前、user が 2 人以上なら行の一覧、password の欄（文字は点、
  空なら「Password」）、青い Log In、下の行に「Wrong password. Try again.」・「Checking...」）、右下に Restart・Shut Down。
  user は passwd の uid 1000〜59999 で shell が nologin・false でない人（GECOS の最初の欄を表示）、居なければ root。
  key: 文字（US 配列、Shift・Caps Lock）、Backspace、Esc（消す）、Enter（ログイン）、Up・Down・Tab（user）。
  pointer: 行・Log In・電源の click、hover で明るく。`AUTH name password` を送ったら password の buffer と行を消す。
  答えは非同期に読み（tick）、OK で zdesktop を終える（sessiond が session を起こす）。fd が閉じたら終わる。
- `main.c`: `--greeter`（`--auth-fd=N` が要る、glass を強制、socket を開かない、期限無し）、`--session`（期限無し）、
  `zwl_request_stop()`。Vulkan の compose が無い greeter は失敗（sessiond の fallback へ）。
- `display.c`: `--width`・`--height` が無ければ出力の大きさを display の preferred mode から（Venus で 1280x800）。
  既存の試験はどれも大きさを渡す。
- `shell.c`・`seat.c`: greeter の間は描画・button・motion・key・tick を greeter.c へ。
- `home.c`: `--session` のとき App Home の最後に「Log Out」（`@logout`、zdesktop が自分で終わる）。
- `src/drivers/gpu/gpu.c`（GPU core、WS075 の持ち物。main が了承 2026-09-28）: `gpu_open` は root だけを受けていた
  （ABI version 1）。root か、device node の持ち主（euid == inode の uid）を受ける。devfs の既定の持ち主は root なので、
  sessiond が無ければ振る舞いは変わらない（root だけ）。拒否は `gpu: open refused: uid N is neither root nor the
  device's owner` を log に。file の mode の検査（VFS の open）はこの前にある。**複数の user の安全は将来の revoke の WS に
  依る**（前の持ち主の開いた fd は chown の後も使える）。
- `greeter-probe --open-as=UID PATH`（root から user になって開く、GPU の受け入れの試験）。

## 検証（amd64、Venus の guest、login image、2026-09-28）

- `plan/ws035/tests/zdesktop-p095.sh` PASS:
  - GPU の受け入れ: alice の 0600 で alice は開ける、bob は mode で EACCES、0666 でも bob は GPU が EACCES（log に理由）、
    root の持ち物に戻すと alice も EACCES（従来どおり）。
  - `sessiond --graphical` → greeter（_greeter、1280x800、users=2）、誤りの password で「Wrong password」、点の表示、
    正しい password（空白を含む）で session が alice の `/bin/wayland`（`/run/user/1001/wayland-0`）、App Home の Log Out で
    session が終わり greeter に戻る。
- 回帰: zdesktop-p093（terminal の選択・copy・drag）PASS。p094 の sessiond の試験は同じ image で PASS（前の Phase）。
- 規約: greeter.c・greeter-probe の style-check 0、変えた zdesktop の file と gpu.c は増えていない。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p095-20260928-greeter.png`、`-wrong.png`、`-typing.png`、
  `-session.png`、`-home.png`（Log Out の icon）、`-again.png`。
- 実機（i915）: 未実施。

## 残り

- 継ぎ目の無い引き継ぎ（g4、lease の fd の受け渡し）、画面の lock（g5）、user の avatar の画像、IME、他の配列。
- Home の外（system bar など）の Log Out・電源の menu。
