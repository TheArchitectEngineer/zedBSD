<!-- awesome-plan project=zedbsd record=ws273-p003 -->

# ws273-p003 bsp-jz4770 の最小（入口・LED と backlight の印・platform・uImage）

- 親: [ws273](../ws.md)、Queue: q1329 / q1329-i01（P1、2026-10-19）
- Status: in-progress（中断、2026-10-19 ユーザーの指示「RG350はきりのいいところでラップアップしましょう。途中までのコードをコミットしてOKです。」でラップアップ）
- 設計: [design.md](../design.md) 改訂 2.1 の §2・§3.1 の p003。依存 p002（T1-748 の回帰待ち）。

## やった事（build が通る段）

| 項 | 中身 |
| --- | --- |
| bsp | `src/hal/mips/bsp-jz4770/jz4770.h`（GPIO の register・PB30 の LED・PE1 の backlight・待ちの単位、出典の comment）、`early.S`（`mips_board_early_mark`: backlight を点け、間の後に段の数だけ LED を点滅。`mips_board_early_fault`: backlight を消し、長い 1 回＋ExcCode の回数の短い点滅を繰り返す。どちらも stack・global を使わない葉、$8〜$13 だけ）、`board-rg350.c`（argv を読まない、256 MiB、PRId・Config0〜3 を boot で記録し describe で出す、FIR は fpu.c の行。timer・INTC・display は stub: tick は来ない） |
| platform | `platform/mips32-rg350/`（Makefile・vmunix.mk・vmunix.ld・mkuimage.py）。vmunix.mk は malta の vmunix.mk を include し、bsp と board の define だけ差し替える（malta の vmunix.mk を `?=` の既定に変えた、malta の出力は不変）。`Makefile`・`tools/menuconfig.py` に `mips32-rg350` |
| link address | **`0x80600000`**（§2.5 の案）。UBIBoot の版が未確認なので、raw（`VMLINUZ.BAK`、KSEG1 の 0xa0600000 で入る）と uImage（`UZIMAGE.BAK`、load = ep = 0x80600000）の両方で同じ本体が走る方を選んだ |
| `.uncached` | vmunix.ld に `__bss_end` の後・`__kernel_vma_end` の前の NOLOAD の section（今は空、p004 で framebuffer・descriptor） |
| uImage（D5 B） | `mkuimage.py`（zlib.crc32、header の CRC と data の CRC、SOURCE_DATE_EPOCH、`--check`）。`make rg350-images` で `VMLINUZ.BAK`・`UZIMAGE.BAK` |

外部の参照: Linux master `drivers/pinctrl/pinctrl-ingenic.c`（2026-10-19 `gh api`、GPIO の offset と出力の手順）、OpenDingux `gcw0-base.dtsi`（LED・backlight の pin）、UBIBoot `fat.c`・`config.h`・`config-gcw0.h`（LD_ADDR、996 MHz）、U-Boot `include/image.h`（header の値）。値だけを「Values referenced:」で書いた。

## 確認（host のみ、QEMU・実機は未実施）

- build: `make -j16 BUILD=build/ws273-p003/rg350 ZEDBSD_CONFIG=plan/ws273/tests/config-mips32-rg350.mk vmunix rg350-images` rc 0・warning 0。mips32-malta（`plan/tools/ports/config-mips32-malta.mk`）rc 0・warning 0。mips64-malta は未実施（vmunix.mk を変えていない）。
- host 試験: 独立の Python で `UZIMAGE.BAK` の header の CRC・data の CRC が一致、本体は `VMLINUZ.BAK` と同一、先頭の語が `mtc0 $0,$13`（`_start` = 0x80600000）。`.uncached` は `__kernel_vma_end` の前。

## 残り

1. 待ちの単位（75M 回 ≈ 0.15 s は cache から走る見込みの推測）を実機で確かめる。
2. `bsp-jz4770` の SoC の部分（intc・tcu・cgu・lcdc）は p004・p005。
3. mips64-malta の build、規約の見直し（p007）。
4. T1 の malta の回帰（vmunix.mk の `?=` 化。共通の層 `src/hal/mips/*` は触れていない）。

## 再開の手順

1. main に p002・p003 の commit があることを確かめ、上の build を流す。
2. 実機 1（下）をユーザーの試験の一覧に載せる（Q1）。結果で p003 を判定し、p004 へ。

## ユーザーが拾える試験（実機 1: 点滅と backlight、D7）

> RG350（320×240）の microSD の第 1 partition（FAT32）で、既存の `UZIMAGE.BAK`・`VMLINUZ.BAK` を別の名前に退避し、`build/ws273-p003/rg350/UZIMAGE.BAK` を置く。Y（UBIBoot の alt key、PE28）を押しながら電源を入れ、Select は押さない。期待: backlight が点き、電源の LED が間を置いて 1 回・2 回・3 回の点滅（各 0.3 s 程度、cache の状態で数倍遅い事もある）。その後は止まったまま（画面は黒、tick は未実装）。OpenDingux が起動したら X を押して試す。それでも OpenDingux なら `VMLINUZ.BAK`（同じ build の raw）を置いて同様に試す。長い点灯の後に短い点滅が繰り返されたら early exception で、短い回数（ExcCode）を写真か動画で返す。戻すには退避した `.BAK` を戻す。試行の上限は実機 2 回。
