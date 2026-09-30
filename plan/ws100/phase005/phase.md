<!-- awesome-plan project=zedbsd record=ws100p005 -->

# ws100-p005: Settings の Sound の頁で音量を変える（slider と mute）

Phase ID: `ws100-p005`
Parent: [WS100](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `ws100-volume`（branch `wt/ws100`）、main を merge した上。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-30 main の割り当て「ws100-p005: Settings の Sound の頁で音量を変えられるようにする」。ユーザーの判断「入れる」2026-09-30 朝。
Settings は WS089 の source だが WS100 の範囲として main が許可。Settings の今の作り（libkeiland と自前の描画）に合わせる）

## 範囲と受け入れ

- Sound の頁に音量の slider と mute の switch。system bar と同じ設定（`desktop.conf` の `sound.volume`・`sound.muted` と audiod）を共有し、
  どちらで変えても他方に数秒以内に反映される。
- 確かめの音は system bar と同じ規則: 確定（drag を放す、mute を外す）で 1 回、drag の間は 250 ms に 1 回まで、mute を入れるときは鳴らさない。
- 確かめ: QEMU（`volume-guest.sh` の音の device 付き）で頁の操作と system bar との同期、WS089 の回帰（`settings-regress.sh`）、boot test。

## 変更

- `userland/desktop/settings/sound.c`（新）: libkeiland の `keiland_audio_*` で audiod を追う（`se_sound_open`・`_poll`・`_close`・`_wait`）。
  - audiod の報告（system bar の変更を含む）で表示を更新する。drag の間と送り待ちの間は表示を保つ。
  - drag の間は 50 ms に 1 回送り（最後は必ず）、確かめの音の規則は system bar（`userland/desktop/wayland/volume.c`）と同じ。
  - 確定した値を `sound.volume`・`sound.muted` に保存する（常に書く。system bar は値があるときだけ当てるため）。
  - 頁を開いている間は 250 ms ごとに audiod を読む（main loop の待ちを短くする）。log は `ZSETTINGS SOUND open/report/set/feedback`。
- `page-input.c`: Sound の頁を「Volume」の card（「Output volume」と百分率か「Muted」、slider、Quiet・Loud、Mute の switch）と「Output」の card
  （Sound service: Running・Running, no sound output・Not running）と注の文にした。audiod に届かない、または device が無いときは操作を無効にする。
- `settings.h`・`pages.c`・`main.c`・`Makefile`: `struct se_sound`、control の番号（6: slider、7: mute）、頁の press・drag、open・poll・close・待ち時間。
- `plan/ws089/tests/host-build.sh`: host の build に libkeiland の `audio.c` を足した（1 行。settings の全 .c を compile するため）。
- `plan/ws100/tests/volume-p005.sh`（新）: 下の QEMU の試験。

## 試験

### QEMU（`volume-p005.sh`、volume の image（`build-volume-image.sh`）、`VOLUME_AUDIO=duplex`、kei の session、Settings は kei の HOME で）

**PASS**（`build/ws100/final/volume-p005.log`、画面と log は worktree の `build/ws100-shots/p005/`）。

| 確かめ | 結果 |
| --- | --- |
| 頁が audiod に届く | `SOUND report reachable=1 device=1 value=60`（audiod を 60 にしてから起動）、`sound.png` |
| Settings → audiod・desktop.conf・system bar | slider を 30% へ drag → audiod 30、`sound.volume=30 sound.muted=0`、放したときに確かめの音、system bar の popup が 30%（`bar-30.png`） |
| Settings の mute | 入れる: audiod muted、確かめの音なし、bar の icon と popup も Muted（`bar-muted.png`）。外す: unmuted、確かめの音 1 回 |
| system bar → Settings | bar の icon の上で wheel 2 notch → 頁が 20%（`SOUND report … value=20`、5 秒以内、`page-20.png`）、bar の popup の mute → 頁が Muted（`page-muted.png`） |
| wav | 確かめの音 4 つを録音（`p005.wav`、`windows.txt`）。ZWL ERROR 0、Settings は動き続けた |

### 回帰・build・boot（`build/ws100/p005-final.sh`、出力 `build/ws100/p005-final.out`）

- volume の image の build: exit 0、settings・keiland の warning 0。
- host: `plan/ws089/tests/host-build.sh`（settings-render）成功、`plan/ws100/tests/host-audio.sh` 14/14。
  host の Sound の頁の絵 `build/ws100-shots/p005/host-sound.png`（audiod なし: 操作は無効、Not running）。
- WS089 の回帰: lean な Settings の image で `settings-regress.sh` **PASS**（p002〜p009 の 8 本）。
- boot test: `build-ssh-image.sh build/amd64` → `boot-test.sh` **PASS**（`build/ws100/boot-p005/login.png`）。
- 規約: `style-check.py`（sound.c・page-input.c・main.c・settings.h・pages.c）違反 0、`git diff --check` 0。

## 制限・残り

- 保存は 2 つの key を順に書くので、zdesktop が間の file を読むと、一瞬だけ古い mute が当たりうる（次の読み直しで直る。system bar の保存と同じ形）。
- host-render は `se_sound_open` を呼ばないので、host の絵の音量は 0% と出る（host の試験の都合。実物は保存の値か 100%）。
- 実機は未実施（L2 の p006）。
