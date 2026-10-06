<!-- awesome-plan project=zedbsd record=ws170-p001 -->

# ws170-p001: Phone の要件と設計（最初の範囲）

Status: cleared（2026-10-07 P2、設計だけ。正常系の範囲は q831 の規則）
Disposition: normal
Parent: [WS170](../ws.md)
Queue: q831（2026-10-07、P2）
依存: なし

## 範囲

ユーザーの最初の範囲（連絡先からタイムラインの表示まで）と、compositor のメッセージの API の骨格と偽の backend（p004）。本物の SMS・MMS・RCS・通話、モデム、スマホの bridge、VoIP、chat の API の統合は後の Phase。Q1（2026-10-07）: 偽の backend「loopback」を desktop の設定 `phone.backend` で選ぶ形は Q1 の技術判断として了承。

## 1. 保存（p002）

`~/Documents/Phone/`（`~/Documents/` は cloud backed とみなす）。同期の衝突に強いように、1 つの物を 1 つの file にする（同じ file を 2 台で書き換えることを減らす）。

- 連絡先: `contacts/<id>.vcf`（vCard 3.0 の `FN` と `TEL` だけ、UTF-8）。`<id>` は作った時の時刻と通し番号（`c1790000000-1`）。
- タイムライン: `messages/<id>/<日時>-<通し番号>.txt`、1 item 1 file。header の行（`Kind: text|call|photo|file`・`Channel: sms|mms|rcs|line|voip`・`Direction: in|out`・`Date: <UNIX 秒>`・`State: unread|read|sending|sent|delivered|failed|answered|missed|no-answer`・`Detail: …`）、空行、本文（UTF-8）。添付は同じ名前の `.files/` の下（最初の範囲では名前だけ）。
- 状態の変化（送信の sent→delivered、未読→既読）は、その item の file だけを書き直す（`.new` に書いて rename）。
- 読み: 起動の時に全部を読み、連絡先は最後の item の新しい順、item は日時の順。

## 2. app（p003）

- p000 の試験の data は program から外して host の試験へ（Mail と同じ）。
- 連絡先の追加: 一覧の上の「+」で form（名前・番号）。知らない番号から受けた時は、その番号の連絡先を作る。
- 送信: 欄の文字を `Kind: text`・`Direction: out`・`State: sending` で保存し、compositor の API で送る。状態の事象で file の State を書き直す。
- 電話: compositor の API で掛け、結果（answered・no-answer・failed）を通話の item に保存する。
- 受信: compositor の事象で `Direction: in`・`State: unread` を保存、通知（`kl_app_notify`）。連絡先を開くと未読を既読に書き直す。

## 3. compositor の API（p004）

system manager の新しい拡張 `kl_system_phone_v1`（manager の version 16、`get_phone` は opcode 11、capability `0x1000`）。WS169 の mail と同じ作り（同じ uid の client だけ）。

| 向き | 名前 | 引数 | 意味 |
| --- | --- | --- | --- |
| request 0 | destroy | — | |
| request 1 | send | request, channel, to, text | メッセージを送る |
| request 2 | call | request, channel, to | 電話を掛ける |
| event 0 | received | channel, from, text, time | メッセージが届いた（全部の phone object に） |
| event 1 | status | request, state | 送信・通話の状態（`KL_PHONE_SENT`・`_DELIVERED`・`_FAILED`・`_ANSWERED`・`_NO_ANSWER`） |
| event 2 | result | request, applied, saved | 受け付けの答え（backend が無ければ `ENODEV` 相当の NOBACKEND） |

- backend: compositor の中の表（`kwl_phone_backend`: 名前・send・call）。今は「loopback」だけ: send は sent と delivered を返し、同じ番号から「Echo: <本文>」を受信として返す。call は no-answer。desktop の設定 `phone.backend`（INT、0 なし・1 loopback、既定 0、compositor）。0 なら send・call は NOBACKEND。
- log は長さだけ（番号・本文を出さない）。
- libkeiland（KL_VERSION は次の番号）: `KL_SYSTEM_HAS_PHONE`、`KL_SYSTEM_CHANGED_PHONE`、`kl_system_phone_send`・`kl_system_phone_call`・`kl_system_take_phone_event`（16 個の ring、received と status）。
- 本物の backend（モデム・スマホの bridge・VoIP）は OS の物なら libkeiland-backend に置く（Guardrail の配置）。今は作らない。
