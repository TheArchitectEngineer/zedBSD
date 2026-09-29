<!-- awesome-plan project=zedbsd record=ws100p004 -->

# ws100-p004: zdesktop の system bar の音量（icon・popup・wheel・確かめの音・保存）

Phase ID: `ws100-p004`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「p004（zdesktop の volume.c、A1〜A6）。seat.c の axis の hook は 1 つ（main の許可）、IME の key の経路と file には触れない」）

## 変更（`userland/desktop/wayland/`）

- `volume.c`（新、network.c と同じ形）: libkeiland の `keiland_audio_*` を通す。
  - icon（`zwl_volume_draw_icon`）: network の icon の左。speaker と波 0〜3（0%・1〜33・34〜66・67〜100）、mute は ×、音なし（audiod に届かない・device が無い）は
    薄い speaker に横線。popup が開いている間は薄い青の背景。
  - popup（`zwl_volume_draw_popup`）: 幅 260 の glass。「Sound」と百分率（mute なら「Muted」）、slider（track・塗り・knob）、「Mute」の switch。音なしは
    操作を薄くして押せなくし、「Sound service is not running」か「No sound output」。
  - 入力: icon の左 click で開閉、popup の中で slider の press と drag（release で確定）、mute の行で切り替え、外の click（via=outside）と Esc（via=key）で閉じる。
    wheel は icon か開いた popup の上で 1 notch 5%（下で小さく）。
  - audiod への送り: drag の間は 50 ms に 1 回（最後は必ず）、確かめの音は確定（wheel の notch・drag の終わり・mute を外す）で 1 回、drag の間は 250 ms に
    1 回まで、mute を入れるときは鳴らさない。
  - 保存: 変化が落ち着いて 1 秒後に `sound.volume`・`sound.muted`（`~/.config/keiland/desktop.conf`、`keiland_preferences_set`）。audiod に届いたとき
    （接続ごとに 1 回）と設定の file が変わったときに設定の値を当てる（`zwl_volume_preferences`）。greeter（preferences が無い）では当てず保存しない。
  - log（試験用）: `ZWL VOLUME icon`・`reachable= device=`・`popup open … slider= mute= sound=`・`popup close via=`・`set value= muted= via= final=`・
    `preferences value=`・`saved value=`。
- `icons.h`・`icons.c`: `GLASS_ICON_VOLUME_0`〜`_3`・`_MUTED`（線の図形: speaker と弧、×）。App の icon の番号は `GLASS_ICON_FIRST_APP` で追う。
- `shell.c`: bar の `volume_x = signal_x - 34`（線はその左）、icon と popup の描画、button（network より前、popup が開いていれば全て）、motion、tick、
  「静止」と「press が窓に行くか」の判定に popup の開閉、`zwl_glass_key` の頭で popup の key（Esc）。
- `seat.c`: `zwl_seat_axis` に hook の塊を 1 つ（`zwl_volume_axis`、App Home の後、title bar の前）。IME の key の経路には触れていない。
- `preferences.c`（wayland）: `preferences_apply` の最後に `zwl_volume_preferences`。
- `zwl.h`・`glass.h`: 宣言。`Makefile`: `volume.c`。
- 規約: `style-check.py volume.c shell.c seat.c icons.c preferences.c` 0、`git diff --check` 0。build: desktop の warning 0。

途中の誤り（直した）: `zwl_seat_axis` の値は notch の数（evdev の REL_WHEEL）で、Wayland の 15 単位は client へ送るときの換算。最初は 15 で割り 1 notch が 0 になり
wheel が効かなかった。

## 試験（`plan/ws100/tests/`）

- `config-amd64-volume.mk`・`build-volume-image.sh`: WS099 の基準の image（kei の autologin）に HDA・audiod・`audiod-feedback`。
- `volume-guest.sh`: `zdesktop-guest.sh` と同じ Venus の起動に `intel-hda` + `hda-duplex`（`VOLUME_AUDIO=duplex`・`duplex-off`・`none`）と wav の録音を足す（共有の script は変えない）。
- `volume-p004.sh IMAGE`（guest を自分で起こす）→ **`volume-p004: PASS`**（`plan/ws100/phase004/volume-p004.txt`）:

| 基準 | 確かめ | 結果 |
| --- | --- | --- |
| A1 | icon の描画、audiod に届き device あり | `ZWL VOLUME icon`・`reachable=1 device=1`、`icon.png` |
| A2 | popup、slider を 1/4 から 3/4 へ drag、mute の入切、外の click と Esc で閉じる | audiod 75、muted 1 → 0、`via=outside`・`via=key` |
| A3 | icon の上で wheel を 3 notch 下げ 2 notch 上げる | 75 → 70（-15 +10）、`via=wheel final=1` が 5 行 |
| A4 | wav（0.25 秒ごとの峰） | 確かめの音 8 つ。wheel の 5 つは 5576・5257・4811・5257・5576（音量の順）、mute の間は無音、どれも 1 つの音の峰（6022）を超えない |
| A5 | 設定の file、Log Out → audiod を 100 に → login | `sound.volume=70`、新しい session が audiod を 70 に戻す（`ZWL VOLUME preferences value=70`） |
| A6 | HDA 無しの guest、audiod を止める | `reachable=1 device=0` で「No sound output」、`reachable=0` で「Sound service is not running」、zdesktop は動き続け `ZWL ERROR` 0 |

画面: `build/ws100-shots/p004/`（`icon.png`・`popup.png`・`slider.png`・`muted.png`・`wheel.png`・`no-device-icon.png`・`no-device-popup.png`・
`no-audiod-popup.png`、拡大 `zoom-*.png`）、wav `p004.wav`、log `session-first.log`・`session-volume.log`。

## 回帰

WS099 の `criteria.sh build/ws100-volume.img build/ws100-p004-regress C7 C8 C9`（system bar の配置が変わったので）: C7（壁紙 6 枚の 72 点、最小 4.68）、C8（p134）、C9 の 10 本（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner）が全て PASS。

## Resume point

2026-09-30: cleared。A7（5330 の実機）は p006、規約と全体は p007。p005（Settings）はユーザーの判断（既定は入れない）。
