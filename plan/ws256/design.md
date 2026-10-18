<!-- awesome-plan project=zedbsd record=ws256-p001-design -->

# WS256 の設計: riscv64 の HAL と StarFive VisionFive 2（JH7110）への移植

2026-10-18 P2（q1243、[ws256-p001](phase001/phase.md)）。改訂 2（q1243-i02、[design-review-1.md](design-review-1.md) の全件を反映。対応の表は §11）。親: [WS256](ws.md)。code は書かない。
HAL の API（`include/hal/`）の差分は [proposed/hal-riscv64.md](proposed/hal-riscv64.md) に、kernel の boot の ledger（`include/kern/boot.h`）の差分は [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) に案として置き、ユーザーの事前承認・決定の前は適用しない。
外部の code（Linux・OpenSBI・U-Boot・QEMU）は手順と値を学ぶために読んだだけで、この文書にも zedBSD の code にも code・comment・構造を写さない（§0.2）。

ユーザーの決定（[ws.md](ws.md)、2026-10-18）のうちこの設計が前提にする物:
1. 最初の到達目標は **UART の console の login**（S-mode の kernel が OpenSBI の上で起動、SMP 4 core、timer と PLIC の割り込み、root は initrd か SD）。
2. 起動の道は **U-Boot の UEFI ＋ zedBSD の loader**（bootloader/uefi の riscv64 版）。DTB は U-Boot から。
3. toolchain の LLVM に RISCV の target を足すのは Q1（p002 cleared）。
4. 最初は **QEMU の virt** で HAL の芯を作り、実機は後（板は未接続）。
5. 起動の確認は **virt に画面を付ける**: QEMU の virt に ramfb か virtio-gpu を付け、`boot-test.sh` の画面で判定する。serial の log で判定する例外は作らない（§5 で ramfb を選び、framebuffer の console の出所と Phase ごとの判定を決めた）。
6. 外部の code の参照は **WS245 と同じ**（§0.2 を決定として書いた）。

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

### 0.2 ライセンスと転記の扱い（決定: 2026-10-18 ユーザー「WS245 と同じ」）

ユーザーの決定（[ws.md](ws.md) 6、クリック）: 「WS245 と同じ。手順は参考にし、code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced: を書く」。この節はその決定の適用で、判断の点ではない（§7 の #1 は決定済み）。

- guardrail「MIT・GPL の出典のデータの引用」（2026-10-15、全部の WS）のとおり: register の offset・bit・IRQ 番号・CSR の番号・GUID・表は**データ**として引用し、zedBSD の code では block ごとに直前の複数行 comment で「Value(s) referenced: <版> <path> (<define・表・node の名前>)」、手順は「Procedure referenced: <版> <path> (<関数の名前>)」と書く。file の先頭には出典・派生の宣言・SHA を一切書かない。
- **code・comment・構造・symbol の名前は、GPL でも MIT でも BSD でも写さない。** Linux の `_PAGE_*`・`SBI_EXT_*`・`CSR_*`、OpenSBI の `sbi_*`、QEMU の `virt_memmap`、U-Boot の `efi_*` の名前を zedBSD に持ち込まない。zedBSD の riscv64 の名前は `riscv64_*`（HAL の内部）、`RISCV64_*`（macro）、BSP は `vf2_*`・`virt_*`。
- DTS（GPL-2.0 OR MIT）の node の名前・property の名前は Devicetree の binding（規格）であり、HAL の parser が探す文字列（`riscv,isa-extensions`・`mmu-type`・`interrupts-extended`・`reg-shift` など）として使う。これは規格の語で転記ではない。
- RISC-V の文書（CC-BY 4.0）の語（CSR の名前、SBI の extension の名前）は規格の語として使う。
- zedBSD の新しい code は全部 Zlib。参照した GPL の file から値を引用しても、明示した引用なら license に影響しない（guardrail 2026-10-15 の WS039 への適用の文と同じ。上の決定で WS256 にも確定）。
- source に開発の管理情報（WS・Phase・Queue・試験の回・QEMU の話・試験の進め方）を書かない（guardrail 2026-10-15・10-17）。この文書の中の話は plan/ に置く。具体には: `include/hal/arch/riscv64.h` の comment に QEMU の machine の名前を書かない（timebase は「device tree の値」と書く）、試験のための boot parameter（初版の `hal.selftest=1`）は作らない（§5 の判定は普通の boot log の行で行う）。
- QEMU の `docs/specs/fw_cfg.rst`（fw_cfg の register と DMA の形）と `hw/display/ramfb.c`（ramfb の設定の形）は GPL-2.0-or-later の文書・code。値（offset・bit・file の名前 `etc/ramfb`・設定の field の並びと byte order）はデータとして「Value(s) referenced:」で引用し、code は写さない。

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
- 使う extension（ID）: Base 0x10（FID 0 spec version、3 probe_extension、4〜6 mvendorid/marchid/mimpid）、**TIME** 0x54494D45（FID 0 set_timer(u64 stime)）、**IPI** 0x735049（FID 0 send_ipi(hart_mask, hart_mask_base)）、**RFENCE** 0x52464E43（FID 0 remote_fence_i、1 remote_sfence_vma、2 remote_sfence_vma_asid。hart_mask は IPI と同じ形）、**HSM** 0x48534D（FID 0 hart_start(hartid, start_addr, opaque)、1 hart_stop、2 hart_get_status。状態 0 STARTED、1 STOPPED、2 START_PENDING、3 STOP_PENDING）、**SRST** 0x53525354（FID 0 system_reset(type 0 shutdown / 1 cold reboot / 2 warm reboot, reason 0)）、**DBCN** 0x4442434E（FID 0 console_write(num_bytes, base_addr_lo, base_addr_hi)、1 console_read、2 console_write_byte(byte)。**base_addr は物理 address**（規格）。HAL は direct map の仮想 address を物理に戻して渡す）。legacy（v0.1、`a7` だけ、`a6` 無し）: 0x00 set_timer、0x01 console_putchar、0x02 console_getchar、0x08 shutdown。
- entropy: `seed` CSR（Zkr）は拡張の無い hart では不正命令の trap になるので、DT の `riscv,isa-extensions` に `zkr` がある時だけ読む（U74・QEMU の既定の rv64 には無いと推定 → `hal_entropy_fill` は false）。counter: `rdtime` は U-Mode で `scounteren.TM`（bit 1）が 1 の時だけ許される。user に時計を許すかは kernel の `clock_gettime` の経路が syscall なので p003 では 0 のまま（推測: U74 の `rdtime` は M-mode の emulation で遅い可能性。user の直読みは Future Work）。
- OpenSBI v1.6 の振る舞い（source で確かめた）:
  - **非 boot hart は OpenSBI の中で HSM の STOPPED の状態で待つ**（`sbi_hsm.c`: cold boot の hart だけ START_PENDING→STARTED、他は STOPPED で `wfi` の loop）。次の段（U-Boot・kernel）に入るのは boot hart だけで、他の hart は次の段が HSM hart_start を呼ぶまで出てこない。cold boot の hart は lottery（最初に atomic swap した hart）か fw_dynamic の boot_hart で決まる。
  - hart_start の前提: 状態が STOPPED でなければ error（STARTED なら -6、他は -3）。始まった hart は `start_addr` に **S-mode、`a0` = hartid、`a1` = opaque、`satp` = 0、`sie` = 0、`sscratch` = 0、`stvec` = start_addr** で入る（`sbi_hart.c` の mode の切り替えで CSR を書いてから `mret`）。
  - fw_dynamic の情報（`fw_dynamic.h`、`a2` で前の段から受ける）: magic 0x4942534f（"OSBI"）、version 2、next_addr、next_mode（0 U、1 S、3 M）、options、boot_hart（-1 なら lottery）。
  - OpenSBI は自分の firmware の領域を PMP で M-mode 専用に守る（S-mode から触ると access fault）。DTB の `/reserved-memory` にその領域（`mmode_resv*`）を足す。HAL はこれを予約として読む。
- SBI の版: OpenSBI 1.6 が報告する spec version は起動時に Base FID 0 で読んで log に出す（p003 の受け入れの 1 つ。推定では 2.0）。SBI v3.0（2025-07 ratified）は OpenSBI 1.6 の後。

### 1.3 PLIC と CLINT/ACLINT

出典: PLIC の register の配置は QEMU `include/hw/riscv/virt.h` の `VIRT_PLIC_*` と Linux `irq-sifive-plic.c` の define（両方同じ値。規格は riscv/riscv-plic-spec）。

- PLIC の register（base からの offset）: priority `0x0000 + 4*irq`（irq 1〜、0 は無い）、pending `0x1000 + (irq/32)*4`、**context ごとの enable** `0x2000 + ctx*0x80 + (irq/32)*4`、**context ごとの threshold** `0x200000 + ctx*0x1000 + 0`、**claim/complete** `0x200000 + ctx*0x1000 + 4`（読むと最優先の pending の irq 番号を claim、0 は無し。同じ値を書くと complete）。priority は QEMU で 3 bit（1〜7）。threshold 0 で全部通す、7 で全部止める。
- **complete は、その source がその context で enable されていない時は黙って捨てられる**（PLIC の規格の文。Linux の `irq-sifive-plic.c` は mask を enable bit でなく **priority 0 への書き込み**で行い（`plic_irq_mask`/`plic_irq_unmask` は priority に 0/1 を書く）、enable が落ちている時の EOI は一時的に enable してから complete を書く（`plic_irq_eoi`）: 2026-10-18 に 6.8.12 の source で確かめた）。→ zedBSD の `hal_irq_mask` は **priority を 0 に**、`hal_irq_unmask` は **priority を 1 に**する（§2.2 irq.c。enable bit は登録の時に立て、以後は触らない）。handler の中で mask された source の complete も enable が立っているので受け付けられ、「二度と来ない」事故を避ける。priority 0 の source は threshold 0 でも claim されない（0 ≤ 0）。init は全 source の priority 0・enable 0 から始め、`hal_irq_register` で enable を立て、unmask で priority 1。
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

- firmware: `-bios` の既定は OpenSBI の `opensbi-riscv64-generic-fw_dynamic.bin`（host に有る）。DRAM の先頭 0x80000000 に置かれ、`-kernel` の S-mode の payload は firmware の終わりを 2 MiB に揃えた所（＝**0x80200000**、`riscv_calc_kernel_start_addr`: 2026-10-18 に v10.0.0 の `hw/riscv/boot.c` で確かめた）に置かれる（flat な image の時。**ELF は `load_elf_ram_sym` で自分の物理 address に置かれ、QEMU は「一番低い load address」を kernel の始まりとして使う**）。**DTB は `align_down(DRAM の終わり − DTB の大きさ, 2 MiB)`**（RV64。`riscv_compute_fdt_addr`、確かめた）＝ **RAM の最後の 2 MiB の近く**（`-m 2G` なら 0xffe0_0000 前後の 2 つ目の GiB の中）。`-append` は `/chosen/bootargs` に入る。`-initrd` は **kernel の低い address + min(RAM/2, 512 MiB)**（`riscv_load_initrd`、確かめた）に置かれ、`/chosen/linux,initrd-start/end` に書かれる。→ locore の boot の table は **RAM 全体**を識別 map で覆う（§2.5。1 GiB の leaf 1 つでは DTB に届かない: M6）。
- `-kernel` の時 OpenSBI は fw_dynamic の next_addr = kernel の低い load address、next_mode = S、**boot_hart = 0**（`riscv_rom_copy_firmware_info` が 0 を書く: 確かめた）で、**kernel は boot hart 0 だけで `a0` = 0、`a1` = DTB の物理 address、`satp` = 0 の S-mode から始まる**。他の hart は HSM の STOPPED。→ virt では boot hart ≠ 0 の道は試せない（§5.2 の sifive_u）。
- CPU: `-cpu rv64`（既定、H 拡張あり）。DT の `mmu-type` は QEMU が支える最大（Sv57）になるが、`satp` に Sv39 を書くのは許される。拡張の property（v10.0.0 `target/riscv/cpu.c` の `riscv_cpu_extensions[]`、2026-10-18 に確かめた）: **`sstc` は既定 true、`svpbmt` は既定 false**（初版の「rv64 は Svpbmt を持つ」は誤り）。property の名前は小文字の拡張名で、`-cpu rv64,sstc=off` のように切れる（on/off・true/false は QEMU の bool の property の書き方）。V 拡張の既定は未確認（推測: off）。HAL は V の有無に依らず `sstatus.VS`（bits 10:9）を Off に保つ（V が無ければ bit は読み取り専用の 0 で書いても無害）。`riscv,isa-extensions` の実際の並びは `-machine virt,dumpdtb=` の DTB で確かめる（T1/Q1、guest は走らない）。
- machine の option: `aia=none`（既定、PLIC）、`aclint=off`（既定、CLINT）、`acpi=on`（既定。DT も渡される。zedBSD は DT だけを読む）。
- **fw_cfg と ramfb**（2026-10-18 に v10.0.0 の `hw/riscv/virt.c`・`hw/riscv/Kconfig`・`hw/display/ramfb.c`・`docs/specs/fw_cfg.rst` で確かめた）: virt は fw_cfg を **DMA 付き**で持つ（`fw_cfg_init_mem_wide(base+8, base, 8, base+16, …)`、base 0x10100000: **data は +0（8 byte 幅）、selector は +8（16 bit、big-endian）、DMA の address は +16（64 bit、big-endian）**。`config RISCV_VIRT` は `select FW_CFG_DMA`）。virt は `TYPE_RAMFB_DEVICE` を dynamic な sysbus device として許す（`machine_class_allow_dynamic_sysbus_dev`）ので **`-device ramfb` が使える**。ramfb は fw_cfg の file **`etc/ramfb`** に guest が設定を**書く**（`fw_cfg_add_file_callback` の書き込みの callback。device は「fw_cfg with DMA」を要る）: 設定は 28 byte の packed な構造 **{u64 addr, u32 fourcc, u32 flags, u32 width, u32 height, u32 stride}、全部 big-endian**。fourcc は DRM の format（XRGB8888 = `XR24` = 0x34325258 を使う。32 bpp）、stride 0 なら width×4。width・height は 16 以上（上限は bochs の VBE の最大）。書くと QEMU はその guest の物理 memory を display の surface にして表示する。fw_cfg の DMA の制御の構造 {u32 control, u32 length, u64 address}（big-endian。control の bit 0 error、1 read、2 skip、3 select（上位 16 bit が selector）、4 write）を物理 address で DMA register に書く。file の selector は directory（key 0x19: u32 count、64 byte の entry {u32 size, u16 select, u16 reserved, char name[56]}）から `etc/ramfb` を探して取る。signature key 0x0000 は "QEMU"、key 0x0001 の bit 1 が DMA の有無。→ §5 の framebuffer の console はこれで作る（HAL の bsp-virt）。virtio-gpu（`imply VIRTIO_VGA`、PCIe の `virtio-gpu-pci`／mmio の `virtio-gpu-device`）も付けられるが virtqueue と 2D の command の driver が要る（§8）。
- UEFI の firmware の選択（§3.1）: (a) U-Boot `qemu-riscv64_smode`（Debian `u-boot-qemu` 2025.01-3+deb13u1、`/usr/lib/u-boot/qemu-riscv64_smode/{u-boot.bin,uboot.elf}`。**host に未導入**）を `-kernel` で渡す。defconfig（v2025.01 `configs/qemu-riscv64_smode_defconfig`、全 25 行、2026-10-18 に確かめた）には VIDEO・PCI・VIRTIO の行が無いが、board の Kconfig（`board/emulation/qemu-riscv/Kconfig`、同日確かめた）が `imply VIDEO`・`imply VIDEO_BOCHS`・`imply VIDEO_SIMPLE`・`imply PCI`・`imply PCIE_ECAM_GENERIC`・`imply VIRTIO_MMIO/PCI/BLK`・`imply NVME_PCI`・`imply USB_XHCI_PCI` を持つ → Debian の binary は **PCI の bochs-display（`-device bochs-display`／`VGA`）の driver を持ち、その device を付けた時だけ EFI の GOP を出す**と推定（`imply` は defconfig で切られていなければ y。binary の中身は未確認: p004 で `strings` か T1 の 1 回の起動で確かめる）。**ramfb の driver は U-Boot に無い**（imply の一覧に無い。`VIDEO_SIMPLE` は DT の simple-framebuffer で、QEMU は ramfb にそれを作らない）→ ramfb を付けた時、U-Boot と loader の間は画面が空で、kernel 側（HAL）が ramfb を設定した所から表示される。設計は GOP に依らない（§5。GOP は §8 の選ばなかった案）。(b) edk2 RiscVVirtQemu（`RISCV_VIRT_CODE.fd`/`VARS.fd` を pflash0/1 に）。Debian trixie に `qemu-efi-riscv64` は無い（`apt-cache policy` に候補無し）ので自分で build する必要がある。

