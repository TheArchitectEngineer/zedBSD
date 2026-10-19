<!-- awesome-plan project=zedbsd record=ws256-p011 -->

# ws256-p011 VisionFive 2 の framebuffer

Status: in-progress（(1) の loader の変更は T1-728 待ち。(2) の土台（handoff 版 2・予約・console の共通化・VF2 の画面と名前）の code・host 試験・build を 2026-10-19 に P2 q1284 が済ませ、T1 の回帰（virt 2 mode と bochs-display の新 mode の案）と Q1 の判定を待つ。(1) の実機の調べと実機の受け入れ（ユーザーの目視）は未実施）

- 親: [WS256](../ws.md)。Queue: q1282 / q1282-i01（P2、(1) の loader の変更だけ）、q1284 / q1284-i01（P2、(2) の土台: 下の「(2) の土台」）。
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

## (2) の土台（2026-10-19、P2、q1284 / q1284-i01）

- 範囲: [design-peripherals.md](../design-peripherals.md) 改訂 4 §3.1 (2) の (a)〜(e) のうち、描き方 (β)（direct map で描き FLUSH64）。ユーザーの決定（2026-10-19、クリック）: #29「両方を承認」（[Guardrail](../../guardrail.md) の 2026-10-19 の WS256 の行、[proposed/hal-riscv64-framebuffer.md](../proposed/hal-riscv64-framebuffer.md) の形）、#15「画面が出るまで待つ」、#10「SD boot, framebufferで目視」（実機の受け入れはユーザーが framebuffer を目で見る）。
- `include/hal/hal.h` は変えていない（承認の形のまま: 名前 6 つは VF2 の BSP だけが返し、console は riscv64 の共通）。
- code の commit（agent/p2）: HAL と handoff の header c14bfbb8e、loader 7c06d189a、kernel の読み手 0f1c25ab6、host 試験 1de2aa8e1、no-map の RAM の画面を device の窓で 2dee74f67（記録は e57163c79 と同じ commit）。

### 実装

- (a) `bootloader/include/riscv64-handoff.h`: `ZBL6_RISCV64_HANDOFF_VERSION` 2。版 1 の field の後ろに framebuffer の 6 field（phys・size・width・height・pitch（byte）・format（`KERN_BOOT_FRAMEBUFFER_*`））、`reserved_count`・`reserved_flags`（bit 0 `ZBL6_RISCV64_RESERVED_OVERFLOW`）、`reserved[16]`（`struct zbl6_riscv64_reserved_range`）。`ZBL6_RISCV64_HANDOFF_V1_SIZE`（offsetof で 3160）・`_V2_SIZE`（3456）と `_Static_assert` 3 つ（版 1 の終わり 3160・8 の倍数・4096 以下）。
- loader（`bootloader/uefi/bootriscv64.c`、新 `riscv64-reserved.c/.h`）:
  - `report_graphics` が今の mode を読み（`SetMode` は呼ばない）、`take_screen` で検査して handoff に入れる: RGBX・BGRX だけ、幅・高さ ≠ 0、`PixelsPerScanLine ≥ 幅`、pitch = `PixelsPerScanLine × 4`（32 bit に収まる）、`FrameBufferSize ≥ pitch × 高さ`、折り返さない。console に `RV64 GOP handed over` か `RV64 GOP not handed over: <理由>`。
  - `exit_boot_services`: 最初の `GetMemoryMap`（console が使える時）で一覧を作り、`RV64 RESERVED <base> size <size>` を出す。16 を超えたら `RV64 RESERVED too many firmware ranges` を出して `EFI_OUT_OF_RESOURCES`（firmware に戻る）。`ExitBootServices` の各試行の直前に取り直した map から作り直し（firmware を呼ばない）、溢れたら `reserved_flags` の OVERFLOW を立てて続ける。
  - 一覧: 画面の範囲を必ず先に 1 つ、続いて map の `EfiReservedMemoryType`・`EfiRuntimeServicesCode/Data`・`EfiUnusableMemory`・`EfiACPIReclaimMemory`・`EfiACPIMemoryNVS`・`EfiPalCode`・`EfiPersistentMemory`（`Values referenced: UEFI Specification 2.10, section 7.2`）。触れる・重なる範囲は合わせる（新しい範囲が触れる物を全部吸って足す）。descriptor は `DescriptorSize` の間隔で読み、構造体より短い間隔の map は読まない。page 数の byte が溢れる・折り返す範囲は address 空間の上端で切る。
