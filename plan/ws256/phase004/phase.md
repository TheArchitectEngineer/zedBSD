<!-- awesome-plan project=zedbsd record=ws256-p004 -->

# ws256-p004 riscv64 の UEFI loader（U-Boot → BOOTRISCV64.EFI → vmunix）

Status: in-progress、test-wait（T1 の番号は Q1。2026-10-19 q1269-i01、P2。実装・build（warning 0）・host 試験は済み。U-Boot `qemu-riscv64_smode` の host への導入（Q1、apt）と T1 の結果を Q1 が判定するまで cleared にしない）

- 親: [WS256](../ws.md)。Queue: q1269 / q1269-i01（承認: 2026-10-19 ユーザー「WS256を実施してください。」）。
- 範囲: [design.md](../design.md) §3.3・§6 の p004。loader の riscv64（PE の header の自作、`.rela.dyn` の自己 relocation、RISCV_EFI_BOOT_PROTOCOL、DTB の複写、ZBL6 の handoff、`satp`=0 で物理の入口へ）、`riscv64-handoff.h`、`bootloader.mk`・`disk-image.mk`、`run.mk` の uboot の行、`elf64.c` の arch の書き分け。
- 受け入れ: checker PASS、build warning 0、T1: QEMU virt で U-Boot → BOOTRISCV64.EFI → vmunix → 画面に login、`uname -m`。
- 依存: p007（実装済み・T1 試験待ち）、p009（cleared）。HAL の API（hal.h）は変えていない。HAL の実装（`src/hal/riscv64/cmain.c`・`bootinfo.[ch]`）に loader の handoff の受け口を足した。

## 作った物・変えた物