### 1.6 JH7110 と VisionFive 2

出典: Linux 6.8.12 の DTS（`jh7110.dtsi`・`jh7110-starfive-visionfive-2.dtsi`・v1.2a/v1.3b）、StarFive TRM の memory map、`errata/sifive/errata.c`、`drivers/cache/sifive_ccache.c`、U-Boot `doc/board/starfive/visionfive2.rst`・`starfive_visionfive2_defconfig`。

- CPU: **hart 0 は SiFive S7**（`rv64imac_zba_zbb`、MMU 無し、DTS で `status = "disabled"`）、**hart 1〜4 は SiFive U74-MC**（`rv64imafdc_zba_zbb`、`riscv,isa-extensions` = i m a f d c zba zbb zicntr zicsr zifencei zihpm、`mmu-type = "riscv,sv39"`、`tlb-split`、I/D cache 各 32 KiB・64 byte line）。**Sstc・Svpbmt・Zicbom・Zihintpause は無い**。`timebase-frequency` = **4000000**。
  → HAL は `/cpus` の中で `mmu-type` を持ち `status` が disabled でない cpu だけを数える（S7 を除く）。OpenSBI の boot hart は lottery で 1〜4 のどれか（U-Boot の文書の log では 2）。**boot hart が 0 でないので、logical CPU 0 = boot hart、hart id ≠ logical id**（§2.3）。
- CLINT 0x2000000（size 0x10000、M-mode）。**L2 cache controller（SiFive ccache）0x2010000**（size 0x4000、2 MiB、64 byte line、`FLUSH64` register offset 0x200: 64 byte line の物理 address を書くとその line を flush）。PLIC 0xc000000（size 0x4000000、`riscv,ndev` = 136、context の並び §1.3）。
- UART0 0x10000000（§1.4）、I2C 0x10030000〜、SPI 0x10060000。TRM の map: SYSCRG 0x13020000、SYS SYSCON 0x13030000、IOMUX 0x13040000、QSPI CSR 0x13010000、AON CRG 0x17000000、AON SYSCON 0x17010000、AON GPIO 0x17020000、SDIO0 0x16010000、SDIO1 0x16020000、GMAC0 0x16030000、GMAC1 0x16040000、DW DMAC 0x16050000、USB 0x10100000、PCIe0/1 CSR 0x2b000000/0x2c000000、PCIe の 32 bit の memory 0x30000000/0x38000000、PCIe の memory 0x9_0000_0000〜、Boot ROM 0x2a000000、**DDR 0x40000000 から（最大 8 GiB、0x2_3fff_ffff まで）**。
- memory: DTS は `memory@40000000` に 4 GiB と書き、U-Boot が板の実際の量（2/4/8 GB の品がある）で `/memory` を直す（U-Boot は `CONFIG_ID_EEPROM=y` で板の情報を EEPROM から読む）。HAL は DT の `/memory` と `/reserved-memory` だけを信じる（UEFI の道では UEFI の memory map が正）。
- 板の版: **v1.2A と v1.3B** の DTS の差は GMAC1 の phy-mode（1.2A は RMII）と PHY の遅延の値だけ。UART の login には関係しない。U-Boot は `$fdtfile` を版で選ぶ。
- 起動の段（U-Boot の文書 `doc/board/starfive/visionfive2.rst`、2026-10-18 に v2025.01 の原文で確かめた）: BootROM → **SPL**（`u-boot-spl.bin.normal.out`。SD では **type GUID 2E54B353-1271-4842-806F-E436D6AF6985 の partition を BootROM が探す。partition の番号は自由**（文書「You are free to choose any partition number」）、byte の offset は文書に無い。QSPI では offset 0x0。SPL は u-boot.itb を **partition 2** から読む（`CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION=0x2`）。文書の例の GPT: p1 spl 4096〜8191 sector、p2 uboot 8192〜16383、p3 system（EBD0A0A2-…の Linux の type）16384〜）→ **u-boot.itb**（OpenSBI `fw_dynamic.bin` ＋ U-Boot ＋ DTB、GUID BC13C2FF-59E6-4262-A352-B275FD6F7172 の partition か QSPI の 0x100000）→ **U-Boot（S-mode、DRAM）**。OpenSBI は `FW_TEXT_START=0x40000000`（DDR の先頭）、SPL は 0x40000000 に置く（defconfig の `SPL_OPENSBI_LOAD_ADDR`）、OpenSBI の次の段（U-Boot）は 0x40200000（文書の log の `Domain0 Next Address`）。**文書の log の boot hart は 2**（`Boot HART ID: 2`、`Domain0 HARTs: 0*,1*,2*,3*,4*`）: lottery で 1〜4 のどれかになり、hart id ≠ logical id（§2.3）。boot の切り替え MSEL[1:0]: `10` SD、`00` flash。U-Boot の `SYS_LOAD_ADDR` = 0x82000000。defconfig は `RISCV_SMODE`・`BOOTSTD_DEFAULTS`（EFI の boot method を含む）・`OF_BOARD`。**Debian trixie の `u-boot-starfive`（2025.01-3+deb13u1）に `u-boot-spl.bin.normal.out`・`u-boot.itb` が入っている**ので、自分で build しなくてよい。
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
- `-march=rv64imac_zicsr_zifencei -mabi=lp64` の C の object と `-march=rv64gc -mabi=lp64` の .S（`fsd` を含む）の object は `e_flags` が同じ（RVC、soft の float ABI）で `ld.lld` が link する（確かめた）。`pause` の mnemonic は `-march` に Zihintpause が無くても assembler が受け、encoding は 0x0100000f（確かめた）。`__builtin_setjmp` は riscv64 では使えない（確かめた、§4.3）。
- `include/uapi/hosted.h`・`include/kern/kcrt.h` は `__ZEDBSD__` の無い compiler のために `KERN_UAPI_NATIVE`・`KERN_KCRT_NATIVE` の define を持つ（sparcv9 の GCC の道）。→ kernel と HAL の Phase（p003・p008）は toolchain の patch を待たずに `--target=riscv64-unknown-elf` ＋ これらの define で build できる。**userland（p007）には `riscv64-unknown-zedbsd` が `__ZEDBSD__` を定義し linker の emulation が正しいことが要る** → §4.5 の toolchain の依頼。

## 2. HAL の riscv64 の構成

### 2.1 方針

- **S-mode・SBI の上の HAL**。M-mode の資源（CLINT・PMP・mhartid）に触れない。timer は Sstc か SBI TIME、IPI は SBI IPI、他 hart の fence は SBI RFENCE、起動は SBI HSM、reset/poweroff は SBI SRST、早期 console は SBI DBCN（無ければ legacy の putchar）。
- **DT で動く芯**: hart の一覧と id、timebase、PLIC の base と context、UART の base/間隔/IRQ、memory と予約は全部 DT から読む。board（BSP）に残るのは DT に無い物だけ: VF2 の ccache の flush、errata の緩和の有無、将来の clock/reset の gating。QEMU virt の BSP はほぼ空。
- **arm64 と同じ file の分け方と kernel との契約**。HAL の API（hal.h）は変えない。arch の header `include/hal/arch/riscv64.h` を足し `include/hal/arch.h` に 1 行足す（承認が要る: [proposed/hal-riscv64.md](proposed/hal-riscv64.md)）。
- **Sv39 を既定**（U74 は Sv39 だけ。QEMU も Sv39 を受ける）。address の定数は `RISCV64_VA_BITS` の macro で持ち、Sv48 は Future Work（§7 #5）。
- **VF2 に無い拡張は WS256 では使わない**（M12 (1)、ユーザー 2026-10-18 既定）: timer は **常に SBI TIME**（Sstc の `stimecmp` は使わない。QEMU で Sstc の道だけが動いて実機で初めて SBI の道を通る事を避ける）、**Svpbmt は使わない**（`hal_pmem_map_uncached` と `HAL_SPACE_WC`・`NOCACHE`・`WRITETHRU` は virt でも VF2 でも `HAL_ERR_UNSUPPORTED`。cache の属性は PMA 任せ）、Zicbom・Zihintpause の命令は出さない（`pause` は hint の encoding なので合法、§9）。DT の `riscv,isa-extensions` は読んで log に出すだけ（Sstc・Svpbmt を後で使う時の Future Work の材料）。
- kernel の C は **FP を使わない**（`-mabi=lp64`、`-march=rv64imac_zicsr_zifencei`）。FP の保存と復元の .S だけ `-march=rv64gc`（ABI は lp64 のまま。lld は float ABI の違う object の混在を拒むので ABI は揃える）。user は `lp64d`。
- 2 つの入口（§3.4）: (a) **SBI の直の起動**（QEMU の `-kernel vmunix`、`a0` = hartid、`a1` = DTB）、(b) **loader からの起動**（`a0` = hartid、`a1` = handoff の物理 address、handoff の先頭が `ZBL6_HANDOFF_MAGIC`）。locore が `a1` の先頭 4 byte で見分ける（DTB の magic 0xd00dfeed は big-endian で `d0 0d fe ed`、`ZBL6_HANDOFF_MAGIC` 0x364c425a は little-endian で `5a 42 4c 36`。衝突しない。初版の `KERN_HANDOFF_MAGIC` 0x48323842 の byte 列「42 28 32 48」は誤記で、正しくは `42 38 32 48`。いずれにせよ loader→HAL の magic は ZBL6 に改めた: §3.4）。両方の入口で **MMU は off（`satp` = 0）、物理の `_start` に入る**（loader は自分の table を作らず `satp` を 0 にして跳ぶ: §3.3）。(a) は p003 の試験と QEMU の回帰の近道、(b) が製品の道。

### 2.2 file の一覧と責務（`src/hal/riscv64/`）