- (b) HAL（新 `src/hal/riscv64/loader.c/.h`、`cmain.c`・`bootinfo.c/.h`・`page.c`・`space.c`）:
  - 版の判定を `version < 1`（`HAL_FATAL` "unknown version"）と `size < ZBL6_RISCV64_HANDOFF_V1_SIZE`（"too short"）に直した（`riscv64_loader_check`）。版 2 の field は `version ≥ 2 && size ≥ V2_SIZE` の時だけ読む（`riscv64_loader_take`）。版 1 の loader の handoff は版 1 として受ける。
  - 画面は値を検査して（format 1・2、幅・高さ ≠ 0、pitch ≥ 幅 × 4 で 4 の倍数、size ≥ pitch × 高さ、折り返さない）TAKEN・REFUSED・NONE。
  - 予約は DT の `info->reserved[]`・`nomap[]` とは別の `firmware_reserved`（`struct riscv64_loader_extras` の `reserved[16]`）に写す。DT の `/memory` のどの bank にも触れない範囲、DT の reserved か nomap の 1 つに丸ごと入る範囲は飛ばし、残りを合わせる。`page.c` の `riscv64_page_init` が DT の予約の後に `reserve_range` する（allocator から外す。(β) なので direct map には残す）。
  - 止める時: `reserved_count > 16`（BAD_COUNT）、折り返す範囲（BAD_RANGE）、OVERFLOW の bit、HAL の 16 個を超える（TOO_MANY、版 2 の配列が 16 なので今は起きない防御）。`cmain` は board の初期化（画面の attach）の後に `HAL_FATAL` するので理由が画面に出る。
  - `RISCV64 LOADER layout=2 reserved=<渡された数> kept=<残した数>` と `RISCV64 LOADER framebuffer <W>x<H> pitch= format= phys= size=`（か `refused`）を boot の log に出す。
  - `space.c`: 新 `riscv64_space_in_direct_map(phys, size)`（RAM で nomap に触れず `RISCV64_DIRECT_LIMIT` の内）。`map_hal_devices` は、画面を direct map が持たない時（RAM の外の PCI の BAR など、または U-Boot が DT で no-map にした RAM）に、satp の切り替えの前に device の窓へ写す（design §3.1 (2)「/memory の外なら device の窓で写す」）。direct map が持つ画面はそのまま。
