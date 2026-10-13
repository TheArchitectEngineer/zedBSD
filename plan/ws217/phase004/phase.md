<!-- awesome-plan project=zedbsd record=ws217-p004 -->
# ws217-p004: protocol・libkeiland・Settings の回転

Status: planned（2026-10-14 Q1、q983、P3）
Disposition: normal
Parent: [WS217](../ws.md)
Queue: q983
依存: p002b・p003（cleared）。p010（scanout の 180°）は待たない: 今の compositor は 4 つの角度を turn pass で回せるので、Settings はそれを使う（p010 が入れば compositor の中で scanout に切り替わるだけ）

## 由来（2026-10-14 ユーザー）

Q1「画面の回転は、ベータ2 では Settings に操作が無く、displays.conf に rotate= の行を手で書く時だけ回ります。release notes で宣伝しますか？」→ ユーザー「ベータ2でSettingsに追加します。」

## 範囲

- [ws.md](../ws.md) の p004 の行と [p001](../phase001/phase.md) の設計（protocol の version 34 の wire、`rotation` は選択の値、`why` の 4 bit、KL_VERSION の次の API）に従う。ただし今日の範囲は Settings で回せるのに要る物に絞る: compositor の `set_rotation`・`rotation`・`rotations`（mask と why）、libkeiland の API、Settings の Display の頁（`page-display.c`）の回転の選択（0°・90°・180°・270°、選べない角度は理由の文）、保存（displays.conf、今の ROTATE と同じ）。`bind_touch`・`listen_touch` は UNSUPPORTED のまま。KL_VERSION は master.md の「次の KL_VERSION」の値を使い、Q1 に知らせる。
- 回転の後の窓の配置・touch・pen は p002b・p003 の経路のまま。
- `plan/coding-style.md` の全文を適用。

## 確かめ

- host 試験（protocol の encode/decode、Settings の頁の選択と理由の文）、amd64 の build warning 0、Linux の Keiland の build（Settings は Linux でも作る）warning 0。
- T1 への依頼の文: QEMU で Settings → Display の回転を 90° にして画面が回る PNG、0° に戻す PNG、保存されて再起動の後も回っていること。
