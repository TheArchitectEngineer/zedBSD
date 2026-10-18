<!-- awesome-plan project=zedbsd record=ws256-p001-design -->

# WS256 の設計: riscv64 の HAL と StarFive VisionFive 2（JH7110）への移植

2026-10-18 P2（q1243、[ws256-p001](phase001/phase.md)）の初版。親: [WS256](ws.md)。code は書かない。
HAL の API（`include/hal/`）の差分は [proposed/hal-riscv64.md](proposed/hal-riscv64.md) に案として置き、ユーザーの事前承認の前は適用しない。
外部の code（Linux・OpenSBI・U-Boot・QEMU）は手順と値を学ぶために読んだだけで、この文書にも zedBSD の code にも code・comment・構造を写さない（§0.2）。

ユーザーの決定（[ws.md](ws.md)、2026-10-18）のうちこの設計が前提にする物:
1. 最初の到達目標は **UART の console の login**（S-mode の kernel が OpenSBI の上で起動、SMP 4 core、timer と PLIC の割り込み、root は initrd か SD）。
2. 起動の道は **U-Boot の UEFI ＋ zedBSD の loader**（bootloader/uefi の riscv64 版）。DTB は U-Boot から。
3. toolchain の LLVM に RISCV の target を足すのは Q1（p002 cleared）。
4. 最初は **QEMU の virt** で HAL の芯を作り、実機は後（板は未接続）。

## 0. 出典とライセンス

### 0.1 読んだ物（事実の出典）

| 出典 | 版・場所 | license | 使い方 |
| --- | --- | --- | --- |
| zedBSD の今の code | `include/hal/hal.h`・`include/hal/arch.h`・`include/hal/arch/aarch64.h`・`src/hal/arm64/`（locore.S・cmain.c・int.c・irq.c・smp.c・percpu.[ch]・task.[ch]・space.[ch]・page.c・lib.c・trap.S・dispatch.S・bsp-rpi4/）・`src/hal/amd64/`（一覧）・`bootloader/uefi/`（bootx64.c の流れ・elf64.[ch]・transition.S・memory-map.h）・`bootloader/include/amd64-handoff.h`・`amd64-kernel-image.h`・`include/kern/boot.h`・`src/kern/entry.c`・`src/kern/platform/rpi4.c`・`src/drivers/platform/rpi4/rpi4-console.c`・`platform/arm64/*.mk`・`platform/amd64/vmunix.mk`（EFI の build と link）・`Makefile`・`tools/menuconfig.py`・`toolchain/llvm/`（llvm.mk・version.mk・patches）・`src/rtld/rtld.c`（arch の macro）・`userland/base/libc/{syscall-aarch64.S,static-tls.c,pthread.c,posix.c}`・`src/libc/crt/crt0-aarch64.S`・`include/uapi/{reg.h,signal.h}`・`include/libc/{elf.h,setjmp.h}`・`plan/tools/boot-test.sh` | Zlib | 形と契約の正。riscv64 はこの形を写す |
| Linux 6.8.12（WS229 p001 が検証して置いた tarball `/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12.tar.xz` から、RISC-V と StarFive の部分だけを P2 の scratchpad に展開。tree には入れない） | `arch/riscv/boot/dts/starfive/jh7110.dtsi`・`jh7110-starfive-visionfive-2.dtsi`・`-v1.2a.dts`・`-v1.3b.dts`（GPL-2.0 OR MIT）、`arch/riscv/include/asm/{csr.h,sbi.h,pgtable*.h}`・`arch/riscv/errata/sifive/errata.c`・`Kconfig.errata`・`Documentation/arch/riscv/boot.rst`・`drivers/irqchip/irq-sifive-plic.c`・`drivers/tty/serial/8250/8250_dw.c`・`drivers/clocksource/{timer-riscv.c,timer-clint.c}`・`drivers/cache/sifive_ccache.c`・`drivers/firmware/efi/libstub/riscv.c`・`arch/riscv/kernel/{efi-header.S,cpu_ops_sbi.c,head.S}`・`arch/riscv/mm/{init.c,cacheflush.c}`・`include/linux/efi.h`（GPL-2.0） | GPL-2.0（DTS は OR MIT） | 値（address・IRQ 番号・CSR の番号・bit・GUID）と手順の参考。code・comment・構造は写さない |
| QEMU v10.0.0（host の package は 1:10.0.11+ds-0+deb13u1） | `hw/riscv/virt.c`・`include/hw/riscv/virt.h`・`hw/riscv/boot.c`（gitlab の raw を読んだ） | GPL-2.0-or-later | virt の memory map・IRQ・firmware の渡し方（値） |
| OpenSBI v1.6（host の package opensbi 1.6-1、`/usr/share/qemu/opensbi-riscv64-generic-fw_dynamic.bin`） | `include/sbi/fw_dynamic.h`・`lib/sbi/{sbi_hsm.c,sbi_init.c,sbi_hart.c}`・`docs/firmware/fw_dynamic.md`・`docs/platform/qemu_virt.md` | BSD-2-Clause | HSM の振る舞い（非 boot hart の待ち、hart_start の前提と入口の状態）、fw_dynamic の形 |
| U-Boot v2025.01（Debian trixie の `u-boot-qemu`・`u-boot-starfive` は 2025.01-3+deb13u1） | `doc/board/starfive/visionfive2.rst`・`doc/board/emulation/qemu-riscv.rst`・`lib/efi_loader/{efi_riscv.c,efi_helper.c,efi_image_loader.c,Kconfig}`・`include/efi_api.h`・`arch/riscv/cpu/start.S`・`arch/riscv/lib/smp.c`・`boot/bootmeth_efi.c`・`configs/{starfive_visionfive2,qemu-riscv64_smode}_defconfig` | GPL-2.0+ | EFI の boot hart・DTB の渡し方、PE の読み込みの作法、VF2 の firmware の段と flash の配置 |
| RISC-V の文書 | riscv-non-isa/riscv-uefi `boot_protocol.adoc`（RISCV_EFI_BOOT_PROTOCOL）、riscv-non-isa/riscv-sbi-doc master `binary-encoding.adoc`（呼び出しの規約・error）と releases（v3.0 ratified、7 月 16 日。年は page に無く 2025 と推定）、riscv-non-isa/riscv-elf-psabi-doc `riscv-elf.adoc`（relocation・TLS・e_flags）、riscv/riscv-isa-manual（privileged の正。版の名前は取れず、CSR と cause の値は Linux の csr.h で確かめた） | CC-BY 4.0（RISC-V の文書） | 規格の正。値は Linux の header と照合 |
| StarFive JH7110 TRM | `doc-en.rvspace.org/JH7110/TRM/JH7110_TRM/system_memory_map.html`（system memory map の表） | StarFive の文書 | SoC の address（値） |
| host の確かめ（2026-10-18） | `build/llvm/bin/clang --print-targets`、`--target=riscv64-unknown-zedbsd` の試し compile（§1.8）、`dpkg -l`・`apt-cache policy`（QEMU 10.0、opensbi 1.6、u-boot-qemu と qemu-efi-riscv64 は未導入）、Debian の package の file の一覧 | — | toolchain と試験の環境の事実 |

取れなかった物（推測と明記して使う）: UEFI 2.10 の §2.3.7（RISC-V の呼び出しの規約と handoff の状態。uefi.org が 403）、PE/COFF の machine の値 `IMAGE_FILE_MACHINE_RISCV64 = 0x5064`（記憶。p004 で Linux の `include/linux/pe.h` か U-Boot の `include/pe.h` で確かめる）、RISC-V privileged の版の名前、VisionFive 2 の板に今入っている vendor の firmware の U-Boot の版、JH7110 の U74 の `mimpid`（errata の該当の判定）。

### 0.2 ライセンスと転記の扱い

- guardrail「MIT・GPL の出典のデータの引用」（2026-10-15、全部の WS）のとおり: register の offset・bit・IRQ 番号・CSR の番号・GUID・表は**データ**として引用し、zedBSD の code では block ごとに直前の複数行 comment で「Value(s) referenced: <版> <path> (<define・表・node の名前>)」、手順は「Procedure referenced: <版> <path> (<関数の名前>)」と書く。file の先頭には出典・派生の宣言・SHA を一切書かない。
- **code・comment・構造・symbol の名前は、GPL でも MIT でも BSD でも写さない。** Linux の `_PAGE_*`・`SBI_EXT_*`・`CSR_*`、OpenSBI の `sbi_*`、QEMU の `virt_memmap`、U-Boot の `efi_*` の名前を zedBSD に持ち込まない。zedBSD の riscv64 の名前は `riscv64_*`（HAL の内部）、`RISCV64_*`（macro）、BSP は `vf2_*`・`virt_*`。
- DTS（GPL-2.0 OR MIT）の node の名前・property の名前は Devicetree の binding（規格）であり、HAL の parser が探す文字列（`riscv,isa-extensions`・`mmu-type`・`interrupts-extended`・`reg-shift` など）として使う。これは規格の語で転記ではない。
- RISC-V の文書（CC-BY 4.0）の語（CSR の名前、SBI の extension の名前）は規格の語として使う。
- zedBSD の新しい code は全部 Zlib。参照した GPL の file から値を引用しても、明示した引用なら license に影響しない（guardrail 2026-10-15 の WS039 への適用の文と同じ）。
- source に開発の管理情報（WS・Phase・Queue・試験の回・QEMU の話）を書かない（guardrail 2026-10-15・10-17）。この文書の中の話は plan/ に置く。

### 0.3 今の code の読み方

- arm64 の HAL を「同じ形」の正として読んだ: file の分け方（locore・cmain・int・irq・smp・percpu・task・space・page・lib・watch・debug・bsp-*）、kernel との契約（`kernel_entry`・`kernel_secondary_entry`・`kernel_timer_handler`・`kernel_cpu_notify_handler`・`kernel_syscall_handler`・`kernel_user_fault_handler`・`kernel_sys_fault_handler`・`kernel_user_return_handler`・`kernel_putc`・`hal_get_arch_handoff("boot.command-line")`）、per-CPU の record と ready mask、secondary の boot block、IRQ の slot と ack の持ち方、task の stack の作り方。
- amd64 を「loader が物理の置き場を選び、HAL が linked の仮想の範囲を実際の物理に当てる」契約（`bootloader/include/amd64-kernel-image.h`）の正として読んだ。riscv64 はこの契約を写す（§3.4）。
- 読んだが持ち込まない物: Linux の trap の入口・per-CPU・page table の code、OpenSBI の関数、U-Boot の構造。

## 1. 調べた事実

### 1.1 RISC-V の特権の仕様（S-mode の kernel が使う範囲）

出典: riscv-isa-manual（privileged）。値は Linux 6.8.12 `arch/riscv/include/asm/csr.h` の define で照合（Value(s) referenced の対象）。

