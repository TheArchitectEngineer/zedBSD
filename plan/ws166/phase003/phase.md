<!-- awesome-plan project=zedbsd record=ws166-p003 -->

# ws166-p003: 画面キーボードの「候補」タブ

Status: in-progress（2026-10-05 夕、P2、q768。実装済み、QEMU は p004）
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
