<!-- awesome-plan project=zedbsd record=ws227-p001 -->
# ws227-p001: menuconfig の表示名、library の Desktop への移動、Linux・FreeBSD の独自の命令

Status: planned（2026-10-14 Q1、q970、P1）
Disposition: normal
Parent: [WS227](../ws.md)
Queue: q970
依存: 無し

## 範囲

[ws.md](../ws.md) の目標 1〜4 の全部。

- 表示名は `ZEDBSD_USERLAND_PACKAGE` の 2 番目の引数（`make list-user-programs` の 2 列目）。Base は目標 1、Desktop は目標 2 の規則で全部の行を揃える。表示の前後の比較の表を phase.md に残す。
- 移動は `git mv`（rm は使わない）。`userland/base/lib{png,gif,jpeg}-compat`・`userland/base/libpdf` → `userland/desktop/`。参照（Makefile の第 9・10 引数の path、`platform/*/vmunix.mk`、`userland/desktop/keiland-{linux,freebsd}.mk`、各 Makefile.linux/freebsd、`plan/tools` と master.md の Tools、docs）を grep で全部追う。menu の group を desktop に。libtruetype の menu も desktop。
- pcat・pc98 の `config/ci/*.mk` は Desktop の app が無いのにこれらを入れている。使う物が無ければ外す（結果を phase.md に）。
- Linux・FreeBSD: zedBSD 独自の base の命令を調べて一覧にする（passkey は既に入っている。他の候補とそれぞれ入れる・入れないの理由）。host にある命令は入れない。入れる物は `KEILAND_LINUX_PROGRAM` などで `bin`・`sbin` に。一覧は Q1 に報告（ユーザーの確認が要る物があれば印を）。

## 確かめ

- `python3 tools/menuconfig.py` の host 試験（`make menuconfig-host-test`）、`make list-user-programs` の前後の差。
- amd64 の build（config-amd64.mk 相当）で warning 0、image の中の library の path と名前が前と同じ（.so の install 先は変えない）。
- `make keiland-linux` と FreeBSD の build（できる範囲、できなければ未実施と書く）。
- QEMU の確かめは不要（path の移動だけ）。要ると判断したら T1 への依頼の文を書く。