- (c) console の共通化: `bsp-virt/cons.c`・`cons.h`・`font.c`・`font.h` を `src/hal/riscv64/` へ `git mv`（名前を `riscv64_cons_*`・`riscv64_font8x16`・`RISCV64_CONSOLE_*` に）。新 `fbcons.c/.h`: board が `riscv64_fbcons_attach(&screen)`（kernel の address・幅・高さ・pitch（byte）・format・`flush(start, length)`、virt は NULL）で渡す。検査（address ≠ 0、format 1・2、640x400 以上、pitch ≥ 幅 × 4 で 4 の倍数）、80x25 の grid を中央に（virt の 640x400 は今と同じ原点 0,0）、palette を format ごとに（BGRX はそのまま、RGBX は赤と青を入れ替え）、全面を黒にして各行を flush、`riscv64_cons_redraw` で grid を描き直す。`hal_putc` は SBI の console と `riscv64_cons_putc`（`riscv64_board_putc` は無くした）。virt の `framebuffer.c` は ramfb の設定だけを残し、画面を `riscv64_fbcons_attach` に渡す（XR24 = BGRX、flush 無し。画素の値は前と同じ）。
- (d) cache（(β)）: 描画は grid（文字の shadow）から。cell は描いてから 16 行の 32 byte の span を `flush`、scroll（全 grid の描き直し）は全 cell を描いてから grid の 400 行の 2560 byte の span を各 1 回。VF2 の flush は既存の `flush_range`（ccache の FLUSH64、fence を前と各 line の後）。boot parameter `vf2.fb=nocache` で flush を外す（DC8200 が snoop するかの実機の試し、design の `nocache`。`alias`（(α)）は作っていない: 既定は (β)、(α) は実機で (β) が足りない時）。
- (e) VF2 の BSP（`bsp-visionfive2/board.c`）: `riscv64_board_init` が ccache を探してから `show_screen`。画面があれば名前の 6 つ（64 bit の word、無い時は全部 NULL）を埋め、direct map が持つ RAM なら direct map で flush 付き、持たない RAM（DT の no-map）なら device の窓で flush 付き（PMA で cache されるので）、RAM の外なら device の窓（`RISCV64_DEVICE_LIMIT` 以下）で flush 無し。画面の flush（`flush_screen`）は画面の kernel の address から物理を出して line ごとに FLUSH64（device の窓の address も扱う。`hal_dcache_*` の `flush_range` は今のまま）。boot の log に `RISCV64 FRAMEBUFFER <W>x<H> phys=<p> ram flush|ram-nomap flush|ram nocache|device`（か `unavailable (<err>)`）。`hal_get_arch_handoff` は共通の名前の後に `riscv64_board_handoff(name)`（bsp.h、src/hal の中）へ委ね、virt は NULL（loader が GOP を渡しても返さない）。名前の比較は `riscv64_boot_name_is`、parameter は新 `riscv64_boot_parameter_given`（語の一致）。
- kernel（`src/kern/platform/visionfive2.c`）: `kern_platform_init` で `kern_boot_framebuffer("riscv64.framebuffer", &fb)` を読み、format・pitch・size を検査して `visionfive2: framebuffer <W>x<H> pitch <p> format <f> at <phys> size <size>`（か `no framebuffer (<errno>)`・検査の失敗の理由）を log に出すだけ（描かない）。
- design からの違い（技術の判断、Q1 に報告）: (1) loader は予約を DT の `/memory` で絞らない（loader に `/memory` の読み手を足さず、HAL が bank に触れない範囲を飛ばす）。loader の 16 の上限は RAM の外の範囲も数えるが、`EfiMemoryMappedIO` は数えないので U-Boot・edk2 の普通の map では届かない見込み（未確認）。(2) (α) と `vf2.fb=alias` は作っていない。(3) `riscv64_board_putc` を無くし、`hal_putc` が共通の console を直に呼ぶ。
- 規約: `plan/coding-style.md` の全文に照らして書いた（宣言は関数の頭、条件の中で関数を呼ばない、段落ごとの comment、成功の return を最後に、static の前方宣言）。clang-format は host に無く未実施。`git diff --check` は 0。

### 確かめた事（host、2026-10-19）