- 特権: M-mode（firmware＝OpenSBI）、S-mode（kernel）、U-mode（user）。kernel は S-mode で動き、M-mode の資源（CLINT の mtime/mtimecmp/msip、PMP、mhartid）に触れない。hart の id は firmware が `a0` で渡す物を使う。
- CSR（番号）: `sstatus` 0x100（SIE bit 1、SPIE bit 5、SPP bit 8、FS bits 14:13、SUM bit 18、MXR bit 19）、`sie` 0x104、`stvec` 0x105、`scounteren` 0x106、`senvcfg` 0x10a、`sscratch` 0x140、`sepc` 0x141、`scause` 0x142、`stval` 0x143、`sip` 0x144、`satp` 0x180、`stimecmp` 0x14d（Sstc の拡張がある時だけ）、`time` 0xc01（`rdtime`、`scounteren`/`mcounteren` で U/S に許された時）。
- 割り込み（`scause` の最上位 bit が 1）: SSIP 1（supervisor software、IPI）、STIP 5（timer）、SEIP 9（external、PLIC）。`sie` の同じ bit で個別に許す。
- 例外（`scause`）: 0 命令の misaligned、1 命令の access fault、2 不正命令、3 breakpoint（`ebreak`）、4 load misaligned、5 load access、6 store/AMO misaligned、7 store/AMO access、8 U-mode からの `ecall`、9 S-mode からの `ecall`、12 命令の page fault、13 load の page fault、15 store/AMO の page fault。`stval` は fault の address（access・page fault・misaligned）。`ecall` は 4 byte の命令（圧縮形は無い）なので syscall の戻りは `sepc + 4`。
- `wfi`: `sstatus.SIE` が 0 でも `sie` で許した割り込みが pending になれば戻る（割り込みの trap は SIE を立てた時に取られる）。
- MMU: `satp` の MODE（bits 63:60）= 8 で Sv39、9 で Sv48、10 で Sv57、0 で無効。ASID は bits 59:44、root table の PPN は bits 43:0。Sv39 は 3 段（各 512 entry、VPN 9 bit ×3）、仮想 address は bit 38 を符号拡張（user 側 `0x0000_0000_0000_0000`〜`0x0000_003f_ffff_ffff`、kernel 側 `0xffff_ffc0_0000_0000`〜）。Sv48 は 4 段、kernel 側は `0xffff_8000_0000_0000` から。
- PTE: V bit 0、R 1、W 2、X 3、U 4、G 5、A 6、D 7、RSW bits 9:8（software 用）、PPN bits 53:10。R=W=X=0 の valid な PTE は次の段の table の pointer。leaf は任意の段（Sv39 なら 1 GiB・2 MiB・4 KiB）。Svpbmt（拡張）がある時だけ bits 62:61 が memory type（0 PMA、1 non-cacheable、2 I/O）。**U74 には Svpbmt が無い**（§1.6）: cache の属性は address ごとの PMA で固定で、PTE では変えられない。
- A/D bit: 仕様では hardware が立てる実装と、立てずに page fault を起こす実装（Svade）が許される。QEMU は既定で立てる（推定）、U74 は立てる（SiFive の文書の記憶。p005 で確かめる）。HAL は「A=0 または D=0 で fault が来たら立てて再実行」の経路も持つ（§2.5）。
- TLB: `sfence.vma`（全部、または address・ASID を指定）。他の hart は SBI RFENCE か IPI。命令 cache: `fence.i`（自分の hart だけ）、他の hart は SBI の remote_fence_i。
- 浮動小数点: F/D の 32 本の 64 bit register と `fcsr`。`sstatus.FS` が Off なら FP 命令は不正命令の trap。

### 1.2 SBI（Supervisor Binary Interface）

出典: riscv-sbi-doc master `binary-encoding.adoc`（規約と error）、extension の ID は Linux 6.8.12 `arch/riscv/include/asm/sbi.h` の値、OpenSBI v1.6 の source。

- 呼び出し: `ecall`。`a7` = extension ID、`a6` = function ID、`a0`〜`a5` = 引数。戻りは `a0` = error、`a1` = value。`a0`・`a1` 以外は callee が保つ。error: 0 成功、-1 FAILED、-2 NOT_SUPPORTED、-3 INVALID_PARAM、-4 DENIED、-5 INVALID_ADDRESS、-6 ALREADY_AVAILABLE、-7 ALREADY_STARTED、-8 ALREADY_STOPPED、-9 NO_SHMEM、-10 INVALID_STATE、-11 BAD_RANGE、-12 TIMEOUT、-13 IO、-14 DENIED_LOCKED。
- 使う extension（ID）: Base 0x10（FID 0 spec version、3 probe_extension、4〜6 mvendorid/marchid/mimpid）、**TIME** 0x54494D45（FID 0 set_timer(u64 stime)）、**IPI** 0x735049（FID 0 send_ipi(hart_mask, hart_mask_base)）、**RFENCE** 0x52464E43（FID 0 remote_fence_i、1 remote_sfence_vma、2 remote_sfence_vma_asid。hart_mask は IPI と同じ形）、**HSM** 0x48534D（FID 0 hart_start(hartid, start_addr, opaque)、1 hart_stop、2 hart_get_status。状態 0 STARTED、1 STOPPED、2 START_PENDING、3 STOP_PENDING）、**SRST** 0x53525354（FID 0 system_reset(type 0 shutdown / 1 cold reboot / 2 warm reboot, reason 0)）、**DBCN** 0x4442434E（FID 0 console_write(num_bytes, lo, hi)、1 console_read、2 console_write_byte(byte)）。legacy（v0.1、`a7` だけ、`a6` 無し）: 0x00 set_timer、0x01 console_putchar、0x02 console_getchar、0x08 shutdown。
- OpenSBI v1.6 の振る舞い（source で確かめた）:
  - **非 boot hart は OpenSBI の中で HSM の STOPPED の状態で待つ**（`sbi_hsm.c`: cold boot の hart だけ START_PENDING→STARTED、他は STOPPED で `wfi` の loop）。次の段（U-Boot・kernel）に入るのは boot hart だけで、他の hart は次の段が HSM hart_start を呼ぶまで出てこない。cold boot の hart は lottery（最初に atomic swap した hart）か fw_dynamic の boot_hart で決まる。
  - hart_start の前提: 状態が STOPPED でなければ error（STARTED なら -6、他は -3）。始まった hart は `start_addr` に **S-mode、`a0` = hartid、`a1` = opaque、`satp` = 0、`sie` = 0、`sscratch` = 0、`stvec` = start_addr** で入る（`sbi_hart.c` の mode の切り替えで CSR を書いてから `mret`）。
  - fw_dynamic の情報（`fw_dynamic.h`、`a2` で前の段から受ける）: magic 0x4942534f（"OSBI"）、version 2、next_addr、next_mode（0 U、1 S、3 M）、options、boot_hart（-1 なら lottery）。
  - OpenSBI は自分の firmware の領域を PMP で M-mode 専用に守る（S-mode から触ると access fault）。DTB の `/reserved-memory` にその領域（`mmode_resv*`）を足す。HAL はこれを予約として読む。
- SBI の版: OpenSBI 1.6 が報告する spec version は起動時に Base FID 0 で読んで log に出す（p003 の受け入れの 1 つ。推定では 2.0）。SBI v3.0（2025-07 ratified）は OpenSBI 1.6 の後。

### 1.3 PLIC と CLINT/ACLINT

出典: PLIC の register の配置は QEMU `include/hw/riscv/virt.h` の `VIRT_PLIC_*` と Linux `irq-sifive-plic.c` の define（両方同じ値。規格は riscv/riscv-plic-spec）。

- PLIC の register（base からの offset）: priority `0x0000 + 4*irq`（irq 1〜、0 は無い）、pending `0x1000 + (irq/32)*4`、**context ごとの enable** `0x2000 + ctx*0x80 + (irq/32)*4`、**context ごとの threshold** `0x200000 + ctx*0x1000 + 0`、**claim/complete** `0x200000 + ctx*0x1000 + 4`（読むと最優先の pending の irq 番号を claim、0 は無し。同じ値を書くと complete）。priority は QEMU で 3 bit（1〜7）。threshold 0 で全部通す、7 で全部止める。
- **context の番号**は hardware ごとに違い、DT の `interrupts-extended` の並び（i 番目の entry = context i、その phandle が hart の intc、引数 11 = M-mode external、9 = S-mode external）で決まる。QEMU virt（TCG）は hart h の M が `2h`、S が `2h+1`。JH7110 は hart 0（S7）の M が 0、hart h（1〜4）の M が `2h-1`、**S が `2h`**（§1.6 の dtsi）。HAL は DT から「hart の id → S-mode の context」の表を作る（固定しない）。
- level/edge は PLIC の register では設定できない（source 側の性質）→ `hal_irq_set_mode` は LEVEL/HIGH をそのまま受け、他は `HAL_ERR_UNSUPPORTED`。MSI は PLIC に無い（AIA の IMSIC は virt の `aia=aplic-imsic` だけ。U74 に無い）→ `hal_irq_*_msi` は `HAL_ERR_UNSUPPORTED`（arm64 と同じ）。
- CLINT（QEMU virt 0x2000000、JH7110 0x2000000）は M-mode の資源（msip 0x0、mtimecmp 0x4000、mtime 0xbff8）。**S-mode の kernel は触らない。** timer は SBI TIME か Sstc の `stimecmp`、IPI は SBI IPI。counter は `rdtime`（`time` CSR）で、周波数は DT の `/cpus/timebase-frequency`（QEMU virt 10 MHz、JH7110 **4 MHz**）。
- Sstc（`stimecmp`）: QEMU の rv64 CPU は持つ（DT の `riscv,isa-extensions` に `sstc`）、**U74 は持たない**（§1.6 の一覧に無い）→ HAL は DT で選ぶ: 有れば `stimecmp` に deadline を書く、無ければ SBI set_timer（ecall 1 回／tick、1 kHz で問題無し）。

### 1.4 UART

- QEMU virt: `ns16550a`、base 0x10000000、size 0x100、**register の間隔 1 byte（reg-shift 0）**、clock-frequency 3686400、PLIC の IRQ **10**（`virt.c`・`virt.h`）。
- JH7110: Synopsys DesignWare APB UART（`snps,dw-apb-uart`）、UART0 base 0x10000000、size 0x10000、**reg-io-width 4・reg-shift 2（32 bit access、間隔 4 byte）**、PLIC の IRQ **32**（UART1 0x10010000 IRQ 33、UART2 0x10020000 IRQ 34、UART3〜5 は 0x12000000〜）。clock は SYSCRG の UART0_CORE/APB。VisionFive 2 の `stdout-path` は `serial0:115200n8`（uart0）。
- DW の 8250 の癖（Linux `8250_dw.c` の手順を参考、値を引用）: UART Status Register `USR` は 16550 の offset 0x1f（shift 2 で 0x7c）、busy の時に LCR を書くと無視され **IIR = 0x7（busy detect）の割り込み**が立つ → IIR が 0x7 なら USR を読んで消す。設計: HAL と driver は VF2 で LCR・分周（DLL/DLM）を書かず、**U-Boot が設定した 115200 をそのまま使う**（QEMU も同じ）。将来の baud の変更は tty の cfsetspeed の経路で、busy の待ち（USR bit 0）を入れてから。
- 16550 の register（規格、`UART_*` の定数は書かない）: RBR/THR 0、IER 1、IIR/FCR 2、LCR 3、MCR 4、LSR 5（bit 0 data ready、bit 5 THR empty）、MSR 6、SCR 7。RX の割り込みは IER bit 0（received data available）、FIFO は FCR で有効化。

### 1.5 QEMU virt（riscv64）の memory map と firmware

出典: QEMU v10.0.0 `hw/riscv/virt.c` の表、`include/hw/riscv/virt.h`、`hw/riscv/boot.c`。

| 領域 | base | size | 備考 |
| --- | --- | --- | --- |
| MROM（reset vector） | 0x1000 | 0xf000 | reset の code が a0=mhartid、a1=DTB、a2=fw_dynamic_info で firmware へ |
| test（sifive-test） | 0x100000 | 0x1000 | poweroff/reset。SBI SRST があるので使わない |
| RTC（goldfish） | 0x101000 | 0x1000 | IRQ 11。`hal_rtc_read_epoch_time` の候補（後） |
| CLINT | 0x2000000 | 0x10000 | M-mode。触らない |
| ACLINT SSWI | 0x2F00000 | 0x4000 | `aclint=on` の時だけ |
| PCIe PIO | 0x3000000 | 0x10000 | |
| platform bus | 0x4000000 | 0x2000000 | IRQ 64〜95 |
| PLIC | 0xc000000 | CPU 数に依る | source 96、priority 3 bit |
| UART0 | 0x10000000 | 0x100 | IRQ 10 |
| virtio-mmio ×8 | 0x10001000 | 0x1000 ずつ | IRQ 1〜8 |
| fw_cfg | 0x10100000 | 0x18 | |
| flash ×2（pflash） | 0x20000000 | 0x2000000 ずつ | edk2 の code/vars 用、各 32 MiB ちょうど |
| PCIe ECAM | 0x30000000 | 0x10000000 | IRQ 32〜35（INTx） |
| PCIe MMIO | 0x40000000 | 0x40000000 | |
| **DRAM** | **0x80000000** | `-m` の値 | |