| file | 責務 | arm64 の対応 |
| --- | --- | --- |
| `locore.S` | 入口 `_start`（`fence.i` を最初に（loader が写した命令の後、I/D 分離の U74 のため: M5）、§2.1 の 2 つの入口の見分け、`gp` は使わない、`sstatus.SIE`=0・`sie`=0・`sscratch`=0・`stvec`=早期の trap（止まるだけ）、boot の stack、BSS の零化、boot 用の静的な Sv39 の table（kernel の窓 ＋ **RAM 全体の識別 map**: §2.5）で `satp` を書いて高い address へ跳ぶ）、`riscv64_secondary_entry`（HSM で始まった hart。前提は SBI の保証だけ（`satp`=0、`a0`=hartid、`a1`=opaque）: 入口で `fence.i`・`stvec`・`sscratch`=0・`sie`=0 を自分で設定し、`a1` の boot block から boot の table の satp・stack・CPU 番号を取る: minor 24） | locore.S |
| `trap.S` | `stvec`（direct mode）の 1 つの入口。入口と出口の列は §2.3（M2）: `csrrw tp, sscratch, tp` で user/kernel を見分け、user からなら percpu に user の `sp` を退避して kernel の stack に切り替え、frame（x1〜x31、sepc、sstatus、scause、stval）を保存し、user の `tp` を frame に入れた直後に **`csrw sscratch, zero`**。`riscv64_trap(frame)` を呼び、復帰。user へ戻る出口は SIE=0 のまま `sscratch` に CPU の record を書き、tp・sp を戻して `sret`（`sscratch` を書いた後に fault し得る命令を置かない） | trap.S |
| `dispatch.S` | `asm_task_dispatch`（callee-saved s0〜s11・ra・sp の切り替え）、kernel task・user task・user frame の最初の入口、FP の保存と復元（f0〜f31・fcsr、`-march=rv64gc`） | dispatch.S |
| `cmain.c` | boot hart の最初の C: per-CPU の bootstrap、早期 console（SBI）、boot source（DTB か handoff）の解釈、FDT の parse、`riscv64_smp_configure`、page/space/int/debug の init、`kernel_entry(handoff)` | cmain.c |
| `fdt.c`/`fdt.h` | 自前の小さな FDT の parser（rpi4 の fdt.c と同じ役、別の実装）: `/cpus`（hart id = `reg`、`mmu-type`、`status`、`riscv,isa-extensions` の sstc/svpbmt/zicbom、intc の phandle）、`/cpus/timebase-frequency`、`/memory`、`/reserved-memory`、PLIC（`sifive,plic-1.0.0`: reg、`interrupts-extended` から hart→S context、`riscv,ndev`）、UART（`ns16550a`/`snps,dw-apb-uart`: reg、`reg-shift`、`reg-io-width`、`interrupts`）、`/chosen/bootargs`、`/chosen/linux,initrd-start/end`、ccache（`sifive,ccache0`: reg）、fw_cfg（`qemu,fw-cfg-mmio`: reg）、header の `/memreserve/` の表。`riscv,isa-extensions` が無い古い DT では `riscv,isa` の文字列も読む（minor 16）。parse の上限 2 MiB | bsp-rpi4/fdt.c |
| `sbi.c`/`sbi.h` | `ecall` の薄い包み（§1.2 の extension）、Base で版と拡張の有無を調べて log、DBCN の有無で早期 console を選ぶ | （無し） |
| `percpu.c`/`percpu.h` | `struct riscv64_percpu`（self、logical id、hart id、ready、running_task、switching_from、current_space、active_ack、timer の deadline と ticks、**ipi の理由の word**、S-mode の PLIC context、**走っている task の kernel の stack の先頭 `kernel_sp`**、**user の `sp` の退避 `user_sp`**（trap の入口が使う: M2）、debug の状態）。kernel 中は `tp` が record を指し、user 中は `sscratch` が指す（trap.S が交換）。`kernel_sp` は task の切り替え（dispatch.S）が更新する | percpu.[ch] |
| `smp.c` | hart の一覧（logical 0 = boot hart）、`cpus=N`、`hal_cpu_start_others`（HSM hart_start、ready の待ちは `rdtime` で 1 秒、来なければ FATAL と `cpus=1` の助言）、`riscv64_secondary_main`、`hal_cpu_notify(_mask)`（理由の bit を atomic で立ててから SBI IPI）、`hal_cpu_park`・`hal_cpu_panic_all`（STOP の理由の IPI）、`hal_cpu_idle`（`wfi` → SIE を立てる → trap → SIE を落とす） | smp.c |
| `int.c`/`int.h` | `riscv64_trap`: 割り込み（SSIP→IPI の理由の word を swap して NOTIFY/STOP、STIP→timer、SEIP→PLIC の claim の loop）と例外（§2.4 の表で `hal_trap_cause`/`mode`/address に写し、user なら `kernel_syscall_handler`/`kernel_user_fault_handler`、kernel なら `kernel_sys_fault_handler`）、CIP-453 の符号拡張 | int.c |
| `irq.c`/`irq.h` | IRQ の slot（PLIC の source 1〜ndev）、`hal_irq_register`（enable bit を立て priority 1）/`unregister`（priority 0、enable を落とす）、**mask = priority 0、unmask = priority 1**（enable bit は触らない: §1.3、M3）、`hal_irq_send_eoi`（claim した番号を complete に書く。ack = 番号 + 1）、affinity は boot hart 固定（arm64 の D1 と同じ）、`hal_irq_set_mode`、MSI は UNSUPPORTED | irq.c |
| `plic.c`/`plic.h` | PLIC の register の読み書き（base・context は DT）、init（全 source の priority 0・enable 0、boot hart の threshold 0、他の hart の threshold 7、**claim の空読みは読んだ番号を complete に書いて完結させる**（捨てない: minor 3））、secondary の context の init | bsp-rpi4/gic.c |
| `clock.c` | tick（`HAL_TIMER_FREQUENCY` 1000 Hz、interval = timebase/1000）: **常に SBI set_timer**（§2.1）。handler の順は rpi4 の `clock.c` と同じ: ticks を数える → deadline を now を超えるまで interval ずつ進める → **次の deadline を書く → それから `kernel_timer_handler(cpu, ack)`**（ack は `HAL_IRQ_ACK_NONE` でない定数、EOI は何もしない: minor 3）。`hal_rtc_read_counter`（`rdtime`、timebase）、`hal_rtc_read_epoch_time` は false（RTC は後） | bsp-rpi4/clock.c |
| `page.c` | 物理 page の bitmap（DT の `/memory` と `/reserved-memory`・`/memreserve/`、kernel の image、DTB、handoff、boot の table、loader の割り当て、OpenSBI の予約、initrd、ramfb の framebuffer）。UEFI の道では handoff の memory map（ZBL6 v6 の形）が正で、**DT の `/reserved-memory` と `/memreserve/` も予約として重ねる**（M4。U-Boot は EFI の予約に写すが、重ねても害が無い） | page.c |
| `space.c`/`space.h` | Sv39 の table: system の root（kernel 半分の 256 entry）、user の space ごとの root（下半分）、direct map（RAM の 1 GiB は leaf。**firmware・予約の領域を含む 1 GiB は 2 MiB／4 KiB の table に割って予約を外す**: M4）、device の窓（2 MiB の leaf、`hal_space_map_device`）、**kernel 半分の第 2 段の table を init で全部確保し、root の kernel の entry は init の後に変えない**（M1、§2.5）、`hal_space_*`、A/D bit の扱い（§2.5）、TLB の flush（自分は `sfence.vma`、他 hart は SBI RFENCE の remote_sfence_vma。CIP-1200 の間は全体 flush）、`hal_sync_instruction_stream`（`fence.i` ＋ remote_fence_i）。uncached の窓は無い（§2.1） | space.c |
| `task.c`/`task.h` | `struct riscv64_task`（arm64 と同じ field: space、run_cpu、target_cpu、stack、resume_sp、fpregs 264 byte、private、tls、active_user_frame、signal の frame ×8、debug）、`hal_task_*`、syscall の frame の出し入れ、signal の入口と戻り、stack の guard と深さの計測 | task.c |
| `debug.c` | `hal_task_get/set_user_{gp,fp,v}regs`、single step と debug point は `HAL_ERR_UNSUPPORTED`（§2.6） | debug.c |
| `asm.c`/`asm.h` | `csr` の読み書きの inline、IRQ の mask/unmask と save/restore、`wfi`、`fence`、`sfence.vma`、`fence.i`、MMIO の `hal_mmio_*`（`fence io,io` を挟む）、`hal_io_inp/outp`（無い: FATAL） | asm.[ch] |
| `lib.c` | `hal_memset/memcpy/strlen`、`hal_printf`、`hal_putc`（`kernel_putc` が無ければ早期 console = SBI の console ＋ bsp の framebuffer の console）、`hal_assert/fatal`、`hal_entropy_fill`（`zkr` が DT にあれば `seed` CSR。無ければ false） | lib.c |
| `watch.c` | arm64 の stall watch の riscv64 版は **p003 では入れない**（後の最適化。arm64 で 4 core の固まりの解析に効いたので Future Work） | watch.c |
| `bsp-virt/board.c` | QEMU virt: dcache の操作は no-op、errata 無し、board の init で fw_cfg を探して ramfb を設定する | bsp-rpi4/ |
| `bsp-virt/fwcfg.c`・`fwcfg.h` | fw_cfg（DT の `qemu,fw-cfg-mmio` の node の reg、無ければ ramfb 無しで続ける）: signature の確認、key 0x0001 の DMA bit、directory（0x19）から file の selector、DMA の書き込み（§1.5 の値を Value(s) referenced で引用） | （無し） |
| `bsp-virt/framebuffer.c`・`framebuffer.h` | ramfb: 640x400・XRGB8888・stride 2560 の 1 MiB の framebuffer を `.bss` に置き（物理 address は direct map から）、`etc/ramfb` に設定を書く。80x25 の cell が画面にちょうど入る（§5.1） | bsp-rpi4/framebuffer.c |
| `bsp-virt/cons.c`・`font.c` | 80x25 の text の console（shadow の cell、scroll、cursor）と 8x16 の font（PC/AT の VGA の font の複製。rpi4 の `font.c` と同じ内容を riscv64 に置く: §7 #16）。`hal_putc` が SBI の console と並べて描く。`kernel_putc` が公開されたら止まる（rpi4 の cons.c と同じ約束） | bsp-rpi4/cons.c・font.c |
| `bsp-visionfive2/board.c`・`ccache.c` | VF2: ccache の FLUSH64 で `hal_dcache_*`、`mimpid` の log、framebuffer は無い（`hal_putc` は SBI の console だけ）、（後の WS の）clock/reset の gating の口 | bsp-rpi4/ |
| `src/hal/idle-suspend-unsupported.c` | そのまま使う（S0ix 無し） | 同 |

早期 console と UART: HAL は UART の register に触らず **SBI の console** を使う（OpenSBI の console は virt でも VF2 でも同じ UART0 に出る）。virt では加えて **ramfb の framebuffer に同じ文字を描く**（rpi4 の「UART と framebuffer の両方に出す」形。§5 の判定はこの画面）。UART の driver（§2.7）は kernel 側にあり、`kernel_putc` が公開された後は HAL の出力もそこへ行く（hal.h の契約: `kernel_putc` は kernel の console が描けるようになった時に RELEASE で公開され、`hal_putc` は ACQUIRE で読んで委ねる）。kernel の console（§2.7 の glue）は文字を UART（driver）と framebuffer（HAL の口 `riscv64_fb_putc`、virt だけ）に出す。利点: HAL が UART の間隔や DW の癖を知らずに済み、U-Boot が設定した baud を壊さない。欠点: SBI の console は 1 byte か 1 buffer ごとに ecall で遅い（boot の間だけ。kernel の driver が TX を持った後は使われない）。

### 2.3 CPU・per-CPU・SMP

- **logical CPU の番号**: 0 = boot hart（firmware の `a0`、UEFI の道では `RISCV_EFI_BOOT_PROTOCOL` の値を loader が handoff に入れる）。他は DT の `/cpus` の順で、`mmu-type` があり `status` が disabled でない hart だけ、`cpus=N` の上限（既定は全部）、`RISCV64_CPU_MAX` = 8（VF2 の U74 ×4 と virt の試験に足りる。virt の `-smp` が 8 を超える分は使わない）。hart id ↔ logical の表は `smp.c` が持つ。
- per-CPU の record の見つけ方: kernel の中では `tp`（x4）が record を指す。kernel の C は TLS を使わないので `tp` は空いている（psABI の thread pointer）。user の `tp` は trap の frame に保存し、user へ戻る時に戻す。**`sscratch` の約束: user が走っている間は CPU の record の address、kernel が走っている間は 0。**
- **trap の入口の列**（M2。`stvec` は direct mode、1 つの入口）:
  1. `csrrw tp, sscratch, tp`。user から来た（sscratch = record）なら `tp` = record、`sscratch` = user の tp。kernel から来た（sscratch = 0）なら `tp` = 0 → `bnez tp, from_user`、kernel の道は `csrrw tp, sscratch, tp` で戻して（sscratch は再び 0）kernel の `sp` のまま frame を積む。
  2. user の道: `sd sp, user_sp(tp)`（percpu へ退避）→ `ld sp, kernel_sp(tp)`（走っている task の kernel の stack の先頭）→ frame を積む → `csrr t0, sscratch`（user の tp）を frame に → **直ちに `csrw sscratch, zero`**（ここから後の入れ子の trap は「kernel から」と判定される）→ `sepc`・`sstatus`・`scause`・`stval` を frame に。入れ子の trap が起きる前（`sscratch` を 0 にする前）に fault し得る命令を置かない: percpu と kernel の stack は direct map で常に有効な事を前提にし、user の memory には触らない。
  3. 入口で `sstatus.SUM` は常に 0（minor 6: kernel は user を pin した page の direct map で読む（`uaccess.c` の `hal_pmem_to_kernel`）ので SUM を立てる場面が無い。trap の入口で `csrc sstatus, SUM` を入れ、立っていれば assert）。`sstatus.FS`・`VS` は frame の `sstatus` に残る。
  4. `riscv64_trap(frame)`（C、§2.4）。
  5. user への出口: `kernel_user_return_handler` の後、SIE = 0 のまま（`sret` が SPIE から SIE を戻す）**`csrw sscratch, tp`**（record）→ frame から x1〜x31（tp と sp を最後に）→ `sret`。`sscratch` を書いた後は register の load と `sret` だけで、fault し得る命令（memory の読み書き以外）を置かない。frame は kernel の stack の上にあり fault しない。
  6. kernel への出口（kernel から来た trap）: `sscratch` は 0 のまま、frame を戻して `sret`。
- hart の起動: `hal_cpu_start_others` は logical 1〜 の各 hart に boot block（boot の table の satp の値、stack の先頭、logical id。64 byte 揃え。cache の clean は要らない: 同じ coherent な memory を読む）を作り `sbi_hsm_hart_start(hartid, phys(riscv64_secondary_entry), phys(block))`。hart は `satp` = 0、`a0` = hartid、`a1` = block の物理 address で入る（§1.2。入口は SBI の保証だけに頼り、`fence.i`・`stvec`・`sscratch`=0・`sie`=0 を自分で書く: minor 24）→ locore が block の satp を書いて（kernel の最終の table は識別 map を持たないので、**block の中に識別 map 付きの boot の table の satp を入れておき**、高い address に跳んでから最終の table に切り替える。arm64 が TTBR0 の boot の識別 map と TTBR1 を同時に使うのと同じ目的）→ `riscv64_secondary_main`（percpu、space の選択、`stvec`、PLIC の自分の context、初期 task、tick、ready の公開、`kernel_secondary_entry`）。
- IPI: 1 本（SSIP）しか無いので理由は per-CPU の word の bit（NOTIFY、STOP）。送る側は bit を `amoor` で立ててから SBI IPI（hart_mask は hartid の bit、base は 0。hart id が 64 を超える機械は対象外）。受ける側は **「`sip.SSIP` を落とす（`csrc sip, SSIP`）→ word を `amoswap` で 0 にして理由を処理」を word が 0 になるまで繰り返す**（minor 4: SSIP を落とした後に届いた bit を取りこぼさない。SBI IPI は pending の bit を立てるだけで数えないので、落とした後に立った bit は次の trap でも拾える。推測: OpenSBI は IPI を `mip.SSIP` の委譲で届ける）。NOTIFY は `kernel_cpu_notify_handler(cpu, ack)`、ack は `HAL_IRQ_ACK_NONE` でない定数（EOI は何もしない）。
- `hal_cpu_idle`: `sie` に timer・soft・ext を許したまま `sstatus.SIE` = 0 で `wfi` → 戻ったら `csrsi sstatus, SIE`（ここで trap が取られ処理される）→ `csrci sstatus, SIE`。「enable して halt」の原子性は `wfi` の性質（§1.1）で満たす。
- 停止: `hal_cpu_park` は SIE を落として `wfi` の loop。`hal_cpu_panic_all` は他の ready な hart に STOP の IPI（SIE を落として回っている hart には届かない。arm64 と同じ制限）。

### 2.4 trap と kernel への写し

| scause | hal_trap_cause | mode | address | 備考 |
| --- | --- | --- | --- | --- |
| 8（U の ecall） | — | — | — | syscall: 番号 `a7`、引数 `a0`〜`a5`、`sepc += 4`、戻り値 `a0`。`kernel_syscall_handler` → `kernel_user_return_handler` |
| 12 / 13 / 15 | PAGE_FAULT | EXEC / READ / WRITE | `stval`（12 は CIP-453 の符号拡張） | HAL は leaf を作る時に A=1（書ける leaf は D=1 も）を立てるので、A/D の 0 による fault は来ない（§2.5）。来たら普通の page fault として kernel に上げる（arm64 と同じ: HAL は A/D を遅延で立てない） |
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
| 0xffff_ffc0_0000_0000 + phys | 128 GiB | **direct map**（RAM だけ。root の entry 256〜383、原則 1 GiB の leaf。VF2 の DDR 0x4000_0000〜0x2_4000_0000、virt の DRAM 0x8000_0000〜 を覆う） |
| 0xffff_ffe0_0000_0000 〜 | 64 GiB | device の窓（`hal_space_map_device`、2 MiB の leaf。root の entry 384〜447。cache の属性は PMA 任せ、Svpbmt は使わない） |
| 0xffff_fff0_0000_0000 〜 0xffff_ffff_7fff_ffff | 30 GiB | 予約（将来の uncached の窓など。root の entry 448〜509 は invalid のまま） |
| 0xffff_ffff_8000_0000 〜 | 2 GiB | **kernel の image の窓**（`RISCV64_KERNEL_LINK_VIRT_BASE`、root の entry 510〜511）。link は仮想のここ、物理は loader が選ぶ。**式: `VMA = LINK_VIRT_BASE + (paddr − LINK_PHYS_START)`、実行時 `physical = VMA − LINK_VIRT_BASE + kernel_phys_start`**（M13: amd64 の `vaddr == BASE + paddr` は riscv64 では 2^64 を超えるので使わない。`vmunix.ld` の `.text` は VMA 0xffff_ffff_8000_0000、LMA（`AT`）0x8020_0000 で、`_start` が最も低い LMA にある事を assert（minor 17: QEMU は一番低い load address を kernel の始まりとする））。`-mcmodel=medany` が要る |