- `bootloader/include/riscv64-kernel-image.h`（新）: kernel の置き場所の約束（`RISCV64_KERNEL_LINK_VIRT_BASE` 0xffff_ffff_8000_0000、`_LINK_PHYS_START` 0x8020_0000、`_MAX_BYTES` 16 MiB、`_PHYS_ALIGN` 2 MiB、`_PHYS_LIMIT` 256 GiB（HAL の boot の table の識別 map））。式は `virtual = VIRT_BASE + (linked physical − LINK_PHYS_START)`（design M13）。名前は amd64 の `amd64-kernel-image.h` と同じ形（ユーザーの決定 12）。
- `bootloader/include/riscv64-handoff.h`（新）: `struct zbl6_riscv64_handoff`（magic `ZBL6_HANDOFF_MAGIC`、version 1、size、flags、boot_hart、fdt_phys/size、initrd_phys/size、kernel_phys_start/end、`kern_boot_parameter_record`）。bootloader/ の header で、include するのは loader と HAL だけ（Guardrail「HAL と kernel・boot loader と HAL の受け渡し」）。版の約束（2026-10-19 Q1 の指示、p011 の framebuffer の field のため）: 後の版は前の版の field を同じ所に残して後ろに足し、version と size を上げる。HAL は version ≥ 1・size ≥ 版 1 の大きさを受け、知っている版の field だけを読む。今の版 1 は boot の parameter で終わる。
- `bootloader/uefi/elf64.c`・`elf64.h`: 機械ごとの約束を macro に（`__riscv` なら riscv64、他は amd64）。riscv64 は EM_RISCV、窓の物理 0x8020_0000 を基準に vaddr を検め、segment の物理の page 揃えは求めない（riscv64 の vmunix には 0x78 byte の page に揃わない segment がある。HAL は image を 1 つの窓で map するので image の先頭だけ 2 MiB に揃える）。**amd64 の `elf64.o` の機械語は変更の前後で一致**（`llvm-objdump -d` の差は file 名の行だけ）。
- `bootloader/uefi/include/uefi.h`: `EFIAPI` を x86_64 だけ `ms_abi`、他は空に。`RISCV_EFI_BOOT_PROTOCOL` の型と GUID（ccd15fec-…-40bf）、device tree の configuration table の GUID（b1b621d5-…-aae0）（`Values referenced:` riscv-uefi の boot_protocol.adoc と Linux 6.8.12 の efi.h）。
- `bootloader/uefi/bootriscv64.c`（新）: `efi_main`。boot hart（protocol、無ければ止める）→ LoadedImage の device の SimpleFS を開き BlockIo の第 0 block から FAT の serial（`zbl_uefi_fat_bpb_parse`）→ `\ZEDBSD.CFG` を `zbl_uefi_kern_config_parse`（`boot0=UUID=` の補いは amd64 と同じ）→ kernel の ELF を `zbl_elf64_plan`、2 MiB 揃えの page（link の 0x8020_0000 を AllocateAddress で試し、だめなら AnyPages に 2 MiB 足して切り上げ）、`zbl_elf64_place`、segment の複写と零化 → `\ROOT.IMG`（無ければ ramdisk 無し）を AnyPages に → DTB を AnyPages に複写（2 MiB まで）→ handoff の 1 page → memory map を取り ExitBootServices（古い key は 3 回まで）→ `zbl_riscv64_enter(hart, handoff, 物理の entry)`。失敗は ConOut（U-Boot の UART）に出して firmware に status を返す。compiler が出す `memset`・`memcpy` を自前で持つ。
- `bootloader/uefi/riscv64/start.S`（新）: PE の entry。`lla __image_base`（link の 0）で load の address を出し、`.rela.dyn` の `R_RISCV_RELATIVE` を当て（他の種類なら EFI_LOAD_ERROR を返す）、`efi_main(image, system)` を呼んで status を返す。`Procedure referenced:` Linux の efi-header.S。
- `bootloader/uefi/riscv64/transition.S`（新）: `csrci sstatus, SIE`、`sie`=0、`satp`=0、`sfence.vma`、`fence.i`（M5）、`jr` で物理の入口へ（a0 = boot hart、a1 = handoff）。stack は kernel の locore が自分で設定する。
- `bootloader/uefi/riscv64/loader.ld`（新）: address 0 の位置独立の ELF。第 1 page は PE の header の場所、`.text`（code・rodata・dynsym 等）は 0x1000 から、`.data`（data・got・dynamic・`.rela.dyn`（`__rela_start`/`__rela_end`））は次の page から、`.bss`。
- `platform/riscv64/tools/make-bootriscv64.py`（新）: ELF の section と `_start` から PE32+ の header（machine 0x5064（Linux の pe.h で確かめた。design の「記憶」の値を確定）、2 section、SectionAlignment・FileAlignment 0x1000、RVA ＝ file の offset、`.data` の VirtualSize が `.bss` を含む、base relocation の directory は空、subsystem 10）を書いて flat の image の前に付ける。`check-bootriscv64.py`（新）: PE の整合と、ELF の dynamic relocation が RELATIVE だけ・undefined 無し。design は `pe-header.S` を案にしていたが、header の大きさの値は link の後でないと決まらないので tool で書く形にした（自作で転記なしは同じ）。
- `platform/riscv64/bootloader.mk`（新）: `-march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -fPIC -fvisibility=hidden -ffreestanding -fshort-wchar …`、`ld.lld -m elf64lriscv -shared -Bsymbolic --no-relax --gc-sections -z norelro --no-undefined --pack-dyn-relocs=none -e _start`（`-z notext` は付けない、minor 15）、`llvm-objcopy -O binary`、tool、checker。target `$(BUILD)/uefi/BOOTRISCV64.EFI`。`-fno-plt` は riscv で使われず clang が unused で落とすので外した。
- `platform/riscv64/disk-image.mk`・`tools/make-riscv64-disk-image.py`・`zedbsd.cfg`（`kernel=vmunix`）（新）: GPT（protective MBR、header と 128 entry、backup、決まった GUID）、ESP 1 つ（FAT32、serial 固定、`EFI/BOOT/BOOTRISCV64.EFI`・`zedbsd.cfg`・`vmunix`・`root.img`）。target `$(BUILD)/disk.img`。**design の「ESP＋root の 2 partition」から root の partition を外した**: kernel は virtio も SD も読まないので今は使い道が無く、root の partition の type は SD・rootfs の Phase（p014・p015）で決める。
- `platform/riscv64/run.mk` に `run-uboot`。
- HAL（実装だけ、hal.h は不変）: `cmain.c` が a1 の最初の word で loader の handoff（`ZBL6_HANDOFF_MAGIC`）と DTB を見分け、handoff なら version（1 以上）・size（版 1 以上）・boot hart の一致・parameter の長さ・ramdisk の範囲を検め、handoff の DTB を parse し、ramdisk を handoff の物（DT の `linux,initrd-*` の代わり）、command line を handoff の parameter（`bootinfo.c` の新しい `riscv64_boot_set_command_line`、DT の bootargs の代わり）に。log の行 `RISCV64 LOADER handoff=… fdt=… initrd=…+…`。memory は DT の `/memory`・`/reserved-memory` のまま（design §3.4 (b) の「UEFI の map が正」は採らなかった: U-Boot の渡す DT は QEMU・板の物で /memory が正しく、OpenSBI の予約も DT にある。loader の DTB の写し・ramdisk は今までどおり page.c が予約し、handoff と loader の image は cmain の後に要らないので予約しない。UEFI の runtime services は使わない）。
- `bootloader/uefi/README.md` に riscv64 の節。