- firmware: `-bios` の既定は OpenSBI の `opensbi-riscv64-generic-fw_dynamic.bin`（host に有る）。DRAM の先頭 0x80000000 に置かれ、`-kernel` の S-mode の payload は firmware の終わりを 2 MiB に揃えた所（＝**0x80200000**）に置かれる（flat な image の時。**ELF は自分の p_paddr に置かれる**）。DTB は RAM の終わりの近くに 2 MiB 揃えで置かれ、`-append` は `/chosen/bootargs` に入る。`-initrd` は kernel の低い address + min(RAM/2, 512 MiB) に置かれ、`/chosen/linux,initrd-start/end` に書かれる。
- `-kernel` の時 OpenSBI は fw_dynamic の next_addr = kernel の低い load address、next_mode = S、boot_hart = 0 で、**kernel は boot hart 0 だけで `a0` = 0、`a1` = DTB の物理 address、`satp` = 0 の S-mode から始まる**。他の hart は HSM の STOPPED。
- CPU: `-cpu rv64`（既定、H 拡張あり）。DT の `mmu-type` は QEMU が支える最大（Sv57）になるが、`satp` に Sv39 を書くのは許される。`riscv,isa-extensions` に `sstc`・`svpbmt`・`zicbom` などが並ぶ（p003 で `-machine dumpdtb=` の DTB を見て確かめる。これは guest を走らせない）。
- machine の option: `aia=none`（既定、PLIC）、`aclint=off`（既定、CLINT）、`acpi=on`（既定。DT も渡される。zedBSD は DT だけを読む）。
- UEFI の firmware の選択（§3.1）: (a) U-Boot `qemu-riscv64_smode`（Debian `u-boot-qemu` 2025.01-3+deb13u1、`/usr/lib/u-boot/qemu-riscv64_smode/{u-boot.bin,uboot.elf}`。**host に未導入**）を `-kernel` で渡す。(b) edk2 RiscVVirtQemu（`RISCV_VIRT_CODE.fd`/`VARS.fd` を pflash0/1 に）。Debian trixie に `qemu-efi-riscv64` は無い（`apt-cache policy` に候補無し）ので自分で build する必要がある。

### 1.6 JH7110 と VisionFive 2

出典: Linux 6.8.12 の DTS（`jh7110.dtsi`・`jh7110-starfive-visionfive-2.dtsi`・v1.2a/v1.3b）、StarFive TRM の memory map、`errata/sifive/errata.c`、`drivers/cache/sifive_ccache.c`、U-Boot `doc/board/starfive/visionfive2.rst`・`starfive_visionfive2_defconfig`。

- CPU: **hart 0 は SiFive S7**（`rv64imac_zba_zbb`、MMU 無し、DTS で `status = "disabled"`）、**hart 1〜4 は SiFive U74-MC**（`rv64imafdc_zba_zbb`、`riscv,isa-extensions` = i m a f d c zba zbb zicntr zicsr zifencei zihpm、`mmu-type = "riscv,sv39"`、`tlb-split`、I/D cache 各 32 KiB・64 byte line）。**Sstc・Svpbmt・Zicbom・Zihintpause は無い**。`timebase-frequency` = **4000000**。
  → HAL は `/cpus` の中で `mmu-type` を持ち `status` が disabled でない cpu だけを数える（S7 を除く）。OpenSBI の boot hart は lottery で 1〜4 のどれか（U-Boot の文書の log では 2）。**boot hart が 0 でないので、logical CPU 0 = boot hart、hart id ≠ logical id**（§2.3）。
- CLINT 0x2000000（size 0x10000、M-mode）。**L2 cache controller（SiFive ccache）0x2010000**（size 0x4000、2 MiB、64 byte line、`FLUSH64` register offset 0x200: 64 byte line の物理 address を書くとその line を flush）。PLIC 0xc000000（size 0x4000000、`riscv,ndev` = 136、context の並び §1.3）。
- UART0 0x10000000（§1.4）、I2C 0x10030000〜、SPI 0x10060000。TRM の map: SYSCRG 0x13020000、SYS SYSCON 0x13030000、IOMUX 0x13040000、QSPI CSR 0x13010000、AON CRG 0x17000000、AON SYSCON 0x17010000、AON GPIO 0x17020000、SDIO0 0x16010000、SDIO1 0x16020000、GMAC0 0x16030000、GMAC1 0x16040000、DW DMAC 0x16050000、USB 0x10100000、PCIe0/1 CSR 0x2b000000/0x2c000000、PCIe の 32 bit の memory 0x30000000/0x38000000、PCIe の memory 0x9_0000_0000〜、Boot ROM 0x2a000000、**DDR 0x40000000 から（最大 8 GiB、0x2_3fff_ffff まで）**。
- memory: DTS は `memory@40000000` に 4 GiB と書き、U-Boot が板の実際の量（2/4/8 GB の品がある）で `/memory` を直す（U-Boot は `CONFIG_ID_EEPROM=y` で板の情報を EEPROM から読む）。HAL は DT の `/memory` と `/reserved-memory` だけを信じる（UEFI の道では UEFI の memory map が正）。
- 板の版: **v1.2A と v1.3B** の DTS の差は GMAC1 の phy-mode（1.2A は RMII）と PHY の遅延の値だけ。UART の login には関係しない。U-Boot は `$fdtfile` を版で選ぶ。
- 起動の段（U-Boot の文書）: BootROM → **SPL**（`u-boot-spl.bin.normal.out`、GUID 2E54B353-1271-4842-806F-E436D6AF6985 の partition か QSPI の offset 0x0）→ **u-boot.itb**（OpenSBI `fw_dynamic.bin` ＋ U-Boot ＋ DTB、GUID BC13C2FF-59E6-4262-A352-B275FD6F7172 の partition か QSPI の 0x100000）→ **U-Boot（S-mode、DRAM）**。OpenSBI は `FW_TEXT_START=0x40000000`（DDR の先頭）、SPL は 0x40000000 に置く（defconfig の `SPL_OPENSBI_LOAD_ADDR`）。boot の切り替え MSEL[1:0]: `10` SD、`00` flash。U-Boot の `SYS_LOAD_ADDR` = 0x82000000。defconfig は `RISCV_SMODE`・`BOOTSTD_DEFAULTS`（EFI の boot method を含む）・`OF_BOARD`。**Debian trixie の `u-boot-starfive`（2025.01-3+deb13u1）に `u-boot-spl.bin.normal.out`・`u-boot.itb` が入っている**ので、自分で build しなくてよい。
- U74 の errata（Linux の `errata.c` の判定の値）: **CIP-453**（marchid 0x8000000000000007、mimpid 0x20181004〜0x20191105: page の境界の分岐で `stval` の上位が符号拡張されない → 命令の page fault/access fault の address を HAL が符号拡張する）、**CIP-1200**（marchid 0x8000000000000007 か 0x1、mimpid の下位 24 bit ≤ 0x200630 かつ ≠ 0x1200626: `sfence.vma addr` が効かない → 全体の `sfence.vma` にする）。JH7110 の mimpid は未確認（p005 で SBI Base の get_mimpid で読んで log に出す）。設計は**両方の緩和を最初から入れる**（命令 fault の address の符号拡張は常に無害、TLB flush は range 版も全体 flush で実装）。U74 の `mimpid` が該当しなければ p005 の後で range 版に戻すのは最適化。
- DMA の coherency: JH7110 の周辺（GMAC・SDIO・USB）の DMA は CPU の cache を snoop しない。Linux は SiFive ccache の `FLUSH64` に line の物理 address を書いて clean+invalidate する（`sifive_ccache.c` の手順）。zedBSD では `hal_dcache_clean_range`・`hal_dcache_invalidate_range`・`hal_dcache_clean_invalidate_range` を VF2 の BSP が ccache で実装し（64 byte ごとに FLUSH64）、virt では no-op（DMA は coherent）。UART の login には不要だが、HAL の関数は揃える（p005）。`hal_pmem_map_uncached` は Svpbmt が無いので VF2 では `HAL_ERR_UNSUPPORTED`（PMA で決まる）。SD・GbE・USB の driver は別の WS（§7）。

### 1.7 UEFI（riscv64）と U-Boot の作法

出典: riscv-uefi `boot_protocol.adoc`、U-Boot v2025.01 の `lib/efi_loader/`、Linux 6.8.12 `include/linux/efi.h`・`libstub/riscv.c`・`Documentation/arch/riscv/boot.rst`・`arch/riscv/kernel/efi-header.S`。

- **boot hart の渡し方**: `RISCV_EFI_BOOT_PROTOCOL`（GUID **ccd15fec-6f73-4eec-8395-3e69e4b940bf**、revision 0x00010000、関数 `GetBootHartId(This, UINTN *BootHartId)`）。UEFI の application の呼び出し規約では `a0` に hartid を渡せないので protocol になった。loader は `ExitBootServices` の前に呼ぶ。U-Boot は `lib/efi_loader/efi_riscv.c`（`CONFIG_EFI_RISCV_BOOT_PROTOCOL`、RISC-V で既定 y）で `gd->arch.boot_hart` を返す。Linux の EFI stub は protocol が無い時だけ `/chosen/boot-hartid`（非推奨）を読む。
- **DTB の渡し方**: EFI の configuration table、GUID **b1b621d5-f19c-41a5-830b-d9152c69aae0**（Linux `DEVICE_TREE_GUID` ＝ U-Boot `EFI_FDT_GUID`）。U-Boot は `efi_install_fdt()` で DT を複写（12 KiB の余白）し、`/reserved-memory` を EFI の予約に写し（`efi_carve_out_dt_rsv`）、table に入れる。bootstd の EFI の方法は boot partition の `${fdtfile}`（`/dtb/`・`/dtbs/` などを `efi_get_distro_fdt_name` が順に試す）を読み、無ければ firmware の持つ DT（control DT）を使う。→ **loader は configuration table の DTB を自分の確保した memory に写して kernel に渡す**（`ExitBootServices` の後も残る EfiACPIReclaim/LoaderData の領域に）。
- **PE/COFF の loader（U-Boot `efi_image_loader.c`）**: machine は `IMAGE_FILE_MACHINE_RISCV64`（値は 0x5064 と記憶、要確認）、PE32+（optional header の magic 0x20b）、section は `SectionAlignment` で確保、entry = 読み込んだ base + `AddressOfEntryPoint`。**relocation の directory が無い image は何も直さず黙って受ける**（`delta` ≠ 0 でも）→ **loader は自分で relocate する**（§3.3）。読み込みの後 `flush_cache` と I-cache の invalidate をする。removable media の既定の path は `\EFI\BOOT\BOOTRISCV64.EFI`（U-Boot `efi_helper.c` の名前、UEFI 3.5.1.1）。
- U-Boot の S-mode の状態（`arch/riscv/cpu/start.S` から推定）: MMU は使わない（`satp` に触れない）、boot hart だけが動く（OpenSBI が他を STOPPED に持つ。U-Boot の `secondary_hart_loop` は M-mode の build か SPL の話）。application に入る時の `sstatus.SIE`・`sie` の状態は未確認 → loader の transition は最初に SIE を落とし `sie` を 0 にする。
- UEFI の RISC-V の呼び出し規約（UEFI 2.10 §2.3.7、未再読）: 整数の呼び出し規約は psABI と同じ、`EFIAPI` は空。application は S-mode。**firmware が paging を有効にしているかは firmware 次第**（edk2 の RiscVVirt は identity map を張る、U-Boot は張らない）→ loader は `satp` の値を前提にせず、自分の table を作って切り替える。
- Linux の EFI の image の作り方（`efi-header.S`、手順の参考）: ELF の先頭に **自作の PE32+ の header**（DOS の stub、COFF header、optional header、`.text`・`.data` の section table、relocation の directory は空）を置き、`objcopy -O binary` で flat にする。code は PIC で、自分の `.rela.dyn` を入口で当てる。zedBSD の loader も同じ手順（§3.3）。
- RISC-V の ELF（psABI）: `EM_RISCV` = 243（zedBSD の `include/libc/elf.h` に既にある）、`e_flags`: RVC 0x1、float ABI double 0x4。dynamic linker が扱う relocation: NONE 0、32 1、**64 2、RELATIVE 3、COPY 4、JUMP_SLOT 5**、TLS_DTPMOD64 7、TLS_DTPREL64 9、TLS_TPREL64 11、TLSDESC 12、IRELATIVE 58。TLS は Variant I で **`tp` は TCB の終わりの次を指す**（TCB は tp の手前）。`__global_pointer$`（gp）は dynamic な executable の dynsym に要る（GP 相対の access がある時）。stack の揃えは 16。

