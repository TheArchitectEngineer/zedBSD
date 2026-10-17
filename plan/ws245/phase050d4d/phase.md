<!-- awesome-plan project=zedbsd record=ws245-p050d4d -->

# ws245-p050d4d: 旧 file の削除

Status: planned（2026-10-17 Q1 の判断で p050d4 を 4 段に分けた 4 段目。p050d4c の後）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）
設計: [design.md](../design.md) §6.2 の p050 の行。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- 呼び手 0 になった旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c の eDP の部分・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・vblank.c・takeover.c の残り・takeover-internal.h・internal.h の Linux の型・watermark.c の modeset の半分・color.c）の中身を空にして vmunix.mk から外す（削除は Q1）。
- 残す物（hotplug.c・tc*.c・dp-ext*.c・hdmi.c・clock.c の TC PLL の pool・diagnostics.c・vbt-shell.c・power.c の殻）に xd.h の型を渡す。
- host-lcd-modeset-test・host-lcd-test・host-dp-test・lcd-modeset-ktest・lcd-fake-hw・dp-fake-hw と harness の oldtests.sh・stagecmp.sh の比べを終える（path を Q1 へ）。oracle は xd の host 試験に移っている事。

## 受け入れ

- 付け替えの後の host 試験の全部（run-xd-host-tests.sh）、`I915_XD_STRICT`、build warning 0、試験の組の image が 16 MiB に入る（size）、R11。QEMU の boot-test と 5320・SV7 の節目は T1・ユーザーの一覧。5330 は全部の段の後に 1 回。