| 確認 | command | 結果 |
| --- | --- | --- |
| virt の vmunix | `make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/p011b-virt -j16 vmunix` | exit 0、warning 0（log build/tmp/p011b-virt.log の "warning" 0 件） |
| riscv64 の loader | `make … BUILD=build/p011b-virt -j16 build/p011b-virt/uefi/BOOTRISCV64.EFI` | exit 0、warning 0、`check-bootriscv64.py`: "RISC-V 64 EFI application OK" |
| VF2 の vmunix と image | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p011b-vf2 -j16 build/p011b-vf2/vmunix build/p011b-vf2/root.img build/p011b-vf2/uefi/BOOTRISCV64.EFI build/p011b-vf2/disk.img` | exit 0、warning 0（build/tmp/p011b-vf2-disk.log）。disk.img 114,294,784 byte |
| host 試験 3 つ × 2 | `plan/ws256/tests/framebuffer/run.sh build/p011b-host`（plain と `-fsanitize=address,undefined -fno-sanitize-recover=all`） | exit 0。fbcons 35・loader-take 34・reserved 32 の check が両方で PASS、FAIL 0、sanitizer の報告 0 |

- host 試験の中身: `fbcons-host-test.c`（1024x768・pitch 4160 の偽の画面: 断る 8 通り、clear が見える画素だけを黒くして行の余りを残し各行を 1 回 flush、BGRX・RGBX の cell の glyph と色と中央の位置、cell の外が変わらない事、描いた byte が全部 flush の範囲に入り flush が cell の 16 行 × 32 byte だけ、cursor、grid の外を描かない、grid の flush が 400 行 × 2560 byte、640x400 で flush 無し）。`loader-take-host-test.c`（版 0・版 1 の 3159/3160 byte・版 2 の 3160 byte/3456 byte・後の版、版 1 は後ろに byte があっても版 1、画面の受け・断り 9 通り、DT の予約・nomap・RAM の外・空の範囲を飛ばす、一部だけ重なる範囲と RAM の端をまたぐ範囲は残す、橋渡しで 1 つに合わせる、count 17・折り返し・OVERFLOW・知らない flag）。`reserved-host-test.c`（型 15 通り、U-Boot の形の map（画面・OpenSBI・runtime の code と data が合わさる・他は数えない）、descriptor の間隔 48 byte、切れた最後の descriptor、短い間隔、map 無し、巨大な page 数、橋渡し、16 個で満ち触れる 17 個目は合わさり離れた 17 個目は OVERFLOW、折り返しの切り）。
- 未実施: QEMU（T1 に依頼する、下）、実機（板の接続の後、ユーザーの目視）、amd64 の BOOTX64 の build（共有の file を変えていない: `uefi.h`・`video.c` は不変）。

### QEMU で確かめられる道（調べた事）

- Debian の u-boot-qemu 2025.01 の `qemu-riscv64_smode/u-boot.bin` の strings に `bochs_video`・`stdout=serial,vidconsole`・`boot_targets=nvme0 virtio0 virtio1 scsi0 dhcp`・`nvme_boot=run boot_pci_enum; …`・`boot_pci_enum=pci enum` がある。→ distro boot の最初の `nvme0` が `pci enum` を走らせるので、`-device bochs-display` を付ければ U-Boot が bochs を見つけて vidconsole にし、`bootefi` の時に GOP を出す見込み（**推測**: U-Boot の PCI の video の framebuffer の確保（`Video device '%s' cannot allocate frame buffer memory` の文字列がある）で失敗する可能性があり、走らせないと分からない）。
- GOP が出れば、bochs の BAR（virt の PCI の MMIO の窓、RAM の外）が handoff 版 2 の framebuffer になり、VF2 の vmunix（config-visionfive2.mk、bsp-visionfive2）を QEMU virt で動かすと、loader の GOP の取り込み → handoff 版 2 → HAL の読み取り → VF2 の BSP の名前と attach → 共通の fbcons（device の窓、satp の切り替えの後は `map_hal_devices` で写す）→ kernel の console が bochs の画面に出る。VF2 の vmunix は virt の DT でも動く見込み（ccache が無ければ保守は無し、UART・PLIC・timer は DT と SBI、root は loader の initrd）。**通らない道**: RAM の framebuffer の (β) の FLUSH64（virt に ccache が無い）、DC8200・HDMI、VF2 の DT。
- virt の既定の ramfb の道（riscv64-virt・riscv64-uboot）は変えない。virt の BSP は GOP があっても名前を返さず ramfb に描く。

### boot-test.sh への変更の案（Q1 が入れる）

- 差分: [proposed/boot-test-riscv64-uboot-gop.diff](../proposed/boot-test-riscv64-uboot-gop.diff)（`plan/tools/boot-test.sh` の今の版に `patch -p1 --dry-run` が通る。`bash -n` も通る）。新しい `BOOT_MODE=riscv64-uboot-gop` は riscv64-uboot と同じ（U-Boot・virtio-blk-device・serial・QMP）で、`-device ramfb` の代わりに `-device bochs-display` を付ける（display は 1 つなので screendump の console 0 が bochs）。既定の operand は `build/visionfive2/disk.img`。既存の mode の command は変わらない（`riscv_display=(-device ramfb)` を配列に分けただけ）。

### T1 への依頼文（Q1 が番号を付けて送る）

> WS256 p011 (2) の riscv64 の framebuffer の土台（P2 q1284、2026-10-19）。tree は P2 の commit 2dee74f67 以降（Q1 の merge の後の main）。走り C は Q1 が [proposed/boot-test-riscv64-uboot-gop.diff](../proposed/boot-test-riscv64-uboot-gop.diff) を `plan/tools/boot-test.sh` に入れた後。
> 1. 走り A（virt の直の起動の回帰、console の共通化で画が変わらない事）: `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img`、`BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$' plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: login prompt・login・`uname -m` の `riscv64`（grid は左上から、前と同じ位置）。
> 2. 走り B（virt の U-Boot の道の回帰、loader の handoff 版 2）: 同じ config で `build/riscv64-virt/uefi/BOOTRISCV64.EFI build/riscv64-virt/disk.img` も作り、`BOOT_MODE=riscv64-uboot BOOT_THEN=（走り A と同じ） plan/tools/boot-test.sh build/riscv64-virt/disk.img`（T1-722 と同じ）。合格: 同じ。loader が予約の一覧を作れずに止まると login に届かない（その時は画面の PNG を添えて FAIL）。
> 3. 走り C（GOP の道、新 mode）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk -j16 build/visionfive2/vmunix build/visionfive2/root.img build/visionfive2/uefi/BOOTRISCV64.EFI build/visionfive2/disk.img`、`BOOT_MODE=riscv64-uboot-gop BOOT_PATTERN=$'RISCV64 LOADER framebuffer [0-9]+x[0-9]+\nRISCV64 FRAMEBUFFER [0-9]+x[0-9]+ phys=[0-9a-f]+ device\n^(?:\\S+ )?login: ?' plan/tools/boot-test.sh build/visionfive2/disk.img`（VF2 の kernel は入力が無いので BOOT_THEN は使わない）。合格（Q1 が判定）: bochs の画面の中央の 80x25 の grid に login prompt。2 つの HAL の行は scroll で消える前に写れば keep-1.png・keep-2.png に残る（写らずに "never saw" でも login が写れば記録だけ）。**見込みの外れ**: U-Boot が bochs で GOP を出さないと画面は U-Boot の黒のまま（loader の `RV64 GOP none` は serial だけ）で login は写らない → PNG を添えて「GOP 無し」と書く（FAIL の解析はしない）。U-Boot の画（`RV64 GOP mode …`・`RV64 GOP handed over`・`RV64 RESERVED …` の行）が写った PNG があれば添える。
> 結果: 各 PNG・PASS/FAIL・image の commit。

## 残り

- T1 の回帰の結果を Q1 が判定（それまで (1) の loader の変更と (2) の土台は cleared にしない）。走り C は Q1 が boot-test.sh の案を入れるかの判断の後。
- (1) の実機の調べ（板の接続の後）: 配布の U-Boot で `RV64 HART protocol|chosen`、`RV64 GOP …`、ExitBootServices の後の画、`bootefi <img> <fdt>` の DTB への fixup。
- (2) の実機の受け入れ（ユーザーの目視、#10・#15）: HDMI の画面に grid と login、`vf2.fb=nocache` で flush を外した時の画（DC8200 が snoop するか）、GOP の範囲が allocator に渡らない事（design の試験の cdev は作っていない: `RISCV64 LOADER layout=2 … kept=` と `RISCV64 MEMORY … free=` の行で代わりに見る）。(β) で足りなければ (α)（system port の別名、nomap と `riscv64_range_is_ram` の直し）を足す。