- **kernel 半分の table の寿命（M1）**: Sv39 には arm64 の TTBR1 の様な kernel 専用の root が無く、root の上半分（entry 256〜511）は space ごとの root に複写される。複写の後に root の kernel の entry を変えると既存の space に届かない。→ **init で kernel 半分の root の entry を全部確定し、以後は変えない**: direct map は 1 GiB の leaf（RAM の無い entry は invalid のまま、RAM の一部だけ予約を外す 1 GiB は第 2 段の table を init で確保）、device の窓 64 entry は **第 2 段（2 MiB）の table 64 page を init で確保**して root に入れ（`hal_space_map_device` は第 2 段の entry を書くだけ）、kernel の窓 2 entry も第 2 段を init で確保。root の上半分へ書く関数は init の後は呼ばれない事を `riscv64_kernel_root_sealed` の assert で守る（`hal_space_create` は sealed の後だけ複写する）。費用: 64 ＋ 2 ＋ 数 page ≈ 0.3 MiB。arm64 が遅れて作る table（device の 1 GiB ごとの level-2、uncached）は riscv64 では init で済ませる形になる。uncached の窓は無い（§2.1）。
- **direct map と firmware・予約の領域（M4）**: OpenSBI の firmware の領域（virt なら 0x8000_0000 からの数百 KiB、PMP で M-mode 専用）と DT の `/reserved-memory`（`no-map` の node、OpenSBI の `mmode_resv*`）・`/memreserve/` は **map しない**（Linux の boot.rst の「PMP で守られた memory を map しない」に合わせる。map して触れば access fault、投機の access も危ない）。実装: 予約を含む 1 GiB は第 2 段の 2 MiB の table に割り、予約を含む 2 MiB はさらに 4 KiB の table に割る（init で確保）。残りの 1 GiB は leaf のまま。UEFI の道でも DT の予約を読んで重ねる。`hal_pmem_to_kernel` は予約の物理を渡されない前提（page.c が予約を bitmap から外す）。
- user の leaf は U=1、kernel は U=0 G=1。`sstatus.SUM` は常に 0（§2.3）。実行可の user page の作成・変更では `hal_sync_instruction_stream`（arm64 の WS044 p009 と同じ理由。`fence.i` は自分の hart だけなので他の hart は SBI remote_fence_i）。
- **A/D bit（minor 9、arm64 の扱いに合わせる）**: 作る leaf に A=1、書ける leaf に D=1 を最初から立てる（Svade の実装でも fault しない。hardware が立てる機械でも立て直すだけ）。DIRTY の追跡は arm64 の `PTE_SW_DIRTY` と同じ **software の bit（PTE の RSW bit 8）**で行う: 書ける leaf を作る時・`hal_space_prot` で書けるようにする時に SW_DIRTY を立て、`hal_space_query`/`prot_query` の DIRTY は SW_DIRTY、ACCESSED は PRESENT と同じ（保守的）、`hal_space_clear_flags(DIRTY)` は書けなくなっている leaf（W=0）だけ SW_DIRTY を落として TLB を flush（arm64 `space.c` の `clear_flags` と同じ順: 書き換え → `hal_wmb` → flush）。hardware の A/D の値は読まない（機械ごとの差を見せない）。
- TLB: 自分の hart は `sfence.vma`（ASID は使わない、arm64 の D2 と同じ）。他の hart へは SBI RFENCE の remote_sfence_vma（hart_mask は space を選んでいる hart、または全部）。user の space の切り替えは自分の hart だけ flush。
- **boot の table（locore の `.bss.boot`、M6）**: kernel の窓（image の物理を 2 MiB の leaf で、root の entry 510〜511 と第 2 段 1 page）＋ **識別 map は RAM の載り得る範囲全体を 1 GiB の leaf で**（virt: 0x8000_0000 から 128 GiB、root の entry 2〜129; VF2: 0x4000_0000 から 8 GiB。DTB は RAM の終わりの近く（§1.5）、initrd は kernel + ≤512 MiB、handoff は loader の選んだ所、どれも RAM の中）。boot の table は予約の領域も map するが、boot の間は kernel の image・DTB・handoff・initrd しか触らないので無害。最終の table（上）に切り替えた後は識別 map は無い。`satp` を書いた直後の命令は識別 map で続き、高い address へ跳んだ後 arm64 と同じく C で最終の table を作って切り替える。

### 2.6 task・signal・debug・FP

- syscall の ABI: `ecall`、番号 `a7`、引数 `a0`〜`a5`、戻り `a0`（aarch64 の `x8`/`x0`〜`x5`/`svc #0` に対応）。libc の `__syscall6` は `a0` の番号を `a7` に移して引数を詰める。
- signal の frame の head は aarch64 と同じ 16 byte（token、reserved、context_pointer）、restorer は無し（`HAL_TASK_SIGNAL_FRAME_HAS_RESTORER 0`）。libc の `__signal_restorer` は `sp` から token と context を読み、`a7` = 80（sigreturn、aarch64 と同じ番号）で `ecall`、戻ったら `ebreak`。
- **TLS（M10、設計で決める）**: zedBSD の TLS の契約は psABI の variant と独立で、「`tp` は TCB（`struct __rtld_tcb`、`kern_tls_prefix` が先頭）を指し、TLS の block はその下」（`src/kern/elf.c` の `load_static_tls`: `tp = mapping + payload_size`、`userland/base/libc/static-tls.c`、`src/rtld/rtld.c` の thread の生成。2026-10-18 に読んだ）。rtld は x86 以外で static TLS の layout を持たず（`rtld.c` の `layout_static_tls` の非 x86 版は空: 「variant I は tp から上に数え TCB を下に置くので `kern_tls_prefix` の契約で表せない。全 module を dynamic に保つ」）、kernel は x86 以外の static な executable の PT_TLS を `ENOEXEC` で断る（`elf.c` の `load_static_tls` の `#if`）。→ **riscv64 は arm64 と同じ: static TLS を持たず全部 dynamic。** compile は `-ftls-model=global-dynamic`（arm64 の `vmunix.mk` 502 行と同じ）、`-mtls-dialect=trad`（TLSDESC を出さない: rtld に TLSDESC の riscv の実装を足さない）、link は `--no-relax`（GP・TLS の relaxation で LE/IE に落ちるのを防ぐ）。rtld の relocation は `R_RISCV_TLS_DTPMOD64`・`DTPREL64`（GD）だけを受け、**`R_RISCV_TLS_TPREL64`（IE）と LE は無い事を `tools/build/check-dynamic-elf.py` の riscv64 の許す relocation の集合で検査**（arm64 が TLSDESC を要るのと対で、riscv64 は `{NONE 0, 64 2, RELATIVE 3, JUMP_SLOT 5, DTPMOD64 7, DTPREL64 9, IRELATIVE 58}` を許し TPREL64 11 を拒む）。libc の `tp` の読み（`static-tls.c`・`pthread.c` の `__aarch64__` の枝）に `__riscv` の枝（`mv %0, tp`）。psABI の「tp は TCB の次」は static TLS の約束であり、全部 dynamic の zedBSD には関係しない（`__tls_get_addr` が DTV で引く）。`hal_task_set_tls` は task の user `tp` の値を持ち、user へ戻る frame に入れる。
- FP: eager（arm64 と同じ）。task の生成時に boot hart が捕った初期の状態（fcsr = 0、f = 0）を複写、切り替えで保存・復元、`sstatus.FS` は user へ戻る時 Clean、`sstatus.VS` は Off に固定（minor 7）。kernel の中では FP 命令を出さない（`-mabi=lp64`）。lazy（FS=Off で不正命令の trap を取って復元）は最適化で Future Work。
- `long double`（minor 14）: LP64 の riscv64 の `long double` は 128 bit（IEEE binary128、soft）。zedBSD の amd64・i386 は `-mlong-double-64` で 64 bit にしているが（`sysroot.mk`）、arm64 は 128 bit のまま compiler-rt の builtins を sysroot に入れている（`ZEDBSD_SYSROOT_ARM64_COMPILER_RT_SOURCES`。その一覧に `*tf3.c` 等の fp128 の物が無い事を 2026-10-18 に grep で確かめた → libc が `long double` の算術を使っていないか、arm64 で link の時に落ちていないかを p007 で `nm` で確かめる）。riscv64 は arm64 と同じ扱い（128 bit、要る builtins を p010 の sysroot に足す）。`-mlong-double-64` は riscv では使えない（推測、p007 で確かめる）。
- `include/uapi/reg.h` の riscv64: `struct reg { uint64_t r_x[32]; uint64_t r_pc; }`（264 byte）、`struct fpreg { uint64_t fp_f[32]; uint32_t fp_fcsr; uint32_t fp_reserved; }`（**264 byte に固定**、minor 13。`hal_fpregs` と同じ並び）。
- debug（ptrace）: `hal_task_get/set_user_gpregs`（x1〜x31 と pc。`struct hal_gpregs { uint64_t x[32]; uint64_t pc; }`、x[0] は 0）、fpregs（f[32]、fcsr）、vregs は空。**single step と hardware の debug point は p003 では UNSUPPORTED**（`HAL_DEBUG_HAS_SINGLE_STEP 0`、`HAL_DEBUG_POINT_MAX 1` で全部の `HAL_DEBUG_KIND_*_OK 0`、`hal_task_set_debug_points` は count 0 だけ受ける）。RISC-V の S-mode には hardware の single step の bit が無く（Sdtrig の trigger は M-mode の委譲が要り OpenSBI は委譲しない）、kernel が `ebreak` を次の命令に置く形は後の Phase。`src/kern/ptrace.c` は `hal_task_set_single_step` の失敗を `ENOTSUP` にするので kernel は壊れない（確かめた）。`HAL_DEBUG_POINT_MAX` は配列の大きさに使われるので 0 にしない。
- stack: kernel の stack は arm64 と同じ 32 KiB（BUG-321 の教訓）、guard 128 byte、fill の計測。

### 2.7 kernel 側（HAL の外、この WS の範囲）

- `src/kern/platform/riscv-virt.c`・`src/kern/platform/visionfive2.c`（README の「platform ごとに 1 つの TU」）。`include/kern/platform.h` の hook の全部（2026-10-18 に読んだ、minor 11）: `kern_platform_init`（handoff の確認、console の driver の登録、`partition_set_scheme(&drv_partition_scheme_mbr)`、device の表に ramdisk を 1 つ）、`kern_platform_refresh_devices`（何もしない）、`kern_platform_input_init`（0: 入力の device は無い）、`kern_platform_block_device`（ramdisk の `struct disk`）、`kern_platform_debug_write`（SBI の console）、`kern_platform_halt`（SBI SRST shutdown、無ければ `wfi` の loop）、`kern_platform_reboot`（SRST cold reboot）、`kern_platform_poweroff`（SRST shutdown、無ければ `EOPNOTSUPP`）、`kern_platform_shutdown_prepare`（weak、定義しない）、`kern_boot_handoff(name)`（HAL の `hal_get_arch_handoff` の名前を通す）。**p003 に最小の `riscv-virt.c` を入れる**（M8: `vmunix` の link には platform の hook と `src/kern/elf.c` の `ELF64_EXPECTED_MACHINE` の riscv の枝（無いと `#error`）が要る。p003 の版は device 0・halt/reboot/debug_write だけ、p008 で console・ramdisk を足す）。
- **UART の console の driver** `src/drivers/serial/ns16550.c`（新。MMIO の 16550、間隔と access 幅を引数で持ち、DW の busy detect の癖を flag で持つ。pcat の `serial-mirror.c` は port I/O の別物、sun4u の `bsp-sun4u/uart.c` は HAL の中の物で再利用しない）＋ teletype の console の glue（rpi4-console.c と同じ役の `src/drivers/generic/serial-console.c`: `kern_text_register` の teletype、出力は UART の TX と（virt なら）HAL の framebuffer の console の口、`kernel_putc` の公開、RX は PLIC の割り込みで tty に入れる。**RX の割り込みが「PLIC の割り込みが動く」の証拠**）。
- **root の媒体（B1 の修正案 (a)、既定）**: initrd。tree の root の選択（`src/kern/vfs.c`、2026-10-18 に読んだ）は (1) `rootpart=` の選択子は `DISK_PARTITION` の flag を持つ device しか受けない（`vfs_resolve_native_root`）、(2) 選択子の無い時の legacy の autoroot は **loader の boot partition** を要り（無ければ `ENXIO`）、boot partition は handoff の version と scheme（`MULTIBOOT`+`MBR` なら `p_index + 1 == boot_partition_index`、SUN4U・X68K・PC98）で同じ物理 disk の partition から探す、(3) root は boot partition と同じ disk の兄弟の partition のうち `/etc/zedbsd-root` の marker を持つ UFS（1 つだけ）、ARM64 だけ image の overlay の予備がある。partition の無い device を root にする道は無い。→ **ramdisk の中身を rpi4 と同じ MBR の disk image にする**: p1 = FAT（小さく、`zedbsd.cfg` と marker を置く「boot partition」）、p2 = UFS の root（`tools/build/ufs_format.py` が `/etc/zedbsd-root` の marker を書く）。HAL の `kern_boot_handoff` は rpi4 と同じ **`MULTIBOOT` / `KERN_PARTITION_SCHEME_MBR` / `boot_partition_index` 1 / `boot_bios_id` 0x80**（`bsp-rpi4/boot.c`）、platform は `partition_set_scheme(&drv_partition_scheme_mbr)` と `disk_registry_reset()` の後に ramdisk を **bios_id 0x80・`KERN_DEV_PRESENT | KERN_DEV_BOOT_ORIGIN`** の device として表に出す（`rpi4.c` の SD と同じ置き方）。kernel の `vfs.c` は触らない。device の class は boot.h の enum（FDD・IDE・SCSI・SD）に ramdisk が無いので、既定は **`KERN_DEV_SD` を使う**（kernel の `src/kern/*.c` は `device_class` を見ない事を grep で確かめた。platform の中の目印にすぎない）。新しい `KERN_DEV_RAM` を足す案は boot.h の変更なので [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) の任意の項目。
  - loader は `root.img`（MBR の image）を ESP から memory に読み handoff に範囲を渡す。kernel は memory を背にした block device（新 `src/drivers/generic/ramdisk.c`。tree に initrd/ramdisk が無い事を grep で確かめた）を `kern_platform_block_device` で出す。QEMU の直の起動では `-initrd root.img` の `/chosen/linux,initrd-start/end` から同じ ramdisk を作る。image は `platform/arm64/disk-image.mk` が使う MBR の image の道具（`tools/build`、`--kernel`・`--ufs-root`）を riscv64 の `rootfs.mk` から呼んで作る（kernel は入れない）。
  - 選ばなかった (b)（vfs.c に partition の無い root の道を足す）は §8。
