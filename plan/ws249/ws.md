<!-- awesome-plan project=zedbsd record=ws249 -->

# WS249: BCM43455（Raspberry Pi 4 の WiFi）の driver の出典の表記と規約の整理

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。p001・p002 cleared。実機（Pi 4）の WiFi の確かめが T の結果待ち）
Primary Milestone: MG005
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」、[coding-style.md](../coding-style.md)、[WS248](../ws248/ws.md)（AX211 での同じ整理、手順の手本）、[WS215](../ws215/ws.md)（Pi 4 の Bluetooth、同じ chip）
Resume point: T による Pi 4（10.0.30.2）の WiFi の確かめ（scan・WPA2 接続・通信）。その結果を Q1 が判定してから WS を完了する。WHD の 2 file の path は不明のまま（ユーザー確認済み、p001 の結果）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「bcm43455も同じガードレールで修正してほしいです。WSを立ててください。」
（「同じガードレール」は WS248 の指示: 「引用は引用部分の直前でブロック単位で行う、ファイル先頭にまとめて引用元を書かない、referenced:形式で引用元を書く、あとはコーディングスタイルの修正が必要ですね。」）

## 対象

`src/drivers/wifi/bcm43455/`（21 file、約 10,400 行）。動作は変えない（comment・宣言・書き方だけ）。firmware の package（`userland/firmware/bcm43455/`、license の項目 `bcm43455-firmware`）は対象外。

作成時の Q1 の観察（2026-10-15）: 8 file の先頭の comment に「Source: OpenBSD bwfm(4) (ISC) …」「Sources: the order of the register operations follows OpenBSD's bwfm(4) …」の形で出典がまとめて書かれ、本文にも OpenBSD・bwfm・Infineon の WHD（Apache-2.0）の名前がある。consolidated の目印は無い。driver の license の項目（OpenBSD の ISC）は release の license の表に無い。

## 完了の条件

- file の先頭の comment に、参考にした source・引用したデータ・出典の一覧が無い。
- 引用した値・layout は、その block の直前の複数行の comment に「Value(s) referenced: <版> <path> (<名前>)」、参考にした手順（register の操作の順など）は「Procedure referenced: <版> <path> (<関数>)」。今の「Source(s): …」の形は改める。
- 引用以外の comment の本文に出典の名前（OpenBSD・bwfm・WHD・Infineon・Linux・brcmfmac など）を書かない。
- code と symbol の名前は出典（bwfm・WHD・brcmfmac）の物を写さない（一致があれば独自の名前に）。
- 開発の管理情報（WS・Phase・Queue・Bug・試験の回の番号、plan/ への参照、日付つきの決定）を comment・log から削除する（2026-10-15 ユーザー「WS249では削除を行いましょう。」、Guardrail「source の中の開発の管理情報」）。
- plan/coding-style.md の全文に合わせる（不要な前方宣言、決まり文句の comment、条件の中の呼び出し、`?:` など。style-check.py と読み比べ）。
- build（warning 0、rpi4 の config）と BCM43455 の host 試験（あれば）が今と同じ結果。preprocess の後の比較で comment 以外の差が意図した物だけ。実機（Pi 4）の WiFi の確かめは T（Pi 4、10.0.30.2）に依頼する。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 出典の表記: file の先頭の出典を除き block ごとの Value(s)/Procedure referenced: へ、本文の出典の名前の除去、出典と一致する symbol 名の付け直し | cleared（2026-10-15、q1205、P2） |
| [p002](phase002/phase.md) | 規約の全文の見直し | cleared（2026-10-15、q1205、P2） |