### 1.8 toolchain の事実（2026-10-18 に host で確かめた）

- 共有の `build/llvm/bin/clang`（23.1.0、zedbsd8、p002 で RISCV を足した物）: `--print-targets` に riscv64 がある。`--target=riscv64-unknown-zedbsd -march=rv64gc -mabi=lp64d -mcmodel=medany -ffreestanding -nostdlibinc -c` は object を出し、`__riscv_xlen 64`・`__riscv_float_abi_double`・`__riscv_atomic`・`__riscv_compressed` を定義する。**しかし `__ZEDBSD__` を定義しない**（`aarch64-unknown-zedbsd` は定義する）。理由: patch `0001-add-zedbsd-x86-target.patch` の `clang/lib/Basic/Targets.cpp` に `ZedBSDTargetInfo` の case が X86_32・X86_64・AArch64 にしか無い。
- 同 patch の `clang/lib/Driver/ToolChains/ZedBSD.cpp` の linker は `-m` の emulation を x86_64 → `elf_x86_64`、aarch64 → `aarch64elf`、**それ以外 → `elf_i386`** とするので、riscv64 の userland の link は壊れる（kernel は `ld.lld -m elf64lriscv` を直に呼ぶので影響しない）。
- `--target=riscv64-unknown-windows -c` は COFF でなく **ELF を出す**（`llvm-objdump -f` で `elf64-littleriscv`）。`lld-link /machine:riscv64` は「unknown /machine argument」。→ amd64 の BOOTX64.EFI の作り方（COFF＋lld-link）は使えない。
- `ld.lld -m elf64lriscv -shared -Bsymbolic -z notext --no-relax` は PIC の object から `.rela.dyn` に `R_RISCV_RELATIVE` を持つ shared object を作り、`llvm-objcopy -O binary` は flat にできる（確かめた）。
- `include/uapi/hosted.h`・`include/kern/kcrt.h` は `__ZEDBSD__` の無い compiler のために `KERN_UAPI_NATIVE`・`KERN_KCRT_NATIVE` の define を持つ（sparcv9 の GCC の道）。→ kernel と HAL の Phase（p003・p008）は toolchain の patch を待たずに `--target=riscv64-unknown-elf` ＋ これらの define で build できる。**userland（p007）には `riscv64-unknown-zedbsd` が `__ZEDBSD__` を定義し linker の emulation が正しいことが要る** → §4.5 の toolchain の依頼。

## 2. HAL の riscv64 の構成

### 2.1 方針

- **S-mode・SBI の上の HAL**。M-mode の資源（CLINT・PMP・mhartid）に触れない。timer は Sstc か SBI TIME、IPI は SBI IPI、他 hart の fence は SBI RFENCE、起動は SBI HSM、reset/poweroff は SBI SRST、早期 console は SBI DBCN（無ければ legacy の putchar）。
- **DT で動く芯**: hart の一覧と id、timebase、PLIC の base と context、UART の base/間隔/IRQ、memory と予約は全部 DT から読む。board（BSP）に残るのは DT に無い物だけ: VF2 の ccache の flush、errata の緩和の有無、将来の clock/reset の gating。QEMU virt の BSP はほぼ空。
- **arm64 と同じ file の分け方と kernel との契約**。HAL の API（hal.h）は変えない。arch の header `include/hal/arch/riscv64.h` を足し `include/hal/arch.h` に 1 行足す（承認が要る: [proposed/hal-riscv64.md](proposed/hal-riscv64.md)）。
- **Sv39 を既定**（U74 は Sv39 だけ。QEMU も Sv39 を受ける）。address の定数は `RISCV64_VA_BITS` の macro で持ち、Sv48 は Future Work（§7 の判断 5）。
- kernel の C は **FP を使わない**（`-mabi=lp64`、`-march=rv64imac_zicsr_zifencei`）。FP の保存と復元の .S だけ `-march=rv64gc`（ABI は lp64 のまま。lld は float ABI の違う object の混在を拒むので ABI は揃える）。user は `lp64d`。
- 2 つの入口（§3.4）: (a) **SBI の直の起動**（QEMU の `-kernel vmunix`、`a0` = hartid、`a1` = DTB）、(b) **loader からの起動**（`a0` = hartid、`a1` = handoff の物理 address、handoff の先頭が `KERN_HANDOFF_MAGIC`）。locore が `a1` の先頭 4 byte で見分ける（DTB の magic 0xd00dfeed は big-endian で `d0 0d fe ed`、`KERN_HANDOFF_MAGIC` 0x48323842 は little-endian で `42 28 32 48`。衝突しない）。(a) は p003 の試験と QEMU の回帰の近道、(b) が製品の道。

### 2.2 file の一覧と責務（`src/hal/riscv64/`）

| file | 責務 | arm64 の対応 |
| --- | --- | --- |
| `locore.S` | 入口 `_start`（§2.1 の 2 つの入口の見分け、`gp` は使わない、boot の stack、BSS の零化、boot 用の静的な Sv39 の table で kernel の窓と識別の map を張り `satp` を書いて高い address へ跳ぶ）、`riscv64_secondary_entry`（HSM で始まった hart: `a1` の boot block から satp・stack・CPU 番号を取る） | locore.S |
| `trap.S` | `stvec`（direct mode）の 1 つの入口。`sscratch` の交換で user/kernel を見分け、frame（x1〜x31、sepc、sstatus、scause、stval）を保存、`riscv64_trap(frame)` を呼び、復帰（`sret`）。user へ戻る時は `sscratch` に per-CPU の record を戻す | trap.S |
| `dispatch.S` | `asm_task_dispatch`（callee-saved s0〜s11・ra・sp の切り替え）、kernel task・user task・user frame の最初の入口、FP の保存と復元（f0〜f31・fcsr、`-march=rv64gc`） | dispatch.S |
| `cmain.c` | boot hart の最初の C: per-CPU の bootstrap、早期 console（SBI）、boot source（DTB か handoff）の解釈、FDT の parse、`riscv64_smp_configure`、page/space/int/debug の init、`kernel_entry(handoff)` | cmain.c |
| `fdt.c`/`fdt.h` | 自前の小さな FDT の parser（rpi4 の fdt.c と同じ役、別の実装）: `/cpus`（hart id = `reg`、`mmu-type`、`status`、`riscv,isa-extensions` の sstc/svpbmt/zicbom、intc の phandle）、`/cpus/timebase-frequency`、`/memory`、`/reserved-memory`、PLIC（`sifive,plic-1.0.0`: reg、`interrupts-extended` から hart→S context、`riscv,ndev`）、UART（`ns16550a`/`snps,dw-apb-uart`: reg、`reg-shift`、`reg-io-width`、`interrupts`）、`/chosen/bootargs`、ccache（`sifive,ccache0`: reg）。parse の上限 2 MiB | bsp-rpi4/fdt.c |
| `sbi.c`/`sbi.h` | `ecall` の薄い包み（§1.2 の extension）、Base で版と拡張の有無を調べて log、DBCN の有無で早期 console を選ぶ | （無し） |
| `percpu.c`/`percpu.h` | `struct riscv64_percpu`（self、logical id、hart id、ready、running_task、switching_from、current_space、active_ack、timer の deadline と ticks、**ipi の理由の word**、S-mode の PLIC context、debug の状態、watch）。kernel 中は `tp` が record を指し、user 中は `sscratch` が指す（trap.S が交換） | percpu.[ch] |
| `smp.c` | hart の一覧（logical 0 = boot hart）、`cpus=N`、`hal_cpu_start_others`（HSM hart_start、ready の待ちは `rdtime` で 1 秒、来なければ FATAL と `cpus=1` の助言）、`riscv64_secondary_main`、`hal_cpu_notify(_mask)`（理由の bit を atomic で立ててから SBI IPI）、`hal_cpu_park`・`hal_cpu_panic_all`（STOP の理由の IPI）、`hal_cpu_idle`（`wfi` → SIE を立てる → trap → SIE を落とす） | smp.c |
| `int.c`/`int.h` | `riscv64_trap`: 割り込み（SSIP→IPI の理由の word を swap して NOTIFY/STOP、STIP→timer、SEIP→PLIC の claim の loop）と例外（§2.4 の表で `hal_trap_cause`/`mode`/address に写し、user なら `kernel_syscall_handler`/`kernel_user_fault_handler`、kernel なら `kernel_sys_fault_handler`）、CIP-453 の符号拡張 | int.c |
| `irq.c`/`irq.h` | IRQ の slot（PLIC の source 1〜ndev）、`hal_irq_register/unregister`、mask/unmask（boot hart の S context の enable bit）、`hal_irq_send_eoi`（claim した番号を complete に書く。ack = 番号 + 1）、affinity は boot hart 固定（arm64 の D1 と同じ）、`hal_irq_set_mode`、MSI は UNSUPPORTED | irq.c |
| `plic.c`/`plic.h` | PLIC の register の読み書き（base・context は DT）、init（全 source の priority 1・enable 0、boot hart の threshold 0、他の hart の threshold 7、claim の空読みで pending を捨てる）、secondary の context の init | bsp-rpi4/gic.c |
| `clock.c` | tick（`HAL_TIMER_FREQUENCY` 1000 Hz、interval = timebase/1000）: Sstc なら `stimecmp`、無ければ SBI set_timer。`hal_rtc_read_counter`（`rdtime`、timebase）、`hal_rtc_read_epoch_time` は false（RTC は後） | bsp-rpi4/clock.c |
| `page.c` | 物理 page の bitmap（DT の `/memory` と `/reserved-memory`、kernel の image、DTB、handoff、boot の table、loader の割り当て、OpenSBI の予約）。UEFI の道では handoff の memory map（ZBL6 v6 の形）が正 | page.c |
| `space.c`/`space.h` | Sv39 の table: system の root（kernel 半分の 256 entry を共有）、user の space ごとの root（下半分）、direct map（1 GiB の leaf）、device の窓（2 MiB の leaf、`hal_space_map_device`）、uncached の窓（Svpbmt がある時だけ）、`hal_space_*`、A/D bit の扱い、TLB の flush（自分は `sfence.vma`、他 hart は SBI RFENCE の remote_sfence_vma。CIP-1200 の間は全体 flush）、`hal_sync_instruction_stream`（`fence.i` ＋ remote_fence_i） | space.c |
| `task.c`/`task.h` | `struct riscv64_task`（arm64 と同じ field: space、run_cpu、target_cpu、stack、resume_sp、fpregs 264 byte、private、tls、active_user_frame、signal の frame ×8、debug）、`hal_task_*`、syscall の frame の出し入れ、signal の入口と戻り、stack の guard と深さの計測 | task.c |
| `debug.c` | `hal_task_get/set_user_{gp,fp,v}regs`、single step と debug point は `HAL_ERR_UNSUPPORTED`（§2.6） | debug.c |
| `asm.c`/`asm.h` | `csr` の読み書きの inline、IRQ の mask/unmask と save/restore、`wfi`、`fence`、`sfence.vma`、`fence.i`、MMIO の `hal_mmio_*`（`fence io,io` を挟む）、`hal_io_inp/outp`（無い: FATAL） | asm.[ch] |
| `lib.c` | `hal_memset/memcpy/strlen`、`hal_printf`、`hal_putc`（`kernel_putc` が無ければ早期 console）、`hal_assert/fatal`、`hal_entropy_fill`（`seed` CSR があれば。無ければ false） | lib.c |
| `watch.c` | arm64 の stall watch の riscv64 版は **p003 では入れない**（後の最適化。arm64 で 4 core の固まりの解析に効いたので Future Work） | watch.c |
| `bsp-virt/board.c` | QEMU virt: 何も要らない事の明示（dcache の操作は no-op、errata 無し） | bsp-rpi4/ |
| `bsp-visionfive2/board.c`・`ccache.c` | VF2: ccache の FLUSH64 で `hal_dcache_*`、`mimpid` の log、（後の WS の）clock/reset の gating の口 | bsp-rpi4/ |
| `src/hal/idle-suspend-unsupported.c` | そのまま使う（S0ix 無し） | 同 |