## 確かめた事（host、2026-10-19）

- `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk build/riscv64-virt/uefi/BOOTRISCV64.EFI build/riscv64-virt/vmunix build/riscv64-virt/disk.img`: exit 0、`warning:`・`error:` 0（log `build/tmp/p004-build1.log`）。BOOTRISCV64.EFI 20480 byte、dynamic relocation は RELATIVE 36 個、undefined 0、`_start` は 0x1000。
- `check-bootriscv64.py` PASS。`llvm-readobj --file-headers --sections`（別の実装）でも Machine 0x5064、AddressOfEntryPoint 0x1000、SizeOfImage 0x9000、Subsystem EFI_APPLICATION、`.text` VA 0x1000 raw 0x3000、`.data` VA 0x4000 virtual 0x5000 raw 0x1000 と読めた。
- `plan/ws256/tests/loader/run.sh OUTDIR vmunix BOOTRISCV64.EFI bootriscv64.elf`: 15 件 PASS（riscv64 の vmunix の plan: entry 0xffff_ffff_8000_0000、link 0x8020_0000、16 MiB 以内、link の所と 0x9000_0000・0xa000_0000（placed の plan をもう一度 place）で各 segment が同じ距離だけ動く、2 MiB 揃えでない base・256 GiB を超える base・0 を拒む、amd64 の machine・ET_DYN・file の外の segment を拒む）＋ checker。
- `fdisk -l build/riscv64-virt/disk.img`: GPT、`disk.img1` 2048〜221183 の EFI System。`mdir`・`minfo`: FAT32、serial 5A45-4253、4 file。
- amd64 の UEFI loader の C 9 file を EFI の flag で compile（warning 0）、`elf64.o` の機械語は前後で一致。BOOTX64.EFI の link と amd64 の image の起動は未実施（`uefi.h` の変更は x86_64 で同じ定義になる `EFIAPI` と、未使用の static な GUID の追加だけ）。
- `git diff --check` 0。
- 未実施: U-Boot での起動（host に `u-boot-qemu` が未導入。T1）。

## Q1 へのお願い（T1 の前）

- host に U-Boot の `qemu-riscv64_smode` を入れる: `sudo apt install u-boot-qemu`（design §3.1 2、Q1 の担当）。`/usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin` が `boot-test.sh` の `riscv64-uboot` の既定。

## T1 への依頼（Q1 経由、U-Boot の導入の後）

image の作り方:

```
make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img build/riscv64-virt/uefi/BOOTRISCV64.EFI build/riscv64-virt/disk.img
```

- 走り A（合否）: `BOOT_MODE=riscv64-uboot BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$' OUTPUT=… plan/tools/boot-test.sh build/riscv64-virt/disk.img`（BOOT_PATTERN は既定の login の prompt）。合格: login の prompt（`login.png`）と `riscv64` の答え（`login-after.png`）。U-Boot と loader の間は画面が空（ramfb は HAL が設定する）。kernel の画面の上の方に `RISCV64 LOADER handoff=…` の行が出れば loader の道を通った印（流れたら判定に数えない）。
- 止まった時: 画面が空のままなら U-Boot か loader で止まった。T1 は serial の socket に残った文字（U-Boot と loader の ConOut、`RV64 UEFI FAILED: <段>` の行）を**解析の助けとして** P2 に渡す（判定には使わない、design §5.2）。kernel の画面が出て `vfs: …failed` で止まるなら、zedbsd.cfg の parser が足す `boot0=UUID=5A45-4253` を kernel が legacy の root の道で扱えるかの疑い（P2 が gdbstub で解析）。

## 残り

- U-Boot の導入（Q1）と T1 の結果の判定（Q1）。
- p005（VisionFive 2）。
