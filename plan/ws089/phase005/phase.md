<!-- awesome-plan project=zedbsd record=ws089-p005 -->

# ws089-p005: Mouse・Keyboard・Sound の頁

Status: cleared（2026-09-29、QEMU の Venus の guest。実機は未実施）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: なし（2026-09-29 main の依頼。サブエージェントが worktree の branch `wt/ws089` で実行）

## 目的と受け入れ（[design.md](../design.md) §6.4・§6.6、main の依頼）

- Mouse: pointer の速さ（25〜300%、相対の pointer だけ）と natural scrolling。desktop に 1 秒以内に反映（p007 の zdesktop）。
- Keyboard: key の repeat の速さ（5〜60 回/秒）と待ち（150〜1000 ms）。新しく bind した keyboard から効く（頁に注記）。
- Sound: HDA の driver がデモに無いので、audiod の有無を表示するだけ（main の依頼）。音量は「later version」。proposed/libkeiland-audio.md は適用しない。
- Touchpad は準備中のまま（D9）。

## 実装

- 新しい `userland/desktop/settings/page-input.c`: Mouse（速さの slider、natural の switch、注記）、Keyboard（rate・delay の slider、注記）、
  Sound（Sound service: Running / Not running、注記）。slider は離したときに、switch は click で保存。既定の値（100・0・25・400）は key を消す。
- `look.c`: preferences から `pointer.speed`・`pointer.natural`・`keyboard.repeat.rate`・`.delay` を読む（範囲は zdesktop と同じ）、
  `se_look_set_number`（既定で key を消す）、`se_look_sound`（`/run/audiod.sock` が socket か。protocol は話さない）。
- `settings.h`（look の値と slider の矩形、宣言）、`pages.c`（3 頁を ready）、`search.c`（Pointer speed・Natural scrolling・Key repeat rate・
  Delay before repeat・Sound service）、`page-home.c`（tile の状態）、`Makefile`。
- 試験: `settings-p005.sh`（guest）と、guest の待ち `settings-wait.sh`（`wait_guest`: guest の SSH が答えるまで最大 2 分、`wait_desktop`: 試験の
  zdesktop の `ZWL READY` まで）。`settings-p004.sh` もこれを使うように直した。
  - p004 の 1 回目の FAIL の原因（確認済み）: `settings-guest.sh start` は guest の SSH が答える前に戻る。起動の直後に guest の SSH を
    続けて試すと `Connection refused` が続いた（greeter はこの lean な image では起動しない）。試験の最初の命令（zdesktop の起動）が
    届かず、`/tmp/zdesktop.log` が無かった。`wait_guest` の後は起動の直後でも PASS（下）。

## 確認（2026-09-29）

- build（worktree の `build/amd64`、-Werror）: `build-settings-image.sh` → exit 0、desktop の warning 0。規約: `style-check.py` settings の
  全 file → 0（`S_ISSOCK` の条件の中の呼び出しは mode の比較に直した）。`git diff --check` → 0。
- host: `host-build.sh` → 成功。Mouse の slider を右へ drag → `pointer.speed=300`、switch → `pointer.natural=1`、Keyboard の rate → 45・delay
  → 160 が試験専用の home（`build/ws089-host/render-home`）の file に書かれた。本当の home には何も作っていない。画面
  `build/ws089-shots/host-p005/grid.png`。
- **QEMU（Venus の guest、起動の直後に開始）**: `plan/ws089/tests/settings-p005.sh` → **PASS**（`guest: up`、`zdesktop: ready` の後）。
  1. Mouse: 速さを右端へ → `LOOK set key=pointer.speed value=300`、zdesktop が applied value=300。switch → natural=1 applied（`mouse.png`）。
     中ほどへ戻す → 100（key を消す）applied、switch を戻す → 0 applied。
  2. Keyboard: rate を右端 → 60 applied、delay を左端 → 150 applied（`keyboard.png`）。
  3. Sound: lean な image に audiod は無い（`/run/audiod.sock` 無し）→ 頁は「Not running」（`sound.png`）。Home（`home.png`）。
  4. zdesktop の log に ERROR なし。画面は `build/ws089-shots/p005/`（目で確かめた）。
- 回帰: 同じ guest で `settings-p004.sh build/ws089-shots/p004-rerun` → PASS。
- 起動の確認: `OUTPUT=build/ws089-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img` → PASS。
- 未実施:
  - pointer の速さの実際の効き目: QEMU の guest の pointer は usb-tablet（絶対）で、速さは相対の mouse にだけ効く。usb-mouse を足すには
    `plan/ws035/tests/zdesktop-guest.sh`（WS035）の変更が要るので行っていない。host の試験も無い（zdesktop の input.c の計算は p007 で
    入れたもの）。
  - client が受ける `wl_keyboard.repeat_info` の値の確認。
  - audiod が動いている image での「Running」の表示（QEMU の HDA・audiod を含む image を作っていない）。
  - 実機。