早期 console と UART: HAL は UART の register に触らず **SBI の console** を使う（OpenSBI の console は virt でも VF2 でも同じ UART0 に出る）。UART の driver（§2.7）は kernel 側にあり、`kernel_putc` が公開された後は HAL の出力もそこへ行く（hal.h の契約）。利点: HAL が UART の間隔や DW の癖を知らずに済み、U-Boot が設定した baud を壊さない。欠点: SBI の console は 1 byte か 1 buffer ごとに ecall で遅い（boot の間だけ）。

### 2.3 CPU・per-CPU・SMP

- **logical CPU の番号**: 0 = boot hart（firmware の `a0`、UEFI の道では `RISCV_EFI_BOOT_PROTOCOL` の値を loader が handoff に入れる）。他は DT の `/cpus` の順で、`mmu-type` があり `status` が disabled でない hart だけ、`cpus=N` の上限（既定は全部）、`RISCV64_CPU_MAX` = 8（VF2 の U74 ×4 と virt の試験に足りる。virt の `-smp` が 8 を超える分は使わない）。hart id ↔ logical の表は `smp.c` が持つ。
- per-CPU の record の見つけ方: kernel の中では `tp`（x4）が record を指す。kernel の C は TLS を使わないので `tp` は空いている（psABI の thread pointer）。user の `tp` は trap の frame に保存し、user へ戻る時に戻す。trap の入口は `csrrw tp, sscratch, tp`: user から来た（sscratch に record が入っていた）なら `tp` = record、kernel から来た（sscratch = 0）なら `tp` = 0 になるので元に戻して続ける。user に戻る直前に `sscratch` に record を書く。
- hart の起動: `hal_cpu_start_others` は logical 1〜 の各 hart に boot block（satp の値、stack の先頭、logical id。64 byte 揃え。cache の clean は要らない: 同じ coherent な memory を読む）を作り `sbi_hsm_hart_start(hartid, phys(riscv64_secondary_entry), phys(block))`。hart は `satp` = 0、`a0` = hartid、`a1` = block の物理 address で入る（§1.2）→ locore が block の satp を書いて（kernel の table は識別 map を持たないので、**block の中に識別 map 付きの boot の table の satp を入れておき**、高い address に跳んでから最終の table に切り替える。arm64 が TTBR0 の boot の識別 map と TTBR1 を同時に使うのと同じ目的）→ `riscv64_secondary_main`（percpu、space の選択、`stvec`、PLIC の自分の context、初期 task、tick、ready の公開、`kernel_secondary_entry`）。
- IPI: 1 本（SSIP）しか無いので理由は per-CPU の word の bit（NOTIFY、STOP）。送る側は bit を `amoor` で立ててから SBI IPI（hart_mask は hartid の bit、base は 0。hart id が 64 を超える機械は対象外）。受ける側は `sip.SSIP` を落とし、word を `amoswap` で 0 にして理由ごとに処理。NOTIFY は `kernel_cpu_notify_handler(cpu, ack)`、ack は `HAL_IRQ_ACK_NONE` でない定数（EOI は何もしない）。
- `hal_cpu_idle`: `sie` に timer・soft・ext を許したまま `sstatus.SIE` = 0 で `wfi` → 戻ったら `csrsi sstatus, SIE`（ここで trap が取られ処理される）→ `csrci sstatus, SIE`。「enable して halt」の原子性は `wfi` の性質（§1.1）で満たす。
- 停止: `hal_cpu_park` は SIE を落として `wfi` の loop。`hal_cpu_panic_all` は他の ready な hart に STOP の IPI（SIE を落として回っている hart には届かない。arm64 と同じ制限）。

### 2.4 trap と kernel への写し

| scause | hal_trap_cause | mode | address | 備考 |
| --- | --- | --- | --- | --- |
| 8（U の ecall） | — | — | — | syscall: 番号 `a7`、引数 `a0`〜`a5`、`sepc += 4`、戻り値 `a0`。`kernel_syscall_handler` → `kernel_user_return_handler` |
| 12 / 13 / 15 | PAGE_FAULT | EXEC / READ / WRITE | `stval`（12 は CIP-453 の符号拡張） | A/D が 0 の leaf への fault は HAL が A/D を立てて再実行（kernel に上げない） |
| 1 / 5 / 7 | OTHER | — | `stval` | access fault（PMP・無い address） |
| 0 / 4 / 6 | ALIGNMENT | — | `stval` | |
| 2 | ILLEGAL_INSN | — | — | FP が Off の時の FP 命令は HAL が FS を Initial にして再実行する案もあるが、p003 は eager な FP で FS=Clean にするので来ない |
| 3 | BREAKPOINT | — | — | `ebreak` |
| 9（S の ecall） | OTHER | — | — | kernel の bug。`kernel_sys_fault_handler` |
| それ以外 | OTHER | — | — | |

kernel から来た例外（`sstatus.SPP` = 1）は `kernel_sys_fault_handler`、user から来た物（SPP = 0）は user frame を立てて `kernel_user_fault_handler`。割り込みは `from_user` を見て user frame を立て、戻りに `kernel_user_return_handler`（arm64 と同じ流れ）。

### 2.5 address 空間（Sv39）

| 範囲（仮想） | 大きさ | 中身 |
| --- | --- | --- |
| 0x0000_0000_0000_1000 〜 0x0000_003f_ffff_ffff | 256 GiB − 4 KiB | user（`hal_space_get_user_range`）。space ごとの root table の entry 0〜255 |
| 0xffff_ffc0_0000_0000 + phys | 128 GiB | **direct map**（RAM だけ。1 GiB の leaf。VF2 の DDR 0x4000_0000〜0x2_4000_0000、virt の DRAM 0x8000_0000〜 を覆う） |
| 0xffff_ffe0_0000_0000 〜 | 64 GiB | device の窓（`hal_space_map_device`、2 MiB の leaf、Svpbmt があれば IO の type、無ければ PMA に任せる） |
| 0xffff_fff0_0000_0000 〜 | 32 GiB | uncached の窓（Svpbmt の時だけ。無ければ `hal_pmem_map_uncached` は UNSUPPORTED） |
| 0xffff_ffff_8000_0000 〜 | 2 GiB | **kernel の image の窓**（`RISCV64_KERNEL_LINK_VIRT_BASE`。link は仮想のここ、物理は loader が選ぶ。amd64 の契約を写す: `physical = virtual − LINK_VIRT_BASE − LINK_PHYS_START + kernel_phys_start`）。`-mcmodel=medany` が要る |

- kernel 半分（root の entry 256〜511）は 1 つの table の列を全 space が共有する（arm64 の `system_kernel_l0` と同じ。root の上半分の entry を space の生成時に複写）。
- user の leaf は U=1、kernel は U=0 G=1。kernel が user の memory を読み書きする時は `sstatus.SUM` = 1 を立てる（`uaccess` の間だけ）。実行可の user page の作成・変更では `hal_sync_instruction_stream`（arm64 の WS044 p009 と同じ理由）。
- A/D bit: 作る leaf に A=1、書ける leaf に D=1 を最初から立てる（Svade の実装でも fault しない）。`hal_space_prot_query`/`hal_space_query` の ACCESSED/DIRTY は PTE の A/D を読む（hardware が立てる機械では意味を持つ。立てない機械では常に 1 と見える: 制限として記録）。
- TLB: 自分の hart は `sfence.vma`（ASID は使わない、arm64 の D2 と同じ）。他の hart へは SBI RFENCE の remote_sfence_vma（hart_mask は space を選んでいる hart、または全部）。user の space の切り替えは自分の hart だけ flush。
- boot の table（locore の `.bss.boot`）: kernel の窓（image の物理を 2 MiB の leaf で）＋ 識別 map（image の物理と DTB/handoff の物理を含む 1 GiB の leaf）。`satp` を書いた直後の命令は識別 map で続く。高い address へ跳んだ後 `arm64` と同じく C で最終の table を作り（direct map など）切り替える。

### 2.6 task・signal・debug・FP

- syscall の ABI: `ecall`、番号 `a7`、引数 `a0`〜`a5`、戻り `a0`（aarch64 の `x8`/`x0`〜`x5`/`svc #0` に対応）。libc の `__syscall6` は `a0` の番号を `a7` に移して引数を詰める。
- signal の frame の head は aarch64 と同じ 16 byte（token、reserved、context_pointer）、restorer は無し（`HAL_TASK_SIGNAL_FRAME_HAS_RESTORER 0`）。libc の `__signal_restorer` は `sp` から token と context を読み、`a7` = 80（sigreturn、aarch64 と同じ番号）で `ecall`、戻ったら `ebreak`。
- TLS: `hal_task_set_tls` は task の user `tp` の値を持ち、user へ戻る frame に入れる。libc は `tp` を直に読む（`static-tls.c` の `__aarch64__` の枝に `__riscv` を足す）。Variant I で `tp` は TCB の終わりの次（§1.7）: libc の static TLS の配置を psABI に合わせる（p007 で `userland/base/libc/static-tls.c` の aarch64 の配置と比べて決める。aarch64 も Variant I だが TCB の大きさ（aarch64 16 byte、riscv64 は TCB を tp の手前に置く約束）が違う）。
- FP: eager（arm64 と同じ）。task の生成時に boot hart が捕った初期の状態（fcsr = 0、f = 0）を複写、切り替えで保存・復元、`sstatus.FS` は user へ戻る時 Clean。kernel の中では FP 命令を出さない（`-mabi=lp64`）。lazy（FS=Off で不正命令の trap を取って復元）は最適化で Future Work。
- debug（ptrace）: `hal_task_get/set_user_gpregs`（x1〜x31 と pc。`struct hal_gpregs { uint64_t x[32]; uint64_t pc; }`、x[0] は 0）、fpregs（f[32]、fcsr）、vregs は空。**single step と hardware の debug point は p003 では UNSUPPORTED**（`HAL_DEBUG_HAS_SINGLE_STEP 0`、`HAL_DEBUG_POINT_MAX 1` で全部の `HAL_DEBUG_KIND_*_OK 0`、`hal_task_set_debug_points` は count 0 だけ受ける）。RISC-V の S-mode には hardware の single step の bit が無く（Sdtrig の trigger は M-mode の委譲が要り OpenSBI は委譲しない）、kernel が `ebreak` を次の命令に置く形は後の Phase。`src/kern/ptrace.c` は `hal_task_set_single_step` の失敗を `ENOTSUP` にするので kernel は壊れない（確かめた）。`HAL_DEBUG_POINT_MAX` は配列の大きさに使われるので 0 にしない。
- stack: kernel の stack は arm64 と同じ 32 KiB（BUG-321 の教訓）、guard 128 byte、fill の計測。

