<!-- awesome-plan project=zedbsd record=ws169-p002 -->

# ws169-p002: compositor のメールの口と許可

Status: in-progress（実装・host の試験・build 済み。QEMU は WS169 の最後にまとめて T1 へ）
Disposition: normal
Parent: [WS169](../ws.md)
Queue: q831（2026-10-06、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-06、P2）

設計は [p001](../phase001/phase.md) §1。

- 線の形: `libkeiland/system/kl-system-protocol.h` に `kl_system_mail_v1`（destroy・arrived・listen、event の mail・result）、manager の version 15、`get_mail`（opcode 10）、capability `0x800`、`KL_SYSTEM_SINCE_MAIL`、設定の接頭辞 `mail.codes.`。`system-protocol.c` に interface の表。
- compositor: `wayland/mail-shell.c`（新規）。listen は設定の表にある名前（`mail.codes.<app>`）だけを 16 行の表に保つ。arrived は設定が今 1 の読み手にだけ mail を送り、その読み手の client・object が無ければ行を空ける。log は文字の長さだけ（code は秘密）。`system.c`（capability・get_mail・dispatch）、`protocol.c`、`kwl.h`、3 つの Makefile。
- 設定: `settings-keys.c` に `mail.codes.browser`（BOOL、既定 0、compositor、desktop.conf に残す）。
- libkeiland（KL_VERSION は次の番号、worktree では 54 と仮置き）: `KL_SYSTEM_HAS_MAIL`（0x1000）、`KL_SYSTEM_CHANGED_MAIL`（0x800）、`struct kl_mail_arrival`・`struct kl_mail_event`、`kl_system_mail_arrived`（文字を UTF-8 の文字の境で 255 byte に切る）・`kl_system_mail_listen`・`kl_system_take_mail_event`（8 個の ring）。`exports.map` を `exports.py` で更新。

## 確かめ

- host: `sh plan/ws169/tests/run-host-mail-shell.sh` → PASS 14（create・listen の拒否と受け入れ・設定 off で送らない・on で mail(from, subject, code)・設定の無い login 画面・読み手の object が無い・壊れた文字列と短い arrived で EPROTO・ring の 8 個と古い物の捨て・code の切り詰め）。ASan・UBSan。
- WS131 の `plan/ws131/tests/host-system.sh` が新しい `kwl_settings_number` の参照で link できなくなったので、`mail-shell.c` を足し、fake の `kwl_settings_number`（ENOENT）と capability の期待に `KL_SYSTEM_HAS_MAIL` を足した → PASS。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws169/tests/config-amd64-mailer.mk BUILD=build/ws169-zed build/ws169-zed/bin/wayland build/ws169-zed/dynamic/libkeiland.so` が warning 0。`plan/tools/keiland-os-boundary/check.sh` PASS（途中で見つけた ws175 の試験の古い header の path は直して別に commit）。`style-check.py mail-shell.c` 0。
- 未実施: QEMU（WS169 の最後に T1）、Linux・FreeBSD の compositor の build。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS169 p002 の行。
