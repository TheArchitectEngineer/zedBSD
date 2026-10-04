<!-- awesome-plan project=zedbsd record=ws113-p013 -->

# ws113-p013: 内蔵の panel の明るさ（kernel の backlight の device・i915・backend）

Parent: [WS113](../ws.md)
Status: planned（2026-10-05 q702 の計画で足した。新しい UAPI は Q1 の許可が要る: C2）
Disposition: normal
Primary Milestone: MG006（WS から継承）
Queue: none
目安: 2〜3h

## 目的

2026-10-04 のユーザーの要望「SettingsのDisplayには、内蔵LCDの場合、明るさ調節がほしい」の下層: kernel の backlight の device と i915 の provider、libkeiland-backend の口（[契約の確定](../phase001/contracts-beta2.md) D-BRIGHT・D-BRIGHT-BOOT）。Settings の slider は p006、compositor の経路と Fn の key は p005。

## 範囲

- `include/uapi/backlight.h`（新規）: FreeBSD の backlight(9) と同じ形（`struct backlight_props`（brightness 0〜100、nlevels、levels[]）、`struct backlight_info`（name・type）、ioctl BACKLIGHTGETSTATUS・BACKLIGHTUPDATESTATUS・BACKLIGHTGETINFO）。kernel の `src/kern/` に backlight の class（provider の登録、`/dev/backlight/backlightN` の cdev）。
- i915: eDP の panel がある時に provider を登録（`panel-backlight.c` の `drv_i915_lcd_modeset_brightness` を 0〜100 で呼ぶ。panel が止まっている間は EBUSY）。起動の時の明るさは変えない。
- libkeiland-backend: `keiland-backend.h` に `kl_backend_backlight_open/get/set/close`、`libkeiland-backend-zedbsd/backlight-zedbsd.c`。Linux・FreeBSD は ENOTSUP の stub（p010 で sysfs・backlight(9)）。

## 受け入れ

host の試験（kernel の backlight の class の登録・ioctl の検査、backend の stub）、実機（5330）で `/dev/backlight/backlight0` の UPDATESTATUS で panel の明るさが変わる（小さい probe、ユーザーの目視）。QEMU（T1）は device が無いことと backend の ENOTSUP。build warning 0（vmunix の kernel include check まで、Linux の backend の build）、規約。

## 依存と衝突

依存: C2 の許可（p002 とは独立に始められる）。p005 がこれを使う。衝突: i915 の panel を触る Phase（WS075・WS084、BUG-159 の電源の調査）。
