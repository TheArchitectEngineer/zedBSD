<!-- awesome-plan project=zedbsd record=ws256-p011 -->

# ws256-p011 VisionFive 2 の framebuffer

Status: in-progress（(1) の loader の変更の code・host 試験・build を 2026-10-19 に済ませ、T1 の riscv64-uboot の回帰を待つ。(1) の実機の調べと (2) は未着手）

- 親: [WS256](../ws.md)。Queue: q1282 / q1282-i01（P2、(1) の loader の変更だけ）。
- 範囲（この試行）: [design-peripherals.md](../design-peripherals.md) 改訂 4 §3.1 (1) の「loader の小さな変更」: `bootloader/uefi/bootriscv64.c` で、`RISCV_EFI_BOOT_PROTOCOL` が無い時に EFI の DT の `/chosen/boot-hartid` を読み、どちらを使ったかを `RV64 HART …` で console に出す。GOP の今の mode を読むだけで（`SetMode` は呼ばない）console に 1 行で出す。`riscv64-handoff.h` は版 1 のまま、kernel・HAL は変えない。
- 範囲の外（この試行ではやらない）: (1) の実機の調べ（配布の U-Boot を SD に置いて GOP・ExitBootServices の後の画・fixup を見る。p005・#15 の後）、(2) の code（#20 の答えと #29 の HAL の承認の後、p004 の上で handoff を版 2 に）。
- 受け入れ（この試行）: host 試験（DT の blob）PASS、riscv64 の loader の build が warning 0、T1 の riscv64-uboot の回帰 PASS（Q1 が判定）。

## 実装（2026-10-19、P2、code の commit bff6d7950）

- `bootloader/uefi/fdt-hartid.c`・`fdt-hartid.h`（新）: `zbl_fdt_boot_hartid(tree, size, &hart)`。header（magic・totalsize ≤ 与えた byte 数・version ≥ 17・struct block の 4 byte 揃えと範囲・strings block の範囲）を検め、struct block の token を 1 回だけ辿る。root の子の `chosen`（深さ 1）の直下の property（深さ 2）の `boot-hartid` だけを読み、長さ 4 なら u32、8 なら u64、他の長さは `BAD_LENGTH`。答えは `FOUND`・`NO_CHOSEN`・`NO_PROPERTY`・`BAD_LENGTH`・`MALFORMED`。読みは全部 block の中に限り、offset の計算は 64 bit で行う（壊れた tree で block の外を読まない、折り返さない）。property を見つけたら残りは読まない（block の中で止まるので安全）。
  - 出典（Guardrail の「MIT・GPL の出典のデータの引用」、block の直前の複数行の comment）: header の field と token の値は `Values referenced: Devicetree Specification v0.4`（§5.2・§5.4.1）、辿り方は `Procedure referenced:` 同 §5.4.2、`/chosen`・`boot-hartid`・長さ 4/8 は `Values referenced: Linux v6.12 drivers/firmware/efi/libstub/riscv.c (get_boot_hartid_from_fdt)`。Linux の file は 2026-10-19 に kernel.org から取得して読んだ（sha256 a0f3bf67bf17c85cc3271b6dc718f1ca20ee3babdfd5a202c08fcc3303ff6860、P2 の scratchpad、git に入れない）。code・symbol の名前は写していない。
- `bootloader/uefi/bootriscv64.c`:
  - DT の複写（`copy_device_tree`）を最初の段に移した（boot hart の前）。
  - `find_boot_hart(context, &fdt_run, fdt_size, &hart)`: まず protocol（`ask_boot_protocol`、元の `find_boot_hart` の中身）、次に複写した DT の `/chosen` を読む。protocol が id を返せば `RV64 HART protocol 0x…` を出してそれを使い、DT にも id があって値が違えば `RV64 HART chosen 0x…` も出す（記録だけ）。protocol が id を返さない時は DT の id を使って `RV64 HART chosen 0x…`。DT にも無ければ `RV64 HART chosen <理由>`（`no /chosen` など）を出し、今までどおり "Boot hart" の失敗で firmware に戻る。
  - design は「LocateProtocol が失敗した時」に DT へ落ちるとしていたが、**protocol が見つかっても id を返さない時（call が無い・`GetBootHartId` の失敗）も DT に落とす**（Linux v6.12 の stub の `check_platform_features` も protocol の status が失敗なら DT を読む: `Procedure referenced:` を関数の comment に）。
  - `report_graphics`: `EFI_GRAPHICS_OUTPUT_PROTOCOL` を `LocateProtocol` で探し、`Mode` を読むだけで 1 行に出す: `RV64 GOP mode <Mode> of <MaxMode> <H>x<V> format <PixelFormat> scanline <PixelsPerScanLine> base 0x<FrameBufferBase> size 0x<FrameBufferSize>`。無ければ `RV64 GOP none`、`Mode` か `Info` が無ければ `RV64 GOP no mode`。handoff には入れない（(2) (a)）。出典は `Values referenced: UEFI Specification 2.10, section 12.9`。
  - 後の「what the kernel will find」の塊から `RV64 HART ` の行を外した（hart は見つけた時に 1 回出す）。console の helper を `console_hex`（改行無し）・`console_hex64`・`console_decimal` に分けた。
