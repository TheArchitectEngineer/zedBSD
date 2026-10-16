<!-- awesome-plan project=zedbsd record=ws245-p050d -->

# ws245-p050d: 口 xd.h と own の file の付け替え、旧 file と旧 host 試験の削除

Status: planned（2026-10-16 Q1 の判断で p050 を p050a〜d に分けた。design の順のまま p039 の前に行う）
Parent: [WS245](../ws.md)
Queue: q1229-i06（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.3、§4.2

## 範囲

own の present.c・head.c・output.c・control.c・backlight.c・interrupts.c・display.c・device.c の呼び先を xd.h に（design §4.2）。旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c・takeover.c の残り・takeover-internal.h と internal.h の Linux の型）の中身を空にして vmunix.mk から外す（file の削除は Q1）。旧 host 試験（host-dp-test・host-lcd-modeset-test）を消し、その oracle の check を xd の試験へ移す。

## 受け入れ

- host 試験（run-xd-host-tests.sh、新しい check は xd の試験へ）、build（既定と I915_TESTS=y の 5 組）warning 0、段の前後の比べ（旧 host 試験が残る間は refcmp.sh）、R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。
