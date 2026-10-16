<!-- awesome-plan project=zedbsd record=ws245-p050d4 -->

# ws245-p050d4: 口 xd.h と own の file の付け替え、旧 file と旧 host 試験の削除

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 4 段目。p050d3 の後）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6、§4.2、§4.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 6 と「own の file が呼ぶ旧の口」

## 範囲

- own の present.c・head.c・output.c・control.c・backlight.c・interrupts.c・display.c・device.c の呼び先を xd.h に（status・show run・resident run・kernel の binding は xd-display）。readout・setup_outputs・n1 の helper・VGA の client の残りを xd の型に。
- 旧 host 試験（host-dp-test・host-lcd-modeset-test）と ktest（lcd-modeset・edp・edp-sync・lcd-show）の oracle の check を xd の試験へ移し、旧の試験を消す（path は Q1 へ）。
- 旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c の eDP の部分・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c・output-shell.c・takeover.c の残り・takeover-internal.h・internal.h の Linux の型）の中身を空にして vmunix.mk から外す（削除は Q1）。p039〜p052 まで残す物（hotplug.c・tc.c・tc-kern.c・dp-ext*.c・hdmi.c・clock.c の TC PLL の pool・diagnostics.c・vbt-shell.c）は殻に xd.h の型を渡す。

## 受け入れ

- 付け替えの後の host 試験の全部（run-xd-host-tests.sh）、`I915_XD_STRICT`、build 6 組 warning 0、試験の組の image が 16 MiB に入る（size）、R11 の数。5330 は全部の段の後に 1 回、5320・SV7・QEMU の boot-test は節目（T1・ユーザーの一覧）。
