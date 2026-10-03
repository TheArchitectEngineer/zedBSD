<!-- awesome-plan project=zedbsd record=ws073-p052 -->
# ws073-p052: i386 pcat の vmunix の build を直す（drv_acpi_poweroff の宣言、splash の object）

Status: cleared（q651-i01、P1 generation12、2026-10-04。Q1 判定: build の直しなので build で十分、i386 の起動は未実施）
Disposition: normal
Parent: [WS073](../ws.md)
Queue: q651（Q1 の dispatch 2026-10-04: 「見つけた担当がすぐ直す」方針（ユーザー 2026-10-03）により、BUG-053 の前に P1 が直す。確認は pcat の vmunix の build（warning 0）だけ、QEMU は不要）

## 範囲

ws073-p047 の確認の途中で、i386 pcat の vmunix（`config/ci/config-pcat.mk`）が main 9e228b6 の時点で build できないことを見つけた。

## 原因と修正

1. `src/kern/platform/pcat.c` の `kern_platform_poweroff()`（BUG-119、ws073-p043 で追加）が `drv_acpi_poweroff()` を無条件に呼ぶが、宣言の
   `<drivers/acpi/acpi.h>` は `#if CONFIG_DRIVER_ACPI` の中でだけ include される。pcat の構成は ACPI の driver が無い（`CONFIG_DRIVER_ACPI=0`）ので、
   暗黙の宣言の error（-Werror）。→ ACPI の driver が無い kernel では ENODEV（電源を切る手段が無い）を返す。ACPI の有る amd64 は今まで通り。
2. 1 を直すと link で `drv_pcat_splash_retarget`・`drv_pcat_splash_step`・`drv_pcat_splash_stop` が未定義（`graphics/text.c` が呼ぶ。splash の
   source `graphics/splash.c` が amd64 の list にだけあった）。→ `platform/pcat/vmunix.mk` の `PCAT_GRAPHICS_OBJS` に `splash.o` を加えた。

## 検証

- `make -j16 ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/p1-q651/pcat build/p1-q651/pcat/vmunix` → exit 0、warning 0、`PC/AT vmunix contract: OK`。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-pc98.mk BUILD=build/p1-q651/pc98 build/p1-q651/pc98/vmunix` → exit 0、warning 0（参考）。
- amd64 の既定の構成の vmunix → exit 0、warning 0、`amd64 vmunix check: PASS`。
- style: `style-diff.py src/kern/platform/pcat.c` → 0。
- QEMU: 不要（Q1 の指示）。i386 の起動は未確認。