- `arch` の `#if` の棚卸し（minor 12、2026-10-18 に grep: `__aarch64__`・`__x86_64__`・`HAL_ARCH_ARM64`・`HAL_ARCH_AMD64` の `#if`/`#elif` を持つ kernel・libc・rtld の file）: `src/rtld/rtld.c`（19）、`src/kern/vfs.c`（7: ARM64 の overlay）、`src/kern/elf.c`（5）、`src/libc/math/sqrt.c`（4）、`src/kern/ptrace.c`（4）、`src/libc/setjmp.c`（2）、`include/uapi/reg.h`（2、`#error`）、`include/libc/setjmp.h`（2）、`src/kern/syscall.c`（1: ptrace の非 x86・非 arm64 の `ENOSYS` の枝）、`src/kern/signal.c`・`main.c`・`klog.c`・`entry.c`（各 1: 大きさの既定）、`include/libc/vulkan/vulkan_core.h`（1）、`include/kern/net/tcp-socket.h`・`packet-buf.h`（各 1）。p008（kernel）と p007（libc・rtld）でそれぞれ全部を見て、riscv64 の枝を足すか「他の arch と同じ既定」で済むかを file ごとに記録する。
- build: `platform/riscv64/`（§4）。

## 3. 起動の道

### 3.1 QEMU virt

1. **p003・p008・p007 の試験（UEFI 無し）**: `qemu-system-riscv64 -M virt -smp 4 -m 2G -device ramfb -display none -serial unix:SOCK,server,nowait -qmp unix:QMP,server,nowait`（既定の `-bios` = OpenSBI fw_dynamic）`-kernel build/riscv64-virt/vmunix -initrd root.img -append "..."`。kernel は ELF のまま渡し（QEMU は segment を物理 address に置く。link の物理 0x8020_0000 = firmware の次の 2 MiB 境界）、`a0` = 0、`a1` = DTB で locore の (a) の入口。画面は ramfb（HAL が設定）、UART は unix socket（`serial.py` が文字を**入れる**口。判定は画面: §5）。
2. **製品の道（p004）**: U-Boot `qemu-riscv64_smode`（Debian `u-boot-qemu`。Q1 が `sudo apt install u-boot-qemu`）を `-kernel /usr/lib/u-boot/qemu-riscv64_smode/u-boot.bin` で渡し、disk を `-drive if=none,id=d,file=image -device virtio-blk-device,drive=d`（virtio-mmio。U-Boot は `VIRTIO_MMIO`・`VIRTIO_BLK` を持つ: §1.5）、`-device ramfb` は同じ。U-Boot が ESP の `\EFI\BOOT\BOOTRISCV64.EFI` を読み、DTB は control DT を configuration table に、boot hart は protocol に。zedBSD の loader → `zedbsd.cfg` → `vmunix`・`root.img`（initrd）。kernel は virtio-blk を読まない（root は ramdisk）ので virtio の driver は要らない。
3. edk2（RiscVVirtQemu）は Debian に無く、build が要る → 既定にしない。U-Boot の EFI で足りない物が出た時の代替（§7 #7）。

### 3.2 VisionFive 2

1. firmware: 板の QSPI に入っている vendor の firmware の U-Boot の版は未確認（古い物は `RISCV_EFI_BOOT_PROTOCOL` を持たない可能性: U-Boot への追加は 2022 年）。**既定の案: Debian の `u-boot-starfive`（2025.01）の `u-boot-spl.bin.normal.out` と `u-boot.itb` を SD の partition 1・2（GUID §1.6）に置き、MSEL を SD にして試す**（flash を書き換えない。動けば後で `sf update` で flash へ）。
2. SD の GPT: 1 spl、2 uboot、3 **ESP**（FAT32: `EFI/BOOT/BOOTRISCV64.EFI`、`zedbsd.cfg`、`vmunix`、`root.img`、`logo.ppm` は不要）、4 UFS の root（SD の driver が来るまで未使用。initrd の間は 3 だけで足りる）。amd64 の `native` layout に 1・2 を足した形を `platform/riscv64/disk-image.mk` で作る（`tools/build` の image の道具は GPT を作れる: amd64 の hybrid/native の道）。
3. U-Boot（S-mode、boot hart は OpenSBI の lottery の 1〜4 のどれか）の bootstd が mmc の ESP を見つけ `BOOTRISCV64.EFI` を実行。DTB は `${fdtfile}` を ESP の `/dtb/` 等に置けば（例 `dtb/starfive/jh7110-starfive-visionfive-2-v1.3b.dtb`、Linux の DTS から dtc で作る物を tree に置くかは §7 #8）、無ければ U-Boot の control DT（u-boot.itb の中の DTB、2025.01 は upstream の DTS）が渡る。**既定: control DT を使う**（file を置かない）。
4. loader → kernel（§3.3・3.4）→ UART0 で login。電源の入れ直しは Pi と同じ SwitchBot の plug（§5）。

### 3.3 loader の riscv64 版（`bootloader/uefi/`）

共有する物（ABI に依らない C）: `volume-discovery.c`（FAT の探索）、`zedbsd-config.c`（`zedbsd.cfg`）、`common/boot-override.c`・`common/logo-path.c`・`boot-keys.c`（EFI の protocol だけ）、`memory-map.c`/`memory-map-v6.c`/`common/memory-map.c`（UEFI の memory map → ZBL6 v6 の範囲。`bootstrap_cr3` の field は「bootstrap の page table の root」の意味で riscv64 では `satp` の値を入れる）、`include/uefi.h`（`EFIAPI` を riscv64 では空に。今は x64 の ms_abi の定義: 確かめて `#if` で分ける）、`logo.c`・`video.c`・`framebuffer.c`（GOP があれば使う。**virt と VF2 の U-Boot に GOP が無い時は skip して handoff の framebuffer の flag を落とす**。今の bootx64.c は GOP が無いと `fail_status("Locate GOP")` で止まるので、riscv64 の main では「無ければ無しで続ける」）。

足す物:
- `bootriscv64.c`: `efi_main(image, system)` の riscv64 版。x64 固有（CR3、4 段の table、GOP の stage の描画、ACPI RSDP の探索）を持たず、(1) 設定と kernel の ELF を読む（`elf64.c` の `zbl_elf64_plan`/`place` の検査を arch ごとに書き分ける: **M13**。今の `vaddr == AMD64_KERNEL_LINK_VIRT_BASE + paddr` と「`load_base > 1 GiB` を拒む」は riscv64 で成り立たない → `riscv64-kernel-image.h` が `KERNEL_LINK_VIRT_BASE`・`KERNEL_LINK_PHYS_START`（0x8020_0000）・`KERNEL_PHYS_LIMIT`・`KERNEL_PHYS_ALIGN`（2 MiB）・`KERNEL_MAX_BYTES`（16 MiB）を定義し、検査は `vaddr == VIRT_BASE + (paddr − LINK_PHYS_START)` と `load_base` が RAM の中（UEFI の map の conventional memory）である事）、(2) **`root.img` を読んで initrd の範囲にする**、(3) `RISCV_EFI_BOOT_PROTOCOL` で boot hart（無ければ `/chosen/boot-hartid`、それも無ければ失敗して止める）、(4) configuration table の DTB を複写、(5) memory map を取り `ExitBootServices`、(6) transition。**loader は Sv39 の table を作らない**（minor 15: kernel の locore が両方の入口で自分の boot の table を作る（§2.5）ので、loader は `satp` を 0 にして物理の `_start` に跳べばよい。edk2 の様に paging を有効にしている firmware でも、identity map の中で `satp` を 0 にする 1 命令で済む（`sfence.vma` を続ける））。
- **handoff は 2 層（M7）**: (1) **loader → HAL**: `bootloader/include/riscv64-handoff.h` の `struct zbl6_riscv64_handoff`（ZBL6 の形: magic `ZBL6_HANDOFF_MAGIC` 0x364c425a、新しい version 番号、UEFI の v7 の並び（common、`kern_boot_parameter_record`、ZBL6 v6 の memory の範囲と割り当て、provenance）に **riscv64 の拡張: `boot_hart`、`fdt_phys`・`fdt_size`、`initrd_phys`・`initrd_size`、`kernel_phys_start`・`kernel_phys_end`**。`bootstrap_cr3` に当たる field は 0（table を作らない））。amd64 の `bsp-pcat/boot.c` が ZBL6 を読んで `kern_boot_handoff` を組み立てるのと同じ役を riscv64 の `cmain.c` が持つ。(2) **HAL → kernel**: `include/kern/boot.h` の `struct kern_boot_handoff`（`MULTIBOOT`、MBR、index 1、bios 0x80: §2.7）。FDT・initrd・boot hart は **既定では boot.h を変えずに `hal_get_arch_handoff("riscv64.fdt")`・`("riscv64.initrd")`・`("riscv64.boot-hart")` の名前付きの object で渡す**（hal.h の契約「architecture-specific boot handoff object by name」の範囲。amd64 の `"acpi.rsdp"`（WS049 p006、HAL の責務の追加として承認）と同じ形）。rpi4 の様に boot.h の struct を拡張する案（`struct riscv64_boot_handoff`、`KERN_RISCV64_HANDOFF_MAGIC`）は boot.h の「記録された決定の後にだけ変える」の対象で、差分を [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) に置いた。どちらにするかはユーザーの判断（§7 #9）。
- `pe-header.S`: PE32+ の header（machine `IMAGE_FILE_MACHINE_RISCV64` = 0x5064（記憶。p004 で U-Boot `include/pe.h` で確かめる）、subsystem 10 = EFI application、`.text`・`.data` の 2 section、base relocation の directory は空。**`.bss` は `.data` の `VirtualSize > SizeOfRawData` で表す**（minor 15: file に無い部分を firmware が零で埋める））。手順は Linux の `efi-header.S`（Procedure referenced）と PE/COFF の仕様。**自作し、転記しない。**
- `start.S`: PE の entry。`a0` = image handle、`a1` = system table を保存、**自分の `.rela.dyn`（`R_RISCV_RELATIVE`）を load address で当て**、stack（firmware の）のまま `efi_main` を呼ぶ。`gp` は使わない（`-mno-relax`）。
- `transition.S`: `csrci sstatus, SIE`、`sie` = 0、`satp` = 0・`sfence.vma`（firmware が paging を使っていた時のため。U-Boot は使わない）、**`fence.i`**（M5: kernel の image を写した後、命令 cache に古い行が残らないように。U74 は I/D 分離、QEMU の TCG では現れない）、stack を loader の確保した page に、`a0` = boot hart、`a1` = handoff の物理 address、kernel の **物理の** entry へ `jr`。
- build（`platform/riscv64/bootloader.mk`）: `clang --target=riscv64-unknown-elf -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -fPIC -fno-plt -mno-relax -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin ...`、`ld.lld -m elf64lriscv -shared -Bsymbolic --no-relax -T bootloader/uefi/riscv64-loader.ld`（**`-z notext` は付けない**: text の relocation があれば link を失敗させる（minor 15）。header を先頭に、`.text`/`.rodata` と `.data`/`.rela.dyn`/`.bss` を 4 KiB に揃えた 2 つの section に）、`llvm-objcopy -O binary → BOOTRISCV64.EFI`、`platform/riscv64/tools/check-bootriscv64.py`（PE の header の整合: machine、section の RVA/size が file の大きさに合う、`.data` の VirtualSize ≥ SizeOfRawData で `.bss` を含む、entry が `.text` の中、`.rela.dyn` に RELATIVE 以外が無い、undefined symbol 無し）。
- ELF と relocation: kernel の `vmunix` は `ET_EXEC`、非 PIC、`-mcmodel=medany`、link は VMA `RISCV64_KERNEL_LINK_VIRT_BASE`（0xffff_ffff_8000_0000）、LMA 0x8020_0000（§2.5 の式）。loader は 2 MiB 揃えの物理の置き場を UEFI の memory map から選び（amd64 の `AllocateMaxAddress` の道）、`zbl_elf64_place` で rebase。kernel は relocation を持たない（HAL が窓で吸収）。`vmunix.ld` の assert: image ≤ 16 MiB（boot の table の 2 MiB の leaf の数 8 に合わせる）、`_start` は 4 byte 揃えで最も低い LMA（minor 17）。

### 3.4 kernel の 2 つの入口と handoff

- (a) SBI の直の起動（QEMU `-kernel`）: `a1` が DTB。cmain は DTB を parse し、HAL の内部の boot の記録（`struct riscv64_boot_info`: memory の範囲、予約、initrd、FDT、boot hart、command line、kernel の物理）を作る: memory は DT の `/memory`−`/reserved-memory`−`/memreserve/`、initrd は `/chosen/linux,initrd-*`、command line は `/chosen/bootargs`、boot hart は `a0`。kernel の物理は link の物理（QEMU が segment の物理 address に置く）。
- (b) loader: `a1` が `zbl6_riscv64_handoff`。memory は handoff の ZBL6 v6 の範囲（UEFI の map が正。DT の `/memory` は読まない: boot.rst の「UEFI では EFI の map だけ」に合わせる）に DT の `/reserved-memory`・`/memreserve/` を予約として重ねる（M4）。
- 両方の後は同じ: HAL が `struct kern_boot_handoff`（MULTIBOOT/MBR/1/0x80）を作り、`hal_get_arch_handoff("boot.command-line")` は parameter record の text（b）か bootargs（a）、`"riscv64.fdt"`・`"riscv64.initrd"`・`"riscv64.boot-hart"` は内部の記録（§3.3 の既定。boot.h の拡張を選べば struct の field）、`kernel_entry(handoff)`。kernel の platform は `kern_boot_handoff(name)` で FDT と initrd を取る。

## 4. build の構成

### 4.1 platform と config