### 2.7 kernel 側（HAL の外、この WS の範囲）

- `src/kern/platform/riscv-virt.c`・`src/kern/platform/visionfive2.c`（README の「platform ごとに 1 つの TU」）: `kern_platform_init`（console の driver の登録、ramdisk の root の公開、FDT の `drv_fdt_open`）、`kern_platform_block_device`、`kern_platform_halt/reboot/poweroff`（SBI SRST）、debug の write（SBI console）。
- **UART の console の driver** `src/drivers/serial/ns16550.c`（新。MMIO の 16550、間隔と access 幅を引数で持ち、DW の busy detect の癖を flag で持つ。pcat の `serial-mirror.c` は port I/O の別物で再利用しない）＋ teletype の console の glue（rpi4-console.c と同じ役の `src/drivers/generic/serial-console.c`: `kern_text_register` の teletype、RX は PLIC の割り込みで tty に入れる。**RX の割り込みが「PLIC の割り込みが動く」の証拠**）。
- **root の媒体**: initrd（§7 の判断 4 の既定）。loader が `rootfs.img`（UFS の image）を同じ FAT から memory に読んで handoff に範囲を渡し、kernel は memory を背にした block device（新 `src/drivers/generic/ramdisk.c`。tree には initrd/ramdisk が無いことを grep で確かめた）を `kern_platform_block_device` で出し、`KERN_PARTITION_SCHEME_LBA`（partition の無い device）で root に mount する。QEMU の直の起動では `-initrd` の `/chosen/linux,initrd-start/end` から同じ物を作る。SD（JH7110 の DW MSHC）・GbE・USB・display は別の WS（§7）。
- build: `platform/riscv64/`（§4）。

## 3. 起動の道

### 3.1 QEMU virt

1. **p003・p008 の試験（UEFI 無し）**: `qemu-system-riscv64 -M virt -smp 4 -m 2G -nographic`（既定の `-bios` = OpenSBI fw_dynamic）`-kernel build/riscv64/vmunix -initrd rootfs.img -append "..."`。kernel は ELF のまま渡し（QEMU は p_paddr に置く。link の物理 0x8020_0000 = firmware の次の 2 MiB 境界）、`a0` = 0、`a1` = DTB で locore の (a) の入口。
2. **製品の道（p004）**: U-Boot `qemu-riscv64_smode`（Debian `u-boot-qemu`。Q1 が `sudo apt install u-boot-qemu`）を `-kernel /usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin` で渡し、disk を `-drive if=none,id=d,file=image -device virtio-blk-device,drive=d`（virtio-mmio。U-Boot の distro boot が virtio を scan する）。U-Boot が ESP の `\EFI\BOOT\BOOTRISCV64.EFI` を読み、DTB は control DT を configuration table に、boot hart は protocol に。zedBSD の loader → `zedbsd.cfg` → `vmunix`・`rootfs.img`（initrd）。
3. edk2（RiscVVirtQemu）は Debian に無く、build が要る → 既定にしない。U-Boot の EFI で足りない物（例: GOP）が出た時の代替（§7 の判断 7）。

### 3.2 VisionFive 2

1. firmware: 板の QSPI に入っている vendor の firmware の U-Boot の版は未確認（古い物は `RISCV_EFI_BOOT_PROTOCOL` を持たない可能性: U-Boot への追加は 2022 年）。**既定の案: Debian の `u-boot-starfive`（2025.01）の `u-boot-spl.bin.normal.out` と `u-boot.itb` を SD の partition 1・2（GUID §1.6）に置き、MSEL を SD にして試す**（flash を書き換えない。動けば後で `sf update` で flash へ）。
2. SD の GPT: 1 spl、2 uboot、3 **ESP**（FAT32: `EFI/BOOT/BOOTRISCV64.EFI`、`zedbsd.cfg`、`vmunix`、`rootfs.img`、`logo.ppm` は不要）、4 UFS の root（SD の driver が来るまで未使用。initrd の間は 3 だけで足りる）。amd64 の `native` layout に 1・2 を足した形を `platform/riscv64/disk-image.mk` で作る（`tools/build` の image の道具は GPT を作れる: amd64 の hybrid/native の道）。
3. U-Boot（S-mode、boot hart は OpenSBI の lottery の 1〜4 のどれか）の bootstd が mmc の ESP を見つけ `BOOTRISCV64.EFI` を実行。DTB は `${fdtfile}` を ESP の `/dtb/` 等に置けば（例 `dtb/starfive/jh7110-starfive-visionfive-2-v1.3b.dtb`、Linux の DTS から dtc で作る物を tree に置くかは判断 8）、無ければ U-Boot の control DT（u-boot.itb の中の DTB、2025.01 は upstream の DTS）が渡る。**既定: control DT を使う**（file を置かない）。
4. loader → kernel（§3.3・3.4）→ UART0 で login。電源の入れ直しは Pi と同じ SwitchBot の plug（§5）。

### 3.3 loader の riscv64 版（`bootloader/uefi/`）

共有する物（ABI に依らない C）: `volume-discovery.c`（FAT の探索）、`zedbsd-config.c`（`zedbsd.cfg`）、`common/boot-override.c`・`common/logo-path.c`・`boot-keys.c`（EFI の protocol だけ）、`memory-map.c`/`memory-map-v6.c`/`common/memory-map.c`（UEFI の memory map → ZBL6 v6 の範囲。`bootstrap_cr3` の field は「bootstrap の page table の root」の意味で riscv64 では `satp` の値を入れる）、`include/uefi.h`（`EFIAPI` を riscv64 では空に。今は x64 の ms_abi の定義: 確かめて `#if` で分ける）、`logo.c`・`video.c`・`framebuffer.c`（GOP があれば使う。**virt と VF2 の U-Boot に GOP が無い時は skip して handoff の framebuffer の flag を落とす**。今の bootx64.c は GOP が無いと `fail_status("Locate GOP")` で止まるので、riscv64 の main では「無ければ無しで続ける」）。

足す物:
- `bootriscv64.c`: `efi_main(image, system)` の riscv64 版。x64 固有（CR3、4 段の table、GOP の stage の描画、ACPI RSDP の探索）を持たず、(1) 設定と kernel の ELF を読む（`elf64.c` を `EM_RISCV` と `riscv64-kernel-image.h` の定数で一般化: machine・LINK_VIRT_BASE・LINK_PHYS_START・MAX_BYTES・ALIGN を arch の header から取る）、(2) **`rootfs.img` を読んで initrd の範囲にする**、(3) `RISCV_EFI_BOOT_PROTOCOL` で boot hart（無ければ `/chosen/boot-hartid`、それも無ければ失敗して止める）、(4) configuration table の DTB を複写、(5) memory map を取り `ExitBootServices`、(6) Sv39 の bootstrap table（識別 map: loader・handoff・kernel の物理、kernel の窓: linked VA → 置いた物理、2 MiB の leaf）を作る、(7) transition。
- `riscv64-handoff.h`（`bootloader/include/`）: ZBL6 の riscv64 版の handoff（UEFI の v7 の形を基に: common、parameters（`kern_boot_parameter_record`）、memory（ZBL6 v6 の範囲と割り当て、`bootstrap_satp`、kernel の物理の範囲）、provenance、**riscv64 の拡張: `boot_hart`、`fdt_phys`・`fdt_size`、`initrd_phys`・`initrd_size`**）。`include/kern/boot.h` の `kern_boot_handoff`（magic/version/size）の共通部を先頭に置き、version は `KERN_HANDOFF_VERSION_MULTIBOOT` と拡張の magic（rpi4 の形）か、新しい version の定数か → 判断 9（boot.h は kernel の公開の ledger で「記録された決定の後にだけ変える」）。
- `pe-header.S`: PE32+ の header（machine 0x5064、subsystem 10 = EFI application、`.text`・`.data` の 2 section、base relocation の directory は空）。手順は Linux の `efi-header.S`（Procedure referenced）と PE/COFF の仕様。**自作し、転記しない。**
- `start.S`: PE の entry。`a0` = image handle、`a1` = system table を保存、**自分の `.rela.dyn`（`R_RISCV_RELATIVE`）を load address で当て**、stack（firmware の）のまま `efi_main` を呼ぶ。`gp` は使わない（`-mno-relax`）。
- `transition.S`: `csrci sstatus, SIE`、`sie` = 0、`satp` = bootstrap（`sfence.vma`）、stack を kernel の boot の stack（handoff の中の stack か loader の確保した page）に、`a0` = boot hart、`a1` = handoff の物理 address、kernel の entry（識別 map にある）へ `jr`。
- build（`platform/riscv64/bootloader.mk`）: `clang --target=riscv64-unknown-elf -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -fPIC -fno-plt -mno-relax -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin ...`、`ld.lld -m elf64lriscv -shared -Bsymbolic -z notext --no-relax -T bootloader/uefi/riscv64-loader.ld`（header を先頭に、`.text`/`.rodata` と `.data`/`.rela.dyn`/`.bss` を 4 KiB に揃えた 2 つの section に）、`llvm-objcopy -O binary → BOOTRISCV64.EFI`、`platform/riscv64/tools/check-bootriscv64.py`（PE の header の整合: machine、section の RVA/size が file の大きさに合う、entry が `.text` の中、`.rela.dyn` に RELATIVE 以外が無い、undefined symbol 無し）。
- ELF と relocation: kernel の `vmunix` は `ET_EXEC`、非 PIC、`-mcmodel=medany`、link は `RISCV64_KERNEL_LINK_VIRT_BASE`（0xffff_ffff_8000_0000）+ 物理の preferred 0x8020_0000。loader は 2 MiB 揃えの物理の置き場を UEFI の memory map から選び（amd64 の `AllocateMaxAddress` の道）、`zbl_elf64_place` で rebase。kernel は relocation を持たない（HAL が窓で吸収）。`vmunix.ld` の assert: image ≤ 16 MiB（loader の leaf の数に合わせる）、`_start` は 4 byte 揃え。

### 3.4 kernel の 2 つの入口と handoff

- (a) SBI の直の起動（QEMU `-kernel`）: `a1` が DTB。cmain は DTB を parse し、自分で `struct riscv64_boot_handoff`（§3.3 の形）を作る: memory は DT の `/memory`−`/reserved-memory`、initrd は `/chosen/linux,initrd-*`、command line は `/chosen/bootargs`、boot hart は `a0`。kernel の物理は link の物理（QEMU が p_paddr に置く）。
- (b) loader: `a1` が handoff。memory は handoff の ZBL6 v6 の範囲（UEFI の map が正。DT の `/memory` は読まない: boot.rst の「UEFI では EFI の map だけ」に合わせる）、`/reserved-memory` は U-Boot が EFI の予約に写している。
- 両方の後は同じ: `hal_get_arch_handoff("boot.command-line")` は parameter record の text（b）か bootargs（a）、`kernel_entry(handoff)`。kernel の platform は handoff の riscv64 の拡張から FDT と initrd を取る。

## 4. build の構成

### 4.1 platform と config