- `platform/riscv64/bootloader.mk`: `RISCV64_EFI_C` に `bootloader/uefi/fdt-hartid.c`。
- 規約: `plan/coding-style.md` の全文に照らして書いた（条件の中で関数を呼ばない、段落ごとの comment、成功の return を最後に）。clang-format は host に無く未実施。`git diff --check` は 0。

## 確かめた事（host、2026-10-19）

| 確認 | command | 結果 |
| --- | --- | --- |
| loader の build（riscv64） | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk BUILD=build/p011-virt -j16 build/p011-virt/uefi/BOOTRISCV64.EFI`（`-Wall -Wextra -Werror`） | exit 0、warning 0（log の "warning" 0 件）。`fdt-hartid.o` を compile・link。`check-bootriscv64.py`: "RISC-V 64 EFI application OK"（relocation は RELATIVE だけ、undefined 無し） |
| `/chosen/boot-hartid` の読み手 | `plan/ws256/tests/loader/run-fdt-hartid.sh build/p011-host` | exit 0、33 check が PASS、FAIL 0（"PASS: 0 failure(s)"） |
| 同じ試験を ASan・UBSan で | `cc -fsanitize=address,undefined -fno-sanitize-recover=all …`（同じ 5 つの blob） | PASS、sanitizer の報告 0 |

- 試験の blob（dtc で作る、`plan/ws256/tests/loader/hartid-*.dts`）: u32（`boot-hartid = <3>`、他の property の後）→ FOUND 3。u64（`/bits/ 64 <0x100000002>`）→ FOUND 0x100000002。property 無し（`/chosen` の子の node にだけある）→ NO_PROPERTY。`/chosen` 無し（`/soc/chosen` にだけある）→ NO_CHOSEN。長さ 2 → BAD_LENGTH。
- 壊れた tree（u32 などの写しを C で壊す）: magic、totalsize が与えた byte 数を超える、version 16、struct block が tree の外へ、end token の前で切れた block（property が無い tree）、strings block の外の名前、知らない token、block の外へ出る property の長さ、開いていない node の終わり、NULL → 全部 MALFORMED で、hart を書かない。end token の前で切れても property が block の中にある u32 の tree は FOUND（読み手は property で止まる）。
- QEMU は起動していない（T1 に依頼する）。実機は未実施。

## T1 への依頼文（Q1 が番号を付けて送る）

> WS256 p011 (1) の riscv64 の UEFI loader の変更（P2 q1282、2026-10-19）。tree は P2 の commit bff6d7950 以降（Q1 の merge の後の main）。
> 1. `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img build/riscv64-virt/uefi/BOOTRISCV64.EFI build/riscv64-virt/disk.img`、`BOOT_MODE=riscv64-uboot BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$' plan/tools/boot-test.sh build/riscv64-virt/disk.img`（T1-722 と同じ）。PASS は画面の login prompt・login・`uname -m` の `riscv64`。loader は DT の複写を最初の段に移し、boot hart を protocol（無ければ `/chosen/boot-hartid`）で得るので、ここで login に届けば protocol の道が働いている。
> 2. **画面の `RV64 HART` の行は、この mode では見えない見込み**: Debian の u-boot-qemu 2025.01 の `qemu-riscv64_smode/u-boot.bin` には ramfb の driver が無く（`strings` に `bochs_video` はあるが `ramfb` は無い）、boot-test.sh の riscv64-uboot は `-device ramfb` だけなので、U-Boot と loader の ConOut は serial にだけ出て、screendump（console 0 = ramfb）には kernel の画しか写らない。serial は drain されて残らず、判定にも使わない。→ 画面に `RV64 HART protocol 0x…`・`RV64 GOP …` が写らなくても FAIL にしない。写った時（U-Boot が画面に出す構成だった時）は PNG を添えて行の文を書く。
> 結果: 各 PNG・PASS/FAIL・image の commit。

- 未解決（Q1 に返す）: 依頼の「画面に RV64 HART の行」は今の riscv64-uboot の mode では確かめられない（上の 2）。確かめるには、boot-test.sh の riscv64-uboot に U-Boot が描ける display（例 `-device bochs-display`）を足して screendump をその console に向けるか（plan/tools は Q1 の物）、gdbstub で loader の `find_boot_hart` の後の `boot_hart` を読むか、の判断が要る。protocol の道が働く事は 1 の login で分かる。`/chosen` の道は QEMU の U-Boot が protocol を持つので通らず、host 試験だけで確かめた（実機の配布の U-Boot で (1) の調べの時に見る）。

## 残り

- T1 の回帰の結果を Q1 が判定（それまで (1) の loader の変更は cleared にしない）。
- (1) の実機の調べ（p005・#15 の後）: 配布の U-Boot で `RV64 HART protocol|chosen`、`RV64 GOP …`、ExitBootServices の後の画、`bootefi <img> <fdt>` の DTB への fixup。
- (2)（#20・#29・p004 の後）。