- `platform/riscv64/`: `Makefile`（include の列）、`vmunix.mk`（HAL と kernel の source の一覧。arm64 の vmunix.mk の形。`RISCV64_CPPFLAGS := -nostdlibinc -Iinclude -Isrc -I. -Isrc/hal/riscv64 -DHAL_ARCH_RISCV64 -DHAL_BOARD_VIRT|-DHAL_BOARD_VISIONFIVE2 -DKERN_USER_ABI_RISCV64 -DKERN_USER_ABI_LP64`、`RISCV64_CFLAGS := -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -mno-relax -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror`、LTO は `ZEDBSD_KERNEL_LTO_CFLAGS`）、`vmunix.ld`（`OUTPUT_ARCH(riscv)`、`KERNEL_LINK_VIRT_BASE = 0xffffffff80000000`、`KERNEL_PHYS_LOAD = 0x80200000`、`.text` を `AT(KERNEL_PHYS_LOAD)`、`.text.boot` を先頭、`.bss.boot`、image ≤ 16 MiB の assert）、`user.ld`、`bootloader.mk`（BOOTRISCV64.EFI）、`rootfs.mk`・`disk-image.mk`（§3.2 の GPT。virt 用は ESP ＋ root の 2 partition（root の partition は virtio-blk の driver が無いので今は使わず、root は initrd。将来の SD/virtio の WS の形を保つため置く）、VF2 用は spl・uboot を足した 4 partition。board で分ける）、`run.mk`（§3.1 の 2 つの command。QEMU の起動は T1 と Q1 だけが行う）、`toolchain.mk`。
- `Makefile`（2026-10-18 に 60〜80・145〜160・197〜200 行を読んだ、minor 10）: `ZEDBSD_PLATFORM` に `riscv64-virt` と `visionfive2` を足し、`ZEDBSD_PLATFORM_DIR` の `$(if $(filter …))` の列に両方 → `riscv64`、`ZEDBSD_ARCHITECTURE riscv64`、`ZEDBSD_BOARD virt|visionfive2`、`ZEDBSD_VARIANTS_riscv64_virt := default`・`ZEDBSD_VARIANTS_riscv64_visionfive2 := default`、`ZEDBSD_PLATFORM_TARGET_riscv64-virt := riscv64_virt`、`_visionfive2 := riscv64_visionfive2`、`ZEDBSD_TARGET_TRIPLE riscv64-unknown-zedbsd`、sysroot `build/riscv64/sysroot`、`sysroot-riscv64` の goal。**`BUILD` は `build/$(ZEDBSD_PLATFORM_DIR)` なので両 board が `build/riscv64` を共有してしまう** → `BUILD := build/$(ZEDBSD_PLATFORM)`（`build/riscv64-virt`・`build/visionfive2`）に分ける規則を riscv64 だけに足す（他の platform の BUILD は変えない。T1 が virt と VF2 の image を同じ tree で作り分けるため）。driver の既定 `CONFIG_DRIVER_NE2000 ?= y`・`LGY98 ?= y`・`PCI_UHCI ?= y`（Makefile 197〜200 行）は riscv64 の config.mk（`config/ci/config-riscv64-*.mk`）で `n` にする（ISA・UHCI の bus が無い。platform の mk で上書きしない）。
- `tools/menuconfig.py`（731 行付近の CPU/Board の menu を読んだ）: `PLATFORMS` に `("riscv64-virt", "riscv64", "virt", "QEMU virt RISC-V 64")`・`("visionfive2", "riscv64", "visionfive2", "StarFive VisionFive 2 RISC-V 64")`、`BOARD_LABELS`、`BOARD_VARIANTS`（default）、`MENU_CPUS` に **2 行** `("riscv64", "RISC-V 64", "riscv64-virt", "QEMU virt")`・`("riscv64", "RISC-V 64", "visionfive2", "VisionFive 2")`（同じ CPU に 2 つ以上の board があると Board の menu が出る: 731〜741 行）、`directory` の表。
- `config/ci/config-riscv64-virt.mk`・`config-visionfive2.mk`（CI の image。`ZEDBSD_USER_PROGRAMS` は最初は base の最小: `init`・`login`・`sh`（既定の shell）・`getty` 相当・`uname`・`ls`・`cat`。rpi4 の一覧から desktop を外す。driver の既定を上の様に `n`）。試験の image は `plan/ws256/tests/config-riscv64-virt.mk` での build と個別の file の複写だけで作る（AGENTS.md 2026-10-04 の規則）。
- CI の binary cache: p002 の注のとおり、riscv64 を CI で build する前に RISCV 入りの LLVM の cache の asset（patch level を上げる）が要る（Q1）。

### 4.2 kernel と HAL

- 対象: `vmunix`（HAL の riscv64 ＋ `src/kern/*` ＋ drivers のうち platform に依らない物 ＋ §2.7 の platform と driver）。既存の kernel の C は arch を `KERN_USER_ABI_LP64`・`__aarch64__`/`__x86_64__` の `#if` で見る所がある（`include/uapi/reg.h` は `#error`、`include/libc/setjmp.h`、`userland/base/libc/*.c`）→ `__riscv` の枝を足す（p007・p008 で grep して全部）。
- warning 0 の `make -j16 build/riscv64-virt/vmunix` が build の関門。

### 4.3 userland（libc・rtld・crt・Noct）

- `userland/base/libc/syscall-riscv64.S`（`__syscall6`、`__signal_restorer`）、`src/libc/crt/crt0-riscv64.S`・`crt1-riscv64.S`（`_start`: argc/argv/envp を `sp` から、`gp` の設定（`__global_pointer$`）、`__libc_init`、`main`、`_exit`）、`src/libc/setjmp-riscv64.S` と `include/libc/setjmp.h` の `__riscv` の枝（clang 23 の riscv64 は `__builtin_setjmp` を「not supported for the current target」と拒む: 2026-10-18 に確かめた。aarch64 と同じく自前で、s0〜s11・ra・sp・fs0〜fs11 の 26 word）、`static-tls.c`（`tp` の読み）、`pthread.c`（relax: `pause` は Zihintpause が無い U74 でも `fence w,0` の encoding で合法な hint）、`posix.c`（`machine` = "riscv64"）、`include/uapi/reg.h`（`struct reg { uint64_t r_x[32]; uint64_t r_pc; }`、`struct fpreg { uint64_t fp_f[32]; uint32_t fp_fcsr; }`）、`include/libc/elf.h` の `R_RISCV_*`。
- `src/rtld/rtld.c`: `HAL_ARCH_RISCV64` の `RTLD_MACHINE EM_RISCV`・`RTLD_RELATIVE R_RISCV_RELATIVE`、relocation の switch（64、RELATIVE、JUMP_SLOT、GLOB_DAT は無い（64 を使う）、TLS_DTPMOD64・DTPREL64、IRELATIVE、COPY。**TPREL64（IE）は拒む**（§2.6 の M10）。TLSDESC は `-mtls-dialect=trad` で出さない）。rtld の 19 箇所の arch の `#if` を全部見る（§2.7）。
- `userland/base/noct/zedbsd-riscv64.cmake`（`CMAKE_SYSTEM_PROCESSOR riscv64`、triple、flags `-march=rv64gc -mabi=lp64d -mno-relax -ftls-model=global-dynamic -mtls-dialect=trad ...`）。`tools/build/check-dynamic-elf.py` の `MACHINES` に `riscv64`（243、ELF64、LE、許す relocation `{0, 2, 3, 5, 7, 9, 58}`。aarch64 の行の形）。`platform/common/userland-*.mk` は platform から flags を受ける形なのでそのまま。
- userland の flags: `-march=rv64gc -mabi=lp64d -mno-relax -fno-plt -ftls-model=global-dynamic -mtls-dialect=trad`（PLT は使ってよいが rtld の実装が簡単な方を p007 で選ぶ）、link は `--no-relax`。`-z max-page-size=4096` は lld の riscv の既定が 4096 なので不要。

### 4.4 Noct・package

- host の Noct は変えない。target の Noct（`noct` の user program）は cmake の toolchain file で build（上）。package（`userland/packages/`）は最初の到達目標の外（login に要らない）。

### 4.5 toolchain への依頼（p010、Q1。P2 は触らない）

1. `toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch` を zedbsd9 に: (1) `clang/lib/Basic/Targets.cpp` の riscv64（と riscv32）の OS の switch に `case llvm::Triple::ZedBSD: return std::make_unique<ZedBSDTargetInfo<RISCV64TargetInfo>>(Triple, Opts);`、(2) `clang/lib/Driver/ToolChains/ZedBSD.cpp` の emulation に `riscv64 → elf64lriscv`、(3) `TripleTest.cpp` に riscv64 の例、(4) `version.mk` の `ZEDBSD_LLVM_PATCH_LEVEL := zedbsd9` と cache の asset。
2. **`toolchain/llvm/sysroot.mk` に riscv64 の sysroot**（M9。2026-10-18 に読んだ: 今は amd64・i386・arm64 の 3 つ。`ZEDBSD_BUILD_SYSROOT` の呼び出しを 1 行足す: `riscv64`、`riscv64-unknown-zedbsd`、`-march=rv64gc -mabi=lp64d -mno-relax`、`HAL_ARCH_RISCV64`、`src/libc/crt/crt0-riscv64.S`・`crt1-riscv64.S`、`platform/riscv64/user.ld`、machine の名前 `RISC-V`、`-DKERN_USER_ABI_RISCV64 -DKERN_USER_ABI_LP64`、libc の .S（`setjmp-riscv64.S`）、compiler-rt と LLVM の builtins の一覧（arm64 の列を基に、fp128 の builtins を足すかは §2.6）、`include/hal/arch/riscv64.h` を sysroot に写す一覧、`sysroot-riscv64` の goal、`check-user-elf.py --machine riscv64`）。user の link は sysroot の builtins・crt0・libc を使うので **p007 の前提**。
3. CI の binary cache の asset（p002 の注）。

kernel・HAL・loader の Phase は `riscv64-unknown-elf` ＋ `-DKERN_UAPI_NATIVE -DKERN_KCRT_NATIVE`（§1.8）で進められるので、patch を待たない。差分の具体（patch の hunk と sysroot.mk の行）は p007 の着手の前に P2 が `plan/ws256/proposed/llvm-zedbsd9.md` に書いて Q1 に送る（toolchain の変更はユーザーの許可: 2026-09-28 の規則）。crt0/crt1/setjmp/user.ld の source は p007 の物（P2）で、sysroot.mk はそれを参照するだけなので、p010 は p007 の source が揃ってから最後に流す（p010 の patch の部分は先に流せる: §6）。

## 5. 試験の方法（決定: 2026-10-18 ユーザー「virt に画面を付ける」）

ユーザーの決定（[ws.md](ws.md) 5、クリック）: 「QEMU の virt に ramfb か virtio-gpu を付け、boot-test.sh の画面で判定する（serial の log で判定する例外は作らない）」。この節はその適用。M11（p003・p008 の判定が serial の log の読み取りだった）はこれで解く。

### 5.1 画面の作り方: ramfb、HAL の framebuffer の console

| 選択 | 決めた物 | 理由 |
| --- | --- | --- |
| device | **`-device ramfb`** | fw_cfg の file に 28 byte の設定を 1 回書くだけで guest の RAM が画面になる（§1.5。driver は fw_cfg の DMA の数十行）。virt は ramfb を許し fw_cfg は DMA 付き（確かめた）。virtio-gpu は virtqueue・resource・scanout・transfer・flush の command の driver が要り、tree に virtio の driver が無い（grep: `src/drivers/` に virtio は無い） |
| console の出所 | **kernel 側: HAL の `bsp-virt`**（fw_cfg を叩いて ramfb を設定し、rpi4 と同じ 80x25 の text の console を描く） | UEFI の GOP は U-Boot の道（p004）でしか無く、しかも bochs-display を付けて U-Boot の video の driver が動く時だけ（§1.5）。p003・p008・p007 は `-kernel` の直の起動で UEFI が無い。HAL が作れば**両方の入口で同じ画面**になり、`boot-test.sh` の判定が全 Phase で同じ形になる |
| 解像度 | **640x400、XRGB8888、stride 2560**（1 MiB、`.bss`） | `boot-test.py` の読み取りは 8x16 の cell を左上から並べて読む（`read_cells`、font は `src/drivers/platform/pcat/graphics/vgafont.c`）。640x400 は 80x25 の cell がちょうど入り、中央寄せも余白も要らない。rpi4 の 640x480 中央寄せも読めるが、揃える方が単純 |
| font | PC/AT の 8x16（rpi4 の `font.c` と同じ内容を `bsp-virt/font.c` に） | `boot-test.py` が照合する font と同じ物でなければ読めない（§7 #16） |
| 文字の経路 | HAL の `hal_putc` が SBI の console と framebuffer に出す（rpi4 の `cons.c` の約束。`kernel_putc` が公開されたら HAL は委ねる）。kernel の console（§2.7 の glue）は rpi4-console.c と同じく teletype の putc で `hal_putc` を呼ぶ | login の prompt（getty の出力）が画面に出る道が rpi4 で動いている形 |
| 入力 | UART（`-serial unix:SOCK,server,nowait`）に `plan/tools/guest/serial.py` が文字を**書く**（login の名前、`uname -m`） | 「guest の操作はシリアルで対話」（AGENTS.md）の許された形。判定は画面で行い、socket から読んだ文字は判定に使わない |

VF2 の実機には画面が無い（HDMI の display は範囲外）。実機の判定は §5.3（判断 #15）。

### 5.2 Phase ごとの判定（QEMU。T1 が流す、P2 は起動しない）

共通: `boot-test.sh` に `BOOT_MODE=riscv64-virt`（`-M virt -smp 4 -m 2G -device ramfb -display none -serial unix:… -qmp unix:… -kernel KERNEL -initrd INITRD -append "…"`）と `BOOT_MODE=riscv64-uboot`（`-kernel u-boot.bin`、`-drive …virtio-blk-device`、同じ ramfb）を足し、環境変数 `BOOT_PATTERN` で `boot-test.py` の `--pattern`（既定 `^(?:\S+ )?login: ?`）を変えられるようにし、login の後に serial.py で command を打って 2 枚目の画面を読む段を足す（p009）。合否は `login.png` と読み取った `.txt`。QEMU の console log・serial log は読まない。

| Phase | 流す物 | 画面で見る物（合格） |
| --- | --- | --- |
| p003 | `riscv64-virt`、`-smp 4`。`-initrd` 無し | HAL の boot log の行（arm64 の `ARM64 PAGING PASS`・`ARM64 TIMER TICK` と同じ流儀、hardware の振る舞いの記録で試験の情報ではない）: `RISCV64 PAGING PASS`、`RISCV64 SBI v<spec の版>`（と拡張の有無）、`RISCV64 TIMER TICK`、`RISCV64 SMP 4 ONLINE`、続く kernel の log と、root が無いための `vfs: … find loader boot partition` の失敗で止まった画面。`BOOT_PATTERN` はこれらの行。追加の走り: `-append cpus=1` で `1 ONLINE`; `-cpu rv64,sstc=off,svpbmt=off`（M12 (3)。timer は常に SBI なので差は DT の parse だけ）で同じ画面 |
| p003（M12 (2)、任意） | `-machine sifive_u -smp 5 -m 2G -kernel vmunix`（hart 0 は E51（MMU 無し）、hart 1〜4 は U54。PLIC の第 1 context が M だけ、UART は SiFive の物（kernel は触らない: console は SBI）。timebase 1 MHz） | sifive_u に fw_cfg は無く（`config SIFIVE_U` に無い: 確かめた）画面が作れない。観察は **gdbstub**（`-gdb unix:…`）で HAL の変数（online の数、boot hart の id ≠ 0、ticks の増え）を読む。これは解析の道具の用法（AGENTS.md「不具合は gdbstub・monitor・QMP で解析する」）で log の読み取りではないが、**受け入れの判定に使うかはユーザーの判断（#17）**。推測: QEMU は fw_dynamic の boot_hart を 0 に固定する（§1.5）ので、OpenSBI が E51 を boot hart にして kernel が「boot hart に MMU が無い」で FATAL する可能性がある。その時はこの走りは情報として記録し、hart id ≠ logical id の道は実機（p005）で初めて通る |
| p008 | `riscv64-virt`、`-initrd root.img`（MBR の image） | ramdisk の partition の log、root の mount の成功、`init` を exec できない（userland が無い）kernel の message。`BOOT_PATTERN` はその message |
| p007 | 同じ、root.img に最小の userland | **既定の pattern で login の prompt**（受け入れ 1 の UEFI 以外）。続けて serial.py が `root`⏎・`uname -m`⏎ を打ち、2 枚目の画面に `riscv64` と shell の prompt が出る（UART の **RX の割り込み**＝PLIC の証拠、syscall・exec・rtld の証拠）。SMP 4 は `RISCV64 SMP 4 ONLINE` の行（画面の上に残る。流れて消えた時は `kmsg` の表示で） |
| p004 | `riscv64-uboot`（U-Boot → BOOTRISCV64.EFI → vmunix） | p007 と同じ画面（受け入れ 1 の完成）。U-Boot の間は画面が空（ramfb は HAL が設定する）。loader の失敗は画面に出ない（GOP 無し）→ loader は `ConOut` に出す（U-Boot の EFI の console は UART。T1 は serial.py の socket に残った文字を**解析の助け**にだけ使い、判定にしない） |
| p005 | 実機 | §5.3 |

