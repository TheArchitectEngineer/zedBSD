<!-- awesome-plan project=zedbsd record=ws166-p003 -->

# ws166-p003: 画面キーボードの「候補」タブ

Status: cleared（2026-10-05 Q1: T1-196c PASS（QEMU、画面キーボードの予測の候補・確定・学習）、osk-guest の回帰も T1-196 で PASS）。以前: in-progress（2026-10-05 夕、P2、q768。実装済み、QEMU は p004）
Disposition: normal
Parent: [WS166](../ws.md)
Queue: q768（Q1、2026-10-05）
依存: [p002](../phase002/phase.md)（`keiland_ime_status_v1` version 2）

## 範囲

WS102 の flick panel の「候補」タブ（p016 で置いた枠）に、打った仮名の読みから予測した語を出し、tap で読みを置き換えて学習させる。設計は [p001 の改訂](../phase001/phase.md)。

## 実装（`userland/desktop/wayland/keyboard.c`、2026-10-05 夕）

- 状態: `reading`（flick で commit したひらがな、最大 95 bytes）、`reading_input`（その欄）、`predict_serial`、`predictions`・`prediction_readings`（最大 12）、`candidate_active`・`candidate_slot`。
- 読み: `keyboard_send` で commit したひらがな（U+3041〜U+3096・ー）を足す。key で送った文字・ひらがな以外・欄に入らなかった文字で捨てる。flick の Del と tool の Del で最後の 1 文字を消す（空になれば捨てる）。濁点キーは最後の文字を置き換える。tab 以外の tool・履歴の貼り付け・絵文字・panel を閉じる時に捨てる。別の欄なら新しい読み。秘密の欄（purpose password・PIN、hint hidden・sensitive）では持たない。
- 予測: 読みが変わるたびに `zwl_ime_predict`（serial を進める）。答え（`zwl_keyboard_predictions`）は最新の serial だけ取り、log `ZWL OSK predictions serial= reading= count= first=` と各 cell の位置 `ZWL OSK crect slot=`。読みが始まると tools の face を「候補」にする。
- 表示: tabs と keys の間に 3 列 × 4 行の cell（履歴・絵文字の face と同じ領域）、押している cell は青。語が無い時は「かなを入力すると候補が出ます」か「候補はありません」。
- 選択: tap で、欄が読みの欄であること、surrounding text があれば cursor の前が読みで終わることを確かめ、`keyboard_send_commit(word, 読みの bytes)` で置き換え、`zwl_ime_learn(読み, 語)`、読みを捨てる。log `ZWL OSK candidate commit sent= slot= word= reading=`。確かめに外れたら `candidate refused reason=field|surrounding`。
- 「候補」タブは常に選べるようにした（p016 では faint）。
- docs: `docs/architecture/keiland.md` の `keiland_ime_status_manager_v1` の行に予測を足した。

## 確かめ（host）

- build: zedBSD の `bin/wayland`、`make keiland-linux`、warning 0。`plan/ws102/tests/host-keyboard.sh`: PASS。style-check: keyboard.c は 0 件。

## 未実施

- QEMU（p004、T1）: `plan/ws166/tests/osk-predict-guest.sh`（image は `config-amd64-osk-predict.mk`）。
- 実機: 未実施。

## T1-196（2026-10-05 夜）: FAIL と直し

- 結果（T1、証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-196-predict2/`）: `reading=か serial=1 error=0` の直後に `KEI-IME DONE`（IME が接続を失って終わる）、次の `reading=かん serial=2 error=21`（status が無い）。IME は起動し直すが、予測を頼むたびに同じく終わる。回帰の osk-guest は PASS。
- 原因: `keiland_ime_status_manager_v1_get_status`（libwayland）が status の proxy を **version 1** で作っていた。compositor の status は manager の version（2）なので `predict` event を送り、IME の libwayland は version 1 の proxy に version 2 の event が来たので protocol の誤りとして接続を閉じた。
- 直し: `ime-status-protocol.c` の get_status で proxy の version を manager の version（`wl_proxy_get_version`）にする。

## T1-196b（2026-10-05 夜）: FAIL ×2 と直し

- 結果（T1、証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-196b-predict2/`）: 予測は通った（`KEI-IME PREDICT`、`predictions ... reading=かん count=12`、predict-kan.png に 缶 館 感 勘 …）。候補を押した所で `ZWL OSK candidate refused reason=surrounding` → `reading end reason=surrounding reading=か`。commit・LEARN・probe の delete・text が MISSING。
- 原因: ime-probe は surrounding text を enable の時に 1 回だけ送る（空の text、cursor 0）。後は送らず、delete も Backspace の key も自分の text に反映しない。keyboard.c は「欄が text を持つなら cursor の前が読みで終わること」を確かめていたので、enable の時の古い空の text と比べて外れた。tree の中の他の client（libkeiland など）は set_surrounding_text を送らない（text は NULL で、確かめは元々省かれていた）。
- **仕様の判断（P2、Q1 の示唆に沿う）**: surrounding text は、**読みが最後に変わった後の client の commit で送られた時だけ**読みの確かめに使う。送られていない（NULL）か古い時は確かめを省き、読みの bytes を消して語を commit する（読みは画面キーボードが自分で commit した物で、欄が読みの欄であることは別に確かめている）。新しい text が読みと合わなければ従来どおり refused。既知の限界: 読みが変わった直後（client が前の変化への surrounding を返す前）に別の変化があると、古い text を新しいと取って refused になりうる（安全な側、置き換えないだけ）。古い text を信じて誤って消す方向には働かない。
- 直し:
  - `userland/desktop/wayland/ime.h`・`text-input.c`: `struct zwl_text_input` に `text_commit`（surrounding text を最後に設定した commit の番号、enable・disable で 0）。
  - `userland/desktop/wayland/keyboard.c`: `reading_commit`（読みが変わった時の欄の commit の数、`keyboard_reading_predict` で欄が現在の物の時に記録）。`keyboard_candidate_release` は `text_commit > reading_commit` の時だけ確かめる。
  - `plan/ws166/tests/osk-predict-guest.sh`: probe は delete を適用しないので、最後の `PROBE TEXT` が語で**終わる**ことを確かめる（前は text が語ちょうど、と古い期待だった）。
- 確かめ（host）: `make ZEDBSD_CONFIG=plan/ws166/tests/config-amd64-osk-predict.mk BUILD=build/ws140-p002 build/ws140-p002/bin/wayland`、`make keiland-linux KEILAND_LINUX_BUILD=build/p2-keiland-linux` とも warning 0。`plan/ws102/tests/host-keyboard.sh`: PASS。style-check: keyboard.c・ime.h 0 件、text-input.c は既存の 1 件のまま。
- 再試験（T1）: T1-196b と同じ手順（image を作り直す: `plan/tools/guest/test-image.sh plan/ws166/tests/config-amd64-osk-predict.mk BUILD`、`plan/ws095/tests/ime-guest.sh start IMAGE`、`plan/ws166/tests/osk-predict-guest.sh OUTDIR`）。合格: `osk-predict: status 0`、PNG predict-kan・chosen・space。surrounding を新しく送る client での確かめの経路（refused になる場合）は QEMU では試さない（そういう client が tree に無い）。