- `platform/riscv64/`: `Makefile`（include の列）、`vmunix.mk`（HAL と kernel の source の一覧。arm64 の vmunix.mk の形。`RISCV64_CPPFLAGS := -nostdlibinc -Iinclude -Isrc -I. -Isrc/hal/riscv64 -DHAL_ARCH_RISCV64 -DHAL_BOARD_VIRT|-DHAL_BOARD_VISIONFIVE2 -DKERN_USER_ABI_RISCV64 -DKERN_USER_ABI_LP64`、`RISCV64_CFLAGS := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror`、LTO は `ZEDBSD_KERNEL_LTO_CFLAGS`）、`vmunix.ld`（`OUTPUT_ARCH(riscv)`、`KERNEL_LINK_VIRT_BASE = 0xffffffff80000000`、`KERNEL_PHYS_LOAD = 0x80200000`、`.text` を `AT(KERNEL_PHYS_LOAD)`、`.text.boot` を先頭、`.bss.boot`、image ≤ 16 MiB の assert）、`user.ld`、`bootloader.mk`（BOOTRISCV64.EFI）、`rootfs.mk`・`disk-image.mk`（§3.2 の GPT。virt 用は ESP ＋ root の 2 partition、VF2 用は spl・uboot を足した 4 partition。board で分ける）、`run.mk`（§3.1 の 2 つの command。QEMU の起動は T1 と Q1 だけが行う）、`toolchain.mk`。
- `Makefile`: `ZEDBSD_PLATFORM` に `riscv64-virt` と `visionfive2` を足し、`ZEDBSD_PLATFORM_DIR` は両方 `riscv64`、`ZEDBSD_ARCHITECTURE riscv64`、`ZEDBSD_BOARD virt|visionfive2`、`ZEDBSD_PLATFORM_TARGET_riscv64-virt := riscv64_virt`、`_visionfive2 := riscv64_visionfive2`、`ZEDBSD_TARGET_TRIPLE riscv64-unknown-zedbsd`、sysroot `build/riscv64/sysroot`、`sysroot-riscv64` の goal。`BUILD := build/riscv64`（board で分けるなら `build/riscv64-virt`。T1 の image の作り分けのため board ごとに分ける案を既定）。
- `tools/menuconfig.py`: `PLATFORMS` に `("riscv64-virt", "riscv64", "virt", "QEMU virt RISC-V 64")`・`("visionfive2", "riscv64", "visionfive2", "StarFive VisionFive 2 RISC-V 64")`、`BOARD_LABELS`、`BOARD_VARIANTS`（default）、`MENU_CPUS` に `("riscv64", "RISC-V 64", "visionfive2", "VisionFive 2")`（virt は Board で選ぶ）、`directory` の表。
- `config/ci/config-riscv64-virt.mk`・`config-visionfive2.mk`（CI の image。`ZEDBSD_USER_PROGRAMS` は最初は base の最小: `init`・`login`・`sh`（既定の shell）・`getty` 相当・`uname`・`ls`・`cat`。rpi4 の一覧から desktop を外す）。
- CI の binary cache: p002 の注のとおり、riscv64 を CI で build する前に RISCV 入りの LLVM の cache の asset（patch level を上げる）が要る（Q1）。

### 4.2 kernel と HAL

- 対象: `vmunix`（HAL の riscv64 ＋ `src/kern/*` ＋ drivers のうち platform に依らない物 ＋ §2.7 の platform と driver）。既存の kernel の C は arch を `KERN_USER_ABI_LP64`・`__aarch64__`/`__x86_64__` の `#if` で見る所がある（`include/uapi/reg.h` は `#error`、`include/libc/setjmp.h`、`userland/base/libc/*.c`）→ `__riscv` の枝を足す（p007・p008 で grep して全部）。
- warning 0 の `make -j16 build/riscv64/vmunix` が build の関門。

### 4.3 userland（libc・rtld・crt・Noct）

- `userland/base/libc/syscall-riscv64.S`（`__syscall6`、`__signal_restorer`）、`src/libc/crt/crt0-riscv64.S`・`crt1-riscv64.S`（`_start`: argc/argv/envp を `sp` から、`gp` の設定（`__global_pointer$`）、`__libc_init`、`main`、`_exit`）、`src/libc/setjmp-riscv64.S` と `include/libc/setjmp.h` の `__riscv` の枝（clang の riscv に `__builtin_setjmp` が無いか確かめてから。aarch64 と同じく自前なら s0〜s11・ra・sp・fs0〜fs11 の 26 word）、`static-tls.c`（`tp` の読み）、`pthread.c`（relax: `pause` は Zihintpause が無い U74 でも `fence w,0` の encoding で合法な hint）、`posix.c`（`machine` = "riscv64"）、`include/uapi/reg.h`（`struct reg { uint64_t r_x[32]; uint64_t r_pc; }`、`struct fpreg { uint64_t fp_f[32]; uint32_t fp_fcsr; }`）、`include/libc/elf.h` の `R_RISCV_*`。
- `src/rtld/rtld.c`: `HAL_ARCH_RISCV64` の `RTLD_MACHINE EM_RISCV`・`RTLD_RELATIVE R_RISCV_RELATIVE`、relocation の switch（64、RELATIVE、JUMP_SLOT、GLOB_DAT は無い（64 を使う）、TLS_DTPMOD64・DTPREL64・TPREL64、IRELATIVE、COPY。TLSDESC は `-mtls-dialect=trad` で出さない）。
- `userland/base/noct/zedbsd-riscv64.cmake`（`CMAKE_SYSTEM_PROCESSOR riscv64`、triple、flags `-march=rv64gc -mabi=lp64d -mno-relax ...`）。`tools/build/check-dynamic-elf.py` の `MACHINES` に `riscv64`（243、ELF64、LE、許す relocation の集合）。`platform/common/userland-*.mk` は platform から flags を受ける形なのでそのまま。
- userland の flags: `-march=rv64gc -mabi=lp64d -mno-relax -fno-plt`（PLT は使ってよいが rtld の実装が簡単な方を p007 で選ぶ）。`-z max-page-size=4096` は lld の riscv の既定が 4096 なので不要。

### 4.4 Noct・package

- host の Noct は変えない。target の Noct（`noct` の user program）は cmake の toolchain file で build（上）。package（`userland/packages/`）は最初の到達目標の外（login に要らない）。

### 4.5 toolchain への依頼（Q1。P2 は触らない）

`toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch` を zedbsd9 に: (1) `clang/lib/Basic/Targets.cpp` の riscv64（と riscv32）の OS の switch に `case llvm::Triple::ZedBSD: return std::make_unique<ZedBSDTargetInfo<RISCV64TargetInfo>>(Triple, Opts);`、(2) `clang/lib/Driver/ToolChains/ZedBSD.cpp` の emulation に `riscv64 → elf64lriscv`、(3) `TripleTest.cpp` に riscv64 の例、(4) `version.mk` の `ZEDBSD_LLVM_PATCH_LEVEL := zedbsd9` と cache の asset。**userland の Phase（p007）の前提。** kernel・HAL・loader の Phase は `riscv64-unknown-elf` ＋ `-DKERN_UAPI_NATIVE -DKERN_KCRT_NATIVE`（§1.8）で進められるので、patch を待たない。差分の具体は p007 の着手の前に P2 が `plan/ws256/proposed/llvm-zedbsd9.md` に書いて Q1 に送る（toolchain の変更はユーザーの許可: 2026-09-28 の規則）。

## 5. 試験の方法

### 5.1 host の試験（P2 が流す。短い物）

- FDT の parser: 手で書いた小さな DTB（dtc は host にある）で `/cpus`（S7 の除外、hart id の表、timebase）、PLIC の context の表（virt の並びと JH7110 の並びの 2 つ）、UART の reg-shift、`/memory` と `/reserved-memory`。
- Sv39 の table の作り方（`space.c` の leaf/table の判定、direct map の index、A/D の扱い）を host の fixture で。
- `elf64.c` の `EM_RISCV` の plan と place（link の定数の header の切り替え）。
- loader の PE の header の checker（python）。
- SBI の呼び出しの encoding（`a7`/`a6` の詰め方）は inline asm なので host では流せない → code review。

### 5.2 QEMU（T1 が流す。P2 は起動しない）

- 段 1（p003）: `-kernel vmunix`（直の起動）、`-smp 4 -m 2G`、合否は HAL の log（SBI の版と拡張、CPU の数、timer の tick、PLIC の UART の RX の割り込みの発火）… **ただし AGENTS.md は serial の log を読んで判定することを禁じる。** 画面も無い。→ 判断 6。既定の案: **`plan/tools/guest/serial.py` で対話して判定する**（`expect 'login: '` → `login` → `run 'uname -m'` の出力が `riscv64`、`run 'cat /proc/cpuinfo'` 相当か `sysctl` で CPU 数 4）。これは「guest の操作はシリアルで対話」の許された形で、log の読み取りではない。boot-test.sh の「login prompt の画面」は virt では撮れない。
- 段 2（p004）: U-Boot の UEFI → loader → kernel → login（同じ判定）。
- 画面での判定が要るなら（判断 6 の (A)）: QEMU virt に `-device VGA`（U-Boot の bochs の display driver で GOP が立つ）＋ kernel の framebuffer の text console（新 `src/drivers/generic/fb-console.c`、rpi4 の font.c の 8x16 を共有）で `boot-test.sh` の `BOOT_MODE=riscv64-virt` を足す。3〜5 LW の追加。
- QEMU の DTB の確認（`-machine dumpdtb=`）は guest を走らせないが、QEMU の起動の規則に触れるので **T1 か Q1 が 1 回だけ取って `plan/ws256/tests/virt.dts` に置く**。

### 5.3 VisionFive 2 の実機（T1/T2）

- 接続: USB-UART（板の 40 pin の UART0、115200）を host に。電源は Pi と同じ SwitchBot の plug（`plan/tools/switchbot.py cycle`）で Q1 が入れ直す（判断 10）。SD は host で書く（Pi と同じ運用）。
- 判定: serial.py の `--socket` は unix socket なので、USB-UART の tty を socket に橋渡しする小さな道具（`socat` か python の 20 行）を `plan/tools/guest/` に足すか、serial.py に `--tty` を足す（Q1 の tools の変更の依頼）。判定は QEMU と同じ対話。
- 受け入れ: 10 回の電源の入れ直しで 10 回 login（WS208 の p005 の形）。SMP 4 core は `cpus=` の無い既定で、timer と PLIC の割り込みは login の成立（RX の割り込み）と tick の進み（`uptime`）で示す。
- 実機の証拠と QEMU の証拠は分けて書く。

## 6. Phase の表（案）

[ws.md](ws.md) の p003〜p006 の ID と意味は保ち、間に p007〜p010 を足す。実行の順は ID の順ではない。LW は 1 LW ≈ 実時間 15〜25 分（master の較正）。粗い（±50%）。