QEMU の DTB（`-machine virt,dumpdtb=…`、guest を走らせない）は T1 か Q1 が 1 回だけ取って `plan/ws256/tests/virt.dts`（`dtc -I dtb -O dts`）に置き、P2 の host の試験（FDT の parser）の入力にする。

### 5.3 VisionFive 2 の実機（T1/T2）

- 接続: USB-UART（板の 40 pin の UART0、115200）を host に。電源は Pi と同じ SwitchBot の plug（`plan/tools/switchbot.py cycle`）で Q1 が入れ直す（#10）。SD は host で書く（Pi と同じ運用）。
- **判定の方法は未決（#15）**。画面が無い（HDMI の display driver は範囲外、U-Boot の video も JH7110 では未確認）。案: (A) `serial.py` に `--tty` を足し、**対話**（`expect 'login: '` → `root` → `uname -m` の応答）で判定する（「guest の操作はシリアルで対話」の形だが、判定に使うのは新しい用法なので**実機に限る例外**としてユーザーの明示の判断が要る）、(B) 実機の受け入れは HDMI の driver（別 WS）の後に延ばし、WS256 の実機の到達は「起動の log を serial.py で**観察**して記録する（判定ではない）」に留める、(C) 板の LED・GPIO の合図（driver を足す: 範囲が増える）。P2 の推し: (A)。ユーザーの決定 5 は virt の話で、実機にはまだ答えが無い。
- 受け入れ（(A) の時）: 10 回の電源の入れ直しで 10 回 login（WS208 の p005 の形）。SMP 4 core は `cpus=` の無い既定で、timer と PLIC の割り込みは login の成立（RX の割り込み）と tick の進み（`uptime`）で示す。`mimpid`・`marchid` を log に出し errata の該当を記録する。
- 実機の証拠と QEMU の証拠は分けて書く。

### 5.4 host の試験（P2 が流す。短い物）

- FDT の parser: `plan/ws256/tests/virt.dts`（§5.2）と手で書いた JH7110 風の小さな DTS（dtc は host にある）で `/cpus`（S7 の除外、hart id の表、timebase）、PLIC の context の表（virt の並びと JH7110 の並び）、UART の reg-shift、`/memory`・`/reserved-memory`・`/memreserve/`、fw_cfg の node。
- Sv39 の table の作り方（`space.c` の leaf/table の判定、direct map の index、予約を含む 1 GiB の割り方、root の上半分が init の後に変わらない事、A/D と SW_DIRTY の扱い）を host の fixture で。
- trap の frame の offset の `_Static_assert`、percpu の `user_sp`・`kernel_sp` の offset（trap.S が使う定数と C の struct の一致）。
- fw_cfg の DMA の制御の構造と ramfb の設定の **byte 列**（big-endian の詰め方）を host で組んで期待の byte と比べる。
- PLIC の mask/unmask が priority の register に 0/1 を書き enable を触らない事（register の読み書きを差し替えた fixture）。
- `elf64.c` の `EM_RISCV` の plan と place（link の定数の header の切り替え、M13 の式）。
- loader の PE の header の checker（python）。
- SBI の呼び出しの encoding（`a7`/`a6` の詰め方）は inline asm なので host では流せない → code review。

## 6. Phase の表

[ws.md](ws.md) の p003〜p006 の ID と意味は保ち、p007〜p010 を足す（p009 の中身は初版の「画面の判定」から「試験の道具」に変えた: 画面は p003 に入った）。実行の順は ID の順ではない。LW は 1 LW ≈ 実時間 15〜25 分（master の較正）。粗い（±50%）。

| Phase | 範囲 | 受け入れの条件 | 依存 | LW |
| --- | --- | --- | --- | --- |
| p001 | この設計（改訂 2） | design.md・proposed/ の 2 file、reviewer 1 回の全件の反映（§11）、Q1 の合格 | — | 済 |
| p002 | toolchain の LLVM に RISCV（Q1） | cleared（2026-10-18） | — | 済 |
| **p009**（内容を変更） | 試験の道具: `plan/tools/boot-test.sh` の `BOOT_MODE=riscv64-virt`・`riscv64-uboot`、`BOOT_PATTERN`、login の後に serial.py で command を打って 2 枚目を読む段、`plan/ws256/tests/config-riscv64-virt.mk` と image の手順、QEMU の DTB（`dumpdtb`）を `tests/virt.dts` に。`serial.py --tty`（実機、#15 の後） | boot-test.sh が riscv64-virt の command を組み、pattern を変えられ、2 枚目を読む（既存の mode の挙動は不変: amd64 の image で 1 回確かめる）。tools は Q1 の path なので **Q1 が行うか、P2 に `plan/tools/boot-test.sh`・`boot-test.py`・`guest/serial.py` だけの変更を許す**（#18） | — （p003 の T1 の試験の前に要る） | 2〜3 |
| **p003** | HAL の芯（QEMU virt、直の起動）: `include/hal/arch/riscv64.h`・`arch.h` の 1 行（**#13 の承認の後**）、locore・trap・dispatch・cmain・fdt・sbi・percpu・smp・int・irq・plic・clock・page・space・task・debug・asm・lib、bsp-virt（board・fwcfg・framebuffer・cons・font）、**最小の `src/kern/platform/riscv-virt.c` と `src/kern/elf.c` の `EM_RISCV` の枝**（M8）、`platform/riscv64/{Makefile,vmunix.mk,vmunix.ld,toolchain.mk,run.mk}`、`Makefile`・menuconfig の riscv64-virt、`config/ci/config-riscv64-virt.mk`。kernel は `kernel_entry` から root の mount の失敗で止まる所まで | host の試験（§5.4）PASS、`make build/riscv64-virt/vmunix` が warning 0、T1 の QEMU（§5.2 の p003 の行）: 画面に `RISCV64 PAGING PASS`・SBI の版・`TIMER TICK`・`SMP 4 ONLINE`・root の失敗、`cpus=1` で `1 ONLINE`、`-cpu rv64,sstc=off,svpbmt=off` で同じ。sifive_u は #17 の決定に従う | #13 の承認が着手の前。p009 が T1 の試験の前 | 28〜36 |
| **p008** | kernel 側: `riscv-virt.c` の残り（console・ramdisk・device の表）、`src/drivers/serial/ns16550.c`（RX）、`src/drivers/generic/serial-console.c`、`src/drivers/generic/ramdisk.c`、MBR の root.img の `rootfs.mk`、FDT・initrd の handoff（**#9 の決定の後**。(a) なら HAL の名前付き object、(b) なら boot.h の差分を適用）、kernel の arch の `#if` の棚卸し（vfs.c・elf.c・ptrace.c・syscall.c・signal.c・main.c・klog.c・entry.c・net の 2 header、`reg.h`・`signal.h`） | build warning 0、host の試験（ramdisk の read、partition の表の fixture）、T1: 画面に ramdisk の mount と「init が無い」 | p003。#9 の決定が着手の前 | 10〜12 |
| **p007** | userland の riscv64: (a) `crt0/crt1-riscv64.S`・`setjmp-riscv64.S`・`syscall-riscv64.S`・`user.ld`（p010b の sysroot の入力。先に書いて Q1 に渡す）、(b) libc の枝（static-tls・pthread・posix・setjmp.h・reg.h・sqrt.c）、rtld の relocation と 19 箇所の `#if`、Noct の cmake、`check-dynamic-elf.py` の riscv64、`ZEDBSD_USER_PROGRAMS` の最小の集合の build、root.img の userland | build warning 0、host の rtld の試験（riscv64 の relocation の fixture、TPREL の拒否）、T1: **QEMU virt で画面に login の prompt、serial.py で打った `uname -m` の答え `riscv64` が画面に出る**（受け入れ 1 の UEFI 以外の全部） | p008、p010a（patch）が (b) の前、p010b（sysroot）が (b) の link の前 | 14〜18 |
| **p010**（Q1） | toolchain: (a) LLVM の patch zedbsd9（§4.5 の 1）、(b) `sysroot.mk` の riscv64（§4.5 の 2、M9）、(c) CI の cache の asset。ユーザーの許可（#14） | (a) `riscv64-unknown-zedbsd` で `__ZEDBSD__` が定義され、`clang -shared` が `elf64lriscv` で link する。(b) `make sysroot-riscv64` が通り `check-user-elf.py --machine riscv64` PASS | (a) は p001 の後いつでも。(b) は p007 (a) の source の後 | 2〜3 |
| **p004** | loader の riscv64（§3.3）、`bootloader/include/riscv64-handoff.h`、`platform/riscv64/bootloader.mk`・`disk-image.mk`（ESP＋root の 2 partition、GPT）、U-Boot `qemu-riscv64_smode` の導入（Q1、apt）、`run.mk` の uboot の行、`elf64.c` の arch の書き分け | checker PASS、build warning 0、T1: QEMU virt で **U-Boot → BOOTRISCV64.EFI → vmunix → 画面に login、`uname -m`**（受け入れ 1 の完成） | p007、p009 | 10〜14 |
| **p005** | VisionFive 2: bsp-visionfive2（ccache の dcache 操作、`mimpid`・`marchid` の log、errata の緩和の有無の記録、framebuffer 無し）、`src/kern/platform/visionfive2.c`、DW UART の RX の癖（busy detect）、`visionfive2` の platform/config/menuconfig、SD の 4 partition の image（SPL・itb・ESP・root）、U-Boot の SD からの起動の手順、実機の試験の道具（tty、plug）、実機の login | T1/T2: 実機で 10 回 login（SMP 4、tick、RX の割り込み）。判定の方法は #15。QEMU と実機の証拠を分ける | p004、板の接続・電源・UART（ユーザー、#10）、#15 の決定、#2・#8 の確認 | 8〜12 ＋ debug 4〜8 |
| **p006** | 規約の全文の見直し（WS が変えた C の全部） | clang-format と全文の照合、build warning 0 | 全部の後 | 2〜3 |

合計: 76〜106 LW（うち Q1 の p009・p010 4〜6、実機の debug 4〜8）。順: p009 ∥ p003（#13 の後）→ p008（#9 の後）→ p007（p010a が先、p010b が途中）→ p004 → p005（#15・板）→ p006。p010a と p009 は p003 と並行できる。

## 7. ユーザーの判断が要る点と既定の案

### 7.1 決まった物（2026-10-18、ws.md の 5・6）

| # | 点 | 決定 | 反映 |
| --- | --- | --- | --- |
| 1 | 外部の code の参照の方針 | 「WS245 と同じ」: 手順は参考にし、code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced: を書く | §0.2 |
| 6 | 試験の判定（virt に画面が無い） | 「virt に画面を付ける」: ramfb か virtio-gpu を付け boot-test.sh の画面で判定、serial の log で判定する例外は作らない | §5（ramfb、HAL の console、Phase ごとの判定）。初版の (B)（serial.py の対話で判定）は virt では捨てた。実機は #15 |

### 7.2 残る物（既定の案つき）

| # | 点 | 既定の案（P2） | 他の案と理由 | 要る時 |
| --- | --- | --- | --- | --- |
| 2 | 板の版と memory | 未決のまま設計は影響を受けない（v1.2A/1.3B の差は GbE だけ、memory は DT/UEFI から読む）。接続した時に U-Boot の `fdt print /model`・板の刻印・`bdinfo` で記録 | — | p005 |
| 3 | SD・eMMC・GbE・USB・display | **別の WS**（WS256 は UART の login まで。dcache の操作だけ p005 で用意） | 同じ WS に入れると到達目標が二つになる | p005 の前 |
| 4 | root の媒体 | **initrd**（MBR の `root.img`、§2.7。B1 を (a) で解いた）。QEMU の直の起動は `-initrd`。SD は別 WS | SD を root にすると DW MSHC の driver が WS256 に要る（4〜8 LW、実機でしか確かめられない） | p008 の前 |
| 5 | Sv39 か Sv48 | **Sv39**（技術の既定: U74 は Sv39 だけ。user 256 GiB、direct map 128 GiB で足りる）。`RISCV64_VA_BITS` で持ち Sv48 は Future Work | Sv48 を今やると U74 で動かない物を QEMU で作る | 反対が無ければ p003 |
| 7 | QEMU の UEFI の firmware | **U-Boot `qemu-riscv64_smode`**（Debian の package、apt）。GOP は要らない（画面は HAL の ramfb） | edk2 RiscVVirt は Debian に無く build が要る | p004 |
| 8 | VF2 の firmware と DTB | **Debian の `u-boot-starfive` の SPL と itb を SD に置き MSEL=SD で起動**（flash を触らない）。DTB は U-Boot の control DT | 板の vendor の firmware が EFI の boot protocol を持つなら flash のままでも動く（板で `version` を見てから）。DTS を tree に入れる案は GPL-2.0 OR MIT の file の配置の決定が要る | p005 |
| 9 | FDT・initrd・boot hart の渡し方（`include/kern/boot.h` を変えるか） | **(a) boot.h を変えない: HAL の `hal_get_arch_handoff("riscv64.fdt")`・`("riscv64.initrd")`・`("riscv64.boot-hart")`**（hal.h の契約の中。amd64 の `"acpi.rsdp"` と同じ形。HAL の責務の追加として #13 と一緒に承認を得る） | (b) rpi4 と同じく boot.h に `struct riscv64_boot_handoff` を足す（差分 [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md)。boot.h は「記録された決定の後にだけ変える」ledger、rpi4 の形には「To be removed. Use the common one.」の注がある）。任意: `KERN_DEV_RAM` の class | p008 の前 |
| 10 | 実機の試験の接続 | USB-UART を host に、電源は SwitchBot の plug（Pi と同じ運用、Q1 が入れ直す）、SD は host で書く | — | p005 |
| 11 | kernel の FP の方針 | eager（技術の既定、arm64 と同じ）。kernel の C は FP 無し | lazy は最適化。後 | 反対が無ければ p003 |
| 12 | 板の HDMI の display | 範囲外（別 WS）。login は UART | #15 と関係 | — |
| 13 | HAL の API の差分の承認 | [proposed/hal-riscv64.md](proposed/hal-riscv64.md) の 2 件（`include/hal/arch.h` の 1 行、`include/hal/arch/riscv64.h` の新規。改訂 2 で comment を直した）＋ #9 (a) の名前付き object（HAL の責務の追加）。hal.h は不変 | — | **p003 の着手の前** |
| 14 | toolchain（p010） | §4.5: patch zedbsd9、sysroot.mk の riscv64、CI の cache。Q1 がユーザーの許可を得て行う | 待たずに kernel を `riscv64-unknown-elf` で進める（§1.8） | p007 の前 |
| 15 | **実機の判定の方法**（画面が無い） | **(A) serial.py の対話（`--tty`）で判定: 実機に限る例外**として明示の判断を求める | (B) 実機の受け入れを HDMI の driver（別 WS）の後に延ばす（WS256 の実機は観察の記録まで）、(C) LED・GPIO の合図（driver が増える）。§5.3 | p005 の前 |
| 16 | font の置き方 | `bsp-virt/font.c` に rpi4 の `font.c` と同じ内容を置く（技術の既定。arm64 を触らない） | `src/hal/common/font-8x16.c` に移して arm64・riscv64 で共有（arm64 の mk を 1 行変える: WS256 の範囲外なので Future Work）。rpi4 の file を riscv64 の mk から compile する案は arch をまたぐ include の path になるので避けた | p003 |
| 17 | sifive_u の走りの判定 | gdbstub で HAL の変数を読む（解析の道具の用法）。**受け入れに数えるかはユーザー**。数えないなら M12 (2) は情報の記録に留め、hart id ≠ logical id の道の確認は実機（p005）に残す | 画面が作れない（fw_cfg 無し）。§5.2 | p003 の T1 の前 |
| 18 | p009 の tools の変更の担当 | Q1 が行う（`plan/tools/` は Q1 の path） | P2 に `boot-test.sh`・`boot-test.py`・`guest/serial.py` だけの変更を許す（Q1 の指示で） | p003 の T1 の前 |

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
- **Svpbmt/Zicbom/Sstc を DT で見て使う**（初版）: QEMU で動く道と実機で動く道が分かれ、実機まで片方が試されない（M12）。WS256 では使わず、uncached・WC・NOCACHE・WRITETHRU は UNSUPPORTED、timer は SBI。使うのは実機の後の Future Work。
- **single step の hardware 実装**: S-mode では無い。ptrace の step は後（kernel の `ebreak` の挿入）。
- **vfs.c に partition の無い root の道を足す**（B1 の (b)）: kernel 全体の root の選択の変更で、別の決定と Phase が要る。ramdisk の中身を MBR の image にすれば kernel を変えずに rpi4 の道で済む（(a)、§2.7）。
- **root の selector `rootpart=UUID=`＋GPT の root.img**（初版）: `rootpart=` は `DISK_PARTITION` の device を要るだけで動きそうだが、legacy の autoroot と marker の形の方が rpi4 で動いている道で、`zedbsd.cfg` の UUID を image ごとに合わせる手間も無い。
- **virtio-gpu を画面に**: virtqueue と 2D の command の driver（tree に virtio の driver が無い）。ramfb は fw_cfg の DMA の数十行（§5.1）。
- **UEFI の GOP の framebuffer を loader が渡す**（amd64 の形）: U-Boot は bochs-display を付けた時だけ video を持つと推定され（§1.5）、直の起動（p003・p008・p007）には無い。HAL の ramfb は両方の入口で同じ画面になる。GOP は p004 で U-Boot が出していれば loader が handoff に入れてよいが、HAL は使わない（将来の実機の display の WS で考える）。
- **loader が Sv39 の bootstrap table を作る**（初版）: kernel の locore が両方の入口で自分の boot の table を作るので要らない（minor 15）。loader は `satp` = 0 で物理の `_start` に跳ぶ。
- **kernel 半分の root の entry を space の生成時に複写し、後から足す**（初版）: 後で作る kernel の table が既存の space に届かない（M1）。init で確定して封じる。
- **mask を PLIC の enable bit で**（初版）: enable の落ちた source の complete は捨てられる（規格、M3）。priority 0/1 で mask/unmask。
- **boot.h に `struct riscv64_boot_handoff`**（初版の既定）: boot.h は決定の後にだけ変える ledger。名前付きの handoff object（hal.h の契約の中）を既定にし、boot.h の差分は案として残した（#9）。
- **serial.py の対話で virt の判定**（初版の (B)）: ユーザーの決定 6 で virt は画面。実機では #15 の案 (A) として残る。
- **試験用の boot parameter `hal.selftest=1`**（初版）: 試験の進め方を source に入れる事になる（guardrail 2026-10-17）。普通の boot log の行で判定する。
- **UART の TX を kernel の driver で**: HAL の SBI の console が TX を持つ（DBCN の console_write は buffer ごと 1 回の ecall）。kernel の ns16550 は RX だけでよく、TX を driver に移すのは tty の速さが要る時の Future Work。

