<!-- awesome-plan project=zedbsd record=ws275-p003 -->

# ws275-p003 generic の /dev/graphics の前段（登録・優先度・行の分割）と host 試験

- WS: [ws275](../ws.md)、設計 [design.md](../design.md) §4.1・§4.2b・§7
- Queue: q1312 / q1312-i01（P1）
- Status: cleared（2026-10-19 Q1 の判定: host 試験 PASS、build warning 0。ユーザーの「全部 merge する」で main に merge（50bd10c5f）。QEMU のみ・実機は未実施）

## 受け入れの条件

1. `include/kern/graphics-backend.h` と `src/drivers/generic/graphics-device.c`: backend の登録と優先度、boot の時に 1 度の選択、行の分割（RGB24 1360・INDEX8 4096・MONO1 4096、`_Static_assert`、`packed` 512 byte は `.vfs_bss`）、pattern の原点は request からの相対の (x, row)。
2. host 試験: 選択（偽の backend 2 つ以上）、分割（幅 1025・1366・4097 × RGB24・INDEX8・MONO1、pattern の有無）、画素の詰め方（RGB332・BGRX8888・XRGB8888 を IE の窓の LE の store として）。
3. sparcv9 の file を変えない。build warning 0（sun4u・amd64・pcat・riscv64）。

## 行った事（2026-10-19 P1）

- `graphics-backend.h`: `struct drv_graphics_backend`（関数の表）、`struct drv_graphics_row`（1 行の 1 chunk、`pattern_x`・`pattern_y`）、
  `drv_graphics_backend_register(backend, priority)`（高い優先度が勝ち、同じなら先の物。NULL・欠けた表は EINVAL、選択の後は EBUSY）、
  `drv_graphics_backend_selected()`、優先度 `DRV_GRAPHICS_PRIORITY_FIRMWARE`（0）・`_CARD`（100）、inline の
  `drv_graphics_pattern_bit`・`drv_graphics_rgb332`・`drv_graphics_pixel_word_le`（IE の窓に native の 32 bit で store する値。XRGB8888 は byte を逆に）。
  card の driver の引き取りの手順（§4.2b）は header の comment。
- `graphics-device.c`: pcat の前段と同じ ioctl の意味。`drv_graphics_device_register` で選択を固定し、backend が無ければ cdev を作らない
  （/dev/graphics が無い）。行は chunk に分けて `blit_row`、幅による EINVAL は無い（stride の下限と mode の幅だけ）。
- sun4u の kernel はまだこの file を compile しない（vmunix.mk は p005）。pcat・pc98 は今の前段のまま（F-111）。

## 確認（2026-10-19 P1）

- `plan/ws275/tests/host-graphics-device.sh`（`BUILD` 既定 `build/ws275-p003/host`）: `PASS host-graphics-device`・`PASS host-graphics-device none`。
  18 本の blit（3 形式 × 3 幅 × pattern の有無）が参照と一致、chunk の数と最大（RGB24 1360、他 4096）も一致。
  試験の効き: `pattern_y = 0` に変えると pattern 付きが FAIL する。`pattern_x = 0` は FAIL しない（chunk の境界が 8 の倍数で位相が同じため、設計どおり）。
- sparcv9 の `-fsyntax-only`（`build/llvm/bin/clang --target=sparcv9-unknown-none`、vmunix.mk の CPPFLAGS と `-Wall -Wextra -Werror`）: rc 0。
- `make ZEDBSD_CONFIG=config/ci/config-{pcat,amd64,riscv64-virt}.mk BUILD=build/ws275-p003/<名前> vmunix -j8`: どれも rc 0、warning 0。
  `make ZEDBSD_CONFIG=plan/tools/ports/config-sun4u.mk BUILD=build/ws275-p003/sun4u vmunix bootloader -j8`: rc 0、warning 0。
- 未実施: QEMU・実機（p006 の T1）。T1 への依頼は無い（host 試験で足りる）。

## 付随: pcat の BLIT_PATTERN の y の位相（design-review-3 の m-A）

- pcat の前段は 1 行ずつ height 1 で blit を呼ぶので、`pattern_bit(pattern, x, y)` の y が常に 0 だった。`struct pcat_graphics_image` に
  `pattern_y` を足し、前段が行の番号を入れ、backend が `image->pattern_y + y` で引く（別 commit）。
- 確認: scratch の host harness（pcat-graphics.c と偽の backend）で 5 行の BLIT_PATTERN の `pattern_y` が 0〜4、pcat の vmunix の build rc 0・warning 0。
  QEMU の画面では未確認。pc98 の前段（`src/drivers/platform/pc98/graphics/`）にも同じ形がある可能性（未確認、範囲外）。

## 残り

- p004 が firmware fb の backend をこの表で登録する。