| Phase | 範囲 | 受け入れの条件 | 依存 | LW |
| --- | --- | --- | --- | --- |
| p001 | この設計 | design.md と proposed/ の HAL の差分の案、reviewer 1 回、Q1 の合格 | — | 済 |
| p002 | toolchain の LLVM に RISCV（Q1） | cleared（2026-10-18） | — | 済 |
| **p010**（新、Q1） | LLVM の patch zedbsd9: riscv64 の `__ZEDBSD__` と linker の emulation（§4.5）。CI の cache の asset | `riscv64-unknown-zedbsd` で `__ZEDBSD__` が定義され、`clang -shared` が `elf64lriscv` で link する。ユーザーの許可 | p001 の案 | 1〜2 |
| **p003** | HAL の芯（QEMU virt、直の起動）: `include/hal/arch/riscv64.h`（承認後）、locore・trap・dispatch・cmain・fdt・sbi・percpu・smp・int・irq・plic・clock・page・space・task・debug・asm・lib、bsp-virt、`platform/riscv64/vmunix.{mk,ld}`、`Makefile`・menuconfig の riscv64-virt、`config/ci/config-riscv64-virt.mk`。kernel は `kernel_entry` から root の mount の失敗で止まる所まで | host の試験 PASS、`vmunix` の build が warning 0、T1 の QEMU: 4 hart online、SBI の版の log、tick、`cpus=1` で 1 CPU。判定は §5.2 の段 1（serial の対話。login は無いので HAL の自己診断の command を `-append` で指定して console に答えさせる形: 例 `hal.selftest=1` で tick の数と CPU の数を出し、serial.py の `expect` で読む → これも「対話」の形。判断 6） | p001 の承認 | 26〜34 |
| **p008**（新） | kernel 側: `src/kern/platform/riscv-virt.c`、`src/drivers/serial/ns16550.c`、`serial-console.c`（PLIC の RX）、`ramdisk.c`（initrd）、`KERN_PARTITION_SCHEME_LBA` の root、`include/kern/boot.h` の riscv64 の handoff（判断 9）、`arch` の `#if` の棚卸し（`reg.h`・`signal.h` 他） | build warning 0、T1: `-initrd` の UFS の image を root に mount し `init` を exec しようとする（userland が無いので「init が無い」で止まる。serial で読む） | p003 | 8〜10 |
| **p007**（新） | userland の riscv64: libc の .S（syscall・crt・setjmp）、static-tls・pthread・posix の枝、rtld の relocation、`user.ld`、Noct の cmake、`check-dynamic-elf.py`、sysroot、`ZEDBSD_USER_PROGRAMS` の最小の集合の build、rootfs の image | build warning 0、host の rtld の試験、T1: **QEMU virt で UART の login（直の起動）**（受け入れ 1 の UEFI 以外の全部） | p008、**p010** | 12〜16 |
| **p004** | loader の riscv64（§3.3）、`platform/riscv64/bootloader.mk`・`disk-image.mk`（ESP＋root）、U-Boot `qemu-riscv64_smode` の導入（Q1、apt）、`run.mk` | checker PASS、T1: QEMU virt で **U-Boot → BOOTRISCV64.EFI → vmunix → login**（受け入れ 1 の完成） | p007 | 10〜14 |
| **p005** | VisionFive 2: bsp-visionfive2（ccache の dcache 操作、`mimpid` の log、errata の緩和の有無の記録）、`src/kern/platform/visionfive2.c`、DW UART の癖、`visionfive2` の platform/config/menuconfig、SD の 4 partition の image、U-Boot の SD からの起動の手順、実機の試験の道具（tty の橋渡し、plug）、実機の login | T1/T2: 実機で 10 回 login（SMP 4、tick、RX の割り込み）。QEMU と実機の証拠を分ける | p004、板の接続・電源・UART（ユーザー） | 8〜12 ＋ debug 4〜8 |
| **p009**（新、任意） | 画面での判定: virt の `-device VGA` ＋ GOP ＋ framebuffer の text console、`boot-test.sh` の `riscv64-virt` | `boot-test.sh` が login の画面を撮る | p004。判断 6 で (A) を選んだ時 | 3〜5 |
| **p006** | 規約の全文の見直し（WS が変えた C の全部） | clang-format と全文の照合、build warning 0 | 全部の後 | 2〜3 |

合計（p009 を除く）: 67〜95 LW（うち Q1 の p010 1〜2、実機の debug 4〜8）。順: p003 → p008 → p007（p010 が先に要る）→ p004 → p005 → p006。p003 と p010 は並行できる。

## 7. ユーザーの判断が要る点と既定の案

| # | 点 | 既定の案（P2） | 他の案と理由 |
| --- | --- | --- | --- |
| 1 | 外部の code の参照の方針 | WS245 と同じ: 手順と値は Linux（GPL）・OpenSBI（BSD-2）・U-Boot（GPL）・QEMU（GPL）から block ごとに「Value(s)/Procedure referenced:」を付けて使い、code・comment・構造・symbol の名前は写さない（§0.2）。DTS は GPL-2.0 OR MIT で node・property の名前は規格の語 | 参照を禁じる案は、PLIC の context の並びや DW の癖など文書の無い事実を失う |
| 2 | 板の版と memory | 未決のまま設計は影響を受けない（v1.2A/1.3B の差は GbE だけ、memory は DT/UEFI から読む）。**接続した時に `cat /proc/device-tree/model` 相当（U-Boot の `fdt print /` か板の刻印）で記録** | — |
| 3 | SD・eMMC・GbE・USB・display | **別の WS**（WS256 は UART の login まで。dcache の操作だけ p005 で用意） | 同じ WS に入れると到達目標が二つになる（WS は一つの到達目標） |
| 4 | root の媒体 | **initrd**（loader が `rootfs.img` を memory に読み、kernel の ramdisk を `KERN_PARTITION_SCHEME_LBA` で root に）。QEMU の直の起動は `-initrd`。SD は別 WS | SD を root にすると JH7110 の DW MSHC の driver が WS256 の中に要る（4〜8 LW、実機でしか確かめられない） |
| 5 | Sv39 か Sv48 | **Sv39**（U74 は Sv39 だけ。user 256 GiB、direct map 128 GiB で VF2 の 8 GB と virt に足りる）。定数は `RISCV64_VA_BITS` で持ち Sv48 は Future Work | Sv48 を今やると U74 で動かない物を QEMU で作ることになる |
| 6 | 試験の判定（virt に画面が無い） | **(B) serial.py の対話で判定**（`expect 'login: '`、`login`、`run 'uname -m'`）。AGENTS.md の「serial の log を読んで判定しない」は log の読み取りの禁止で、serial.py の対話は guest の操作として許されている形だが、**判定に使うのは新しい用法なので例外として明示の判断が要る**。(A) `-device VGA`＋GOP＋framebuffer console で `boot-test.sh` の画面（p009、3〜5 LW）。実機は UART しか無いので (B) が要る | (A) だけだと実機の判定ができない。(B) だけだと boot-test.sh の形から外れる。両方（(B) を今、(A) を後）が P2 の推し |
| 7 | QEMU の UEFI の firmware | **U-Boot `qemu-riscv64_smode`（Debian の package、apt で入れる）** | edk2 RiscVVirt は Debian に無く build が要る。U-Boot に GOP が無い（`-device VGA` で bochs の driver がある。要確認）時だけ再考 |
| 8 | VF2 の firmware と DTB | **Debian の `u-boot-starfive` の SPL と itb を SD の partition 1・2 に置いて MSEL=SD で起動**（flash を触らない）。DTB は U-Boot の control DT（file を置かない） | 板の vendor の firmware が EFI の boot protocol を持つなら flash のままでも動く（板で `version` を見てから）。DTS の file を tree に入れる案は GPL-2.0 OR MIT の file の配置の決定が要る |
| 9 | `include/kern/boot.h` の handoff | **rpi4 と同じ形**: version は `KERN_HANDOFF_VERSION_MULTIBOOT`、riscv64 の拡張の magic と `struct riscv64_boot_handoff`（fdt・initrd・boot_hart・ZBL6 v6 の memory）。boot.h は「記録された決定の後にだけ変える」ledger なので、この行がその記録 | 新しい version の定数を足す案は `kernel_entry`・`vfs.c` の version の分岐を触る |
| 10 | 実機の試験の接続 | USB-UART を host に、電源は SwitchBot の plug（Pi と同じ運用、Q1 が入れ直す）、SD は host で書く。serial.py に tty の口（Q1 の tools） | — |
| 11 | kernel の FP の方針 | eager（arm64 と同じ）。kernel の C は FP 無し（`-mabi=lp64`） | lazy は最適化。後 |
| 12 | 板の HDMI の display | 範囲外（別 WS）。login は UART | — |
| 13 | HAL の API の差分の承認 | [proposed/hal-riscv64.md](proposed/hal-riscv64.md) の 2 件（`include/hal/arch.h` の 1 行、`include/hal/arch/riscv64.h` の新規）。hal.h は不変 | — |
| 14 | toolchain の patch zedbsd9 | §4.5。Q1 がユーザーの許可を得て行う（p010） | 待たずに kernel を `riscv64-unknown-elf` で進める（§1.8） |

## 8. 選ばなかった案と理由

- **HAL が UART を直に叩く早期 console**（rpi4 の形）: SBI の console があり、virt と VF2 で UART の間隔と DW の癖が違い、U-Boot の設定した baud を壊したくない → SBI DBCN を使う。`kernel_putc` の後は kernel の driver。
- **CLINT を S-mode から触る**: M-mode の資源で PMP で守られている。SBI TIME/IPI を使う。
- **hart の lottery（全 hart を一斉に kernel に入れる）**: OpenSBI は非 boot hart を STOPPED に持ち、HSM で順に起こすのが規格の推し（boot.rst の「ordered booting」）。
- **ASID**: arm64 の D2 と同じく使わない（切り替えで全体 flush）。Future Work。
- **kernel を direct map の中に置く（arm64 の形）**: loader が物理を選ぶ UEFI の道では amd64 の「窓」の契約が合う。QEMU の直の起動では link の物理に置かれ退化する。
- **lld-link で PE を作る**: LLVM 23 は riscv64 の COFF を出せない（確かめた）→ 自作の PE の header。
- **edk2 を QEMU の UEFI に**: Debian に無く build が重い。U-Boot で足りる。
- **SD を root に**: driver が WS256 に入る。initrd で login を先に。
- **共有の FDT parser（`src/hal/common/`）**: arm64 の fdt.c の移動は WS256 の範囲外（arm64 を変える）。riscv64 は自前の小さい parser。共有化は Future Work。
- **stall watch（arm64 の watch.c）を最初から**: 4 core の固まりの解析に効いた道具だが、p003 の芯には要らない。SMP の試験で固まりが出たら入れる。
- **Svpbmt/Zicbom の前提**: U74 に無い。DT で有無を見て使う。
- **single step の hardware 実装**: S-mode では無い。ptrace の step は後（kernel の `ebreak` の挿入）。

## 9. 失敗と回復・並行性・資源（設計の要点の確認）

- 失敗: 秒の timeout で来ない hart は FATAL と `cpus=1` の助言（arm64 D6）。SBI の拡張が無い（probe 失敗）なら: TIME/IPI/HSM/RFENCE は必須で FATAL（OpenSBI 1.6 は全部ある）、DBCN が無ければ legacy の putchar、SRST が無ければ legacy の shutdown か `wfi` の loop。PLIC・UART が DT に無ければ FATAL（message は SBI console に出る）。loader: boot hart の protocol が無ければ `/chosen/boot-hartid`、それも無ければ止める（間違った hart id で HSM を呼ぶと他 hart が起きない）。
- 並行性と lock: arm64 と同じ所に同じ lock（IRQ の slot の spin、pmem、space、task list、PLIC の enable の読み書き）。lock の順も arm64 に合わせる。`hal_atomic_uint_try_acquire` は `amoswap.w.aq`、`hal_atomic_relax` は `pause`（`fence w,0` の encoding。U74 でも合法）。
- 割り込みと DMA: 装置の割り込みは boot hart の context だけで受ける（分散は Future Work）。DMA の coherency は VF2 の ccache で（§1.6）。
- 資源の所有と寿命: HAL の静的な表（percpu ×8、IRQ の slot ×ndev、boot block ×8）は kernel と同じ寿命。task・space・table の page は arm64 と同じ所有（kernel_alloc と hal_pmem_alloc）。DTB と initrd と handoff の物理は page.c が予約し、kernel の platform が使い終わっても解放しない（単純さ優先。initrd の解放は Future Work）。

## 10. レビューと改訂

- 改訂 1（2026-10-18 P2 初版）。design-reviewer の指摘とその扱いは §11 に足す。