## 9. 失敗と回復・並行性・資源（設計の要点の確認）

- 失敗: 秒の timeout で来ない hart は FATAL と `cpus=1` の助言（arm64 D6）。SBI の拡張が無い（probe 失敗）なら: TIME/IPI/HSM/RFENCE は必須で FATAL（OpenSBI 1.6 は全部ある）、DBCN が無ければ legacy の putchar、SRST が無ければ legacy の shutdown か `wfi` の loop。PLIC・UART が DT に無ければ FATAL（message は SBI console に出る）。loader: boot hart の protocol が無ければ `/chosen/boot-hartid`、それも無ければ止める（間違った hart id で HSM を呼ぶと他 hart が起きない）。
- 並行性と lock: arm64 と同じ所に同じ lock（IRQ の slot の spin、pmem、space、task list、PLIC の priority・enable の読み書き）。lock の順も arm64 に合わせる。`hal_atomic_uint_try_acquire` は `amoswap.w.aq`、`hal_atomic_relax` は `pause`（encoding 0x0100000f: `fence` の形で pred = W、succ = 無し（minor 2: 初版の「predecessor も successor も無い」は誤り）。Zihintpause の無い U74 では普通の fence として合法、assembler も `-march` に無くても受ける: 確かめた）。
- trap の入れ子と per-CPU（M2）: `sscratch` の約束（user 中 = record、kernel 中 = 0）を入口で直ちに回復するので、kernel の中で起きた trap（page fault・IPI・timer）は user の `tp` を record と誤認しない。kernel の stack の先頭は percpu の `kernel_sp`（task の切り替えで更新）。
- 割り込みと DMA: 装置の割り込みは boot hart の context だけで受ける（分散は Future Work）。DMA の coherency は VF2 の ccache で（§1.6）。ramfb は DMA ではない（QEMU が guest の RAM を読む）。
- 資源の所有と寿命: HAL の静的な表（percpu ×8、IRQ の slot ×ndev、boot block ×8、kernel 半分の第 2 段の table ≈ 70 page、framebuffer 1 MiB）は kernel と同じ寿命。task・space・table の page は arm64 と同じ所有（kernel_alloc と hal_pmem_alloc）。DTB と initrd と handoff の物理は page.c が予約し、kernel の platform が使い終わっても解放しない（単純さ優先。initrd の解放は Future Work）。

## 10. レビューと改訂

- 改訂 1（2026-10-18 P2 初版、e79c3846d）。design-reviewer のレビュー 1（[design-review-1.md](design-review-1.md)、blocker 1・major 13・minor 24）。
- 改訂 2（2026-10-18 P2、q1243-i02）: レビュー 1 の全件を反映（§11）、ユーザーの決定 5・6 を入れた（§0.2・§5）、Phase の表を直した（§6）、判断の点を決まった物と残る物に分けた（§7）。確かめた事（2026-10-18、host の tree と公開の source の読み取り。QEMU・実機は起動していない）: `src/kern/vfs.c` の root の選択、`bsp-rpi4/boot.c`・`platform/rpi4.c` の handoff と device の表、`include/kern/boot.h` の全文、`src/hal/arm64/space.c` の table の作り方と SW_DIRTY、`src/kern/elf.c` の machine と static TLS、`src/rtld/rtld.c` の TLS、`userland/base/libc/static-tls.c`、`toolchain/llvm/sysroot.mk`、`src/kern/uaccess.c`、`include/hal/hal.h` の console・space・handoff の契約、`include/kern/platform.h`、`Makefile`・`tools/menuconfig.py` の platform の表、`plan/tools/boot-test.sh`・`boot-test.py`（画面の読み方）、`bootloader/uefi/elf64.c`、`bootloader/include/amd64-handoff.h`、Linux 6.8.12 `irq-sifive-plic.c`（scratchpad の展開）、QEMU v10.0.0 の `hw/riscv/virt.c`・`boot.c`・`Kconfig`・`hw/display/ramfb.c`・`docs/specs/fw_cfg.rst`・`target/riscv/cpu.c`、U-Boot v2025.01 の `configs/qemu-riscv64_smode_defconfig`・`board/emulation/qemu-riscv/Kconfig`・`doc/board/starfive/visionfive2.rst`。確かめられなかった物は本文で「推測」と書いた（QEMU rv64 の V の既定、U-Boot の Debian の binary の video の有無、`IMAGE_FILE_MACHINE_RISCV64` の値、U74 の rdtime の emulation と A/D の hardware、OpenSBI の IPI の届け方、sifive_u での boot hart、arm64 の `long double` の builtins）。

## 11. レビュー 1 の反映

[design-review-1.md](design-review-1.md) の全件。「直した節」はこの文書の節。

| 項目 | 扱い | 直した節 / 直さない理由 |
| --- | --- | --- |
| B1 initrd を root にできない | **(a) を既定**: ramdisk の中身を rpi4 と同じ MBR の image（FAT p1 ＋ marker 付き UFS p2）、handoff は MULTIBOOT/MBR/1/0x80、platform は MBR の scheme と bios_id 0x80 の device。vfs.c の該当の行を読んで確かめた | §2.7、§3.4、§7 #4、§8（(b) は選ばなかった案） |
| M1 kernel 半分の複写 | init で第 2 段の table を全部確保し、root の上半分を封じる（assert）。uncached の窓は無し | §2.5、§2.2 space.c |
| M2 sscratch と kernel の stack | 入口・出口の列を書いた。percpu に `kernel_sp`・`user_sp`。SUM は常に 0 | §2.3、§2.2 trap.S・percpu、§9 |
| M3 PLIC の mask | priority 0/1 で mask/unmask（Linux の source で確かめた）。init の claim の空読みは complete まで | §1.3、§2.2 irq.c・plic.c |
| M4 direct map と PMP・予約 | 予約を含む 1 GiB は 2 MiB／4 KiB に割って外す。UEFI の道でも DT の予約を重ねる | §2.5、§2.2 page.c、§3.4 |
| M5 `fence.i` | loader の transition と kernel の `_start`・secondary の入口に | §3.3、§2.2 locore.S |
| M6 識別 map の 1 GiB | boot の table は RAM の載り得る範囲全体を 1 GiB の leaf で。QEMU の DTB の置き場（RAM の終わり − 2 MiB 揃え）を boot.c で確かめた | §2.5、§1.5 |
| M7 handoff の二重 | 2 層に分けた（loader→HAL は ZBL6 の `zbl6_riscv64_handoff`、HAL→kernel は `kern_boot_handoff`）。boot.h の変更は差分を [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) に置き、既定は boot.h を変えない名前付き object。ユーザーの判断 #9 | §3.3、§3.4、§7 #9、proposed/ |
| M8 p003 の link に要る物 | 最小の `riscv-virt.c` と `elf.c` の `EM_RISCV` の枝を p003 に | §2.7、§6 p003 |
| M9 sysroot.mk | p010 (b) に入れた（Q1、ユーザーの許可）。p007 (a) の source が先 | §4.5、§6 p010・p007 |
| M10 TLS の ABI | 設計で決めた: arm64 と同じく static TLS 無し・全部 dynamic、`-ftls-model=global-dynamic -mtls-dialect=trad --no-relax`、TPREL64 を checker で拒む。elf.c・rtld.c・static-tls.c を読んで確かめた | §2.6、§4.3 |
| M11 serial の log での判定 | ユーザーの決定 5（virt に画面）で解いた: ramfb、HAL の console、Phase ごとの判定。`hal.selftest` は捨てた | §5、§7.1 #6、§8 |
| M12 VF2 だけの道が試されない | (1)(2) を既定（ユーザー）: timer は常に SBI、Svpbmt は使わない; sifive_u の走り（判定の方法は #17）。(3) `-cpu rv64,sstc=off,svpbmt=off` も p003 に。property の名前は cpu.c で確かめた（`svpbmt` の既定が false である事も分かった） | §2.1、§5.2、§1.5、§7 #17 |
| M13 kernel の窓の式 | `VMA = VIRT_BASE + (paddr − LINK_PHYS_START)` に定義し直し、elf64.c の検査を arch ごとに。vmunix.ld の VMA/LMA | §2.5、§3.3 |
| m1 magic の byte 列 | 直した（`42 38 32 48`）。loader→HAL の magic は ZBL6 に変えたので見分けは `5a 42 4c 36` | §2.1 |
| m2 `pause` の comment | 直した（pred = W）。proposed/hal-riscv64.md の comment も | §9、proposed/hal-riscv64.md |
| m3 timer の ack と次の deadline | 「EOI は何もしない定数」、deadline を書いてから `kernel_timer_handler`（rpi4 の clock.c の順を確かめた）。claim の loop は 1 件ずつ完結 | §2.2 clock.c・plic.c |
| m4 IPI の受け方 | SSIP を落とす → swap、を 0 になるまで | §2.3 |
| m5 DBCN の物理 address、`seed`、rdtime | 書いた（`seed` は DT の zkr を見る、`scounteren.TM` は 0 のまま） | §1.2、§2.2 lib.c |
| m6 SUM | 要らない（uaccess.c を確かめた）。常に 0、入口で落とす | §2.3、§2.5 |
| m7 `sstatus.VS` | Off に固定（V の既定は未確認なので有無に依らず） | §2.6、§1.5 |
| m8 WC・NOCACHE・WRITETHRU | Svpbmt を使わないので UNSUPPORTED（hal.h の契約の中） | §2.1 |
| m9 A/D | arm64 の扱いに合わせた（SW_DIRTY = RSW bit 8、ACCESSED = PRESENT、clear_flags の順） | §2.5、§2.4 |
| m10 build の表 | Makefile・menuconfig の該当の行を読んで直した。BUILD は riscv64 だけ board で分ける。driver の既定は config.mk で n | §4.1 |
| m11 hook の一覧 | platform.h の 10 個を全部書いた | §2.7 |
| m12 arch の `#if` の棚卸し | grep の一覧を載せ、p007・p008 で file ごとに記録する | §2.7 |
| m13 `fpreg` の 264 byte | 予約を足して固定 | §2.6 |
| m14 `long double` 128 bit | arm64 と同じ扱い。arm64 の builtins の一覧に fp128 の物が無い事を確かめ、p007 で `nm` で確かめる事にした | §2.6 |
| m15 loader | `-z notext` を外す、`.bss` は VirtualSize、checker で確かめる。bootstrap table は作らない（satp = 0 で物理の入口へ） | §3.3 |
| m16 `riscv,isa` | 古い DT では読む | §2.2 fdt.c |
| m17 `_start` が最も低い LMA | vmunix.ld の assert | §2.5、§3.3 |
| m18 source の中の QEMU の話・試験の parameter | riscv64.h の comment から QEMU を外し、`hal.selftest` を捨てた | §0.2、proposed/hal-riscv64.md |
| m19 ws.md の Phase の表 | p003〜p010 を ws.md に写した（p009 の中身を変えた） | §6、ws.md |
| m20 LW | p003 28〜36、p008 10〜12、p007 14〜18 に上げた | §6 |
| m21 依存の明記 | #13 → p003、#9 → p008、#15・板 → p005、p009 → p003 の T1、p010a → p007 (b) | §6 |
| m22 §0.2 の確定 | ユーザーの決定 6 で確定 | §0.2、§7.1 |
| m23 BootROM の SPL の位置 | U-Boot の文書で確かめた: type GUID で探し partition の番号は自由。SPL は itb を partition 2 から読む | §1.6 |
| m24 secondary の入口 | SBI の保証だけに頼り、`fence.i`・`stvec`・`sscratch`・`sie` を入口で設定 | §2.2 locore.S、§2.3 |
