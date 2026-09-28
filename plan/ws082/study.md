<!-- awesome-plan project=zedbsd record=ws082p001 -->

# WS082 p001: Linux の `/dev/kvm` を zedBSD に移植する検討

Status: 検討の成果物（実装しない）。2026-09-28、subagent（kernel-and-driver-designer）。design-reviewer の敵対的レビューを 1 回受けて改訂（§12）。
親: [ws.md](ws.md)。読んだもの: AGENTS.md、guardrail.md、master.md の WS082 の行と決定の表、master-design-policy.md、
`include/hal/hal.h`、`include/hal/arch/amd64.h`、`src/hal/amd64/`（asm.c・int.c・irq.h・task.c・task.h・percpu.h・defs.h・space.h・
smp.c・ap-trampoline.S・bsp-pcat/lapic.c・timecounter.c・clock.c）、`src/kern/`（cdev.c・file.c・syscall.c・poll.c・sched.c・vmspace.c・vm-device.c・
devfs.c・uaccess.c・net/unix-socket.c）、`include/kern/`（cdev.h・file.h・fd-object.h・handle.h・vm-object.h・vmspace.h・thread.h・sched.h・clock.h・
pmem.h・irq.h・poll.h・waitq.h・usync.h・vm-device.h・filedesc.h・signal.h・net/packet-buf.h・net/socket.h）、`include/uapi/`（ioctl.h・socket.h・
un.h・mman.h・poll.h・input.h）、`userland/base/libc/posix.c`（ioctl）、`platform/amd64/vmunix.mk`。
外部: host の Linux の UAPI header（`/usr/include/linux/kvm.h`、`asm/kvm.h`）、QEMU v10.0.0（`accel/kvm/kvm-all.c`、`target/i386/kvm/kvm.c`、
`util/event_notifier-posix.c`、`meson.build`）、Firecracker main（`src/vmm/src/vstate/{kvm,vm,vcpu}.rs`、`arch/x86_64/{kvm,vm,vcpu}.rs`）、
cloud-hypervisor main（`hypervisor/src/kvm/mod.rs`、`kvm/x86_64/mod.rs`）。§9 にライセンスの扱い。

## 0. 要約

- **結論: 移植できる。** KVM の API のうち amd64 に関係する面は、ほぼ全部が「同じ ioctl・同じ意味」（A）で実装でき、eventfd に依る 2 つの ioctl
  （`KVM_IOEVENTFD`・`KVM_IRQFD`。irqfd の resample は `KVM_IRQFD` の flag）だけが「同じ ioctl・別の仕組み」（B）になる。実装できない・しないもの（C）は、
  zedBSD に無い仕組み（`guest_memfd`・`userfaultfd`・memfd）に依るもの、機密 VM（SEV/SEV-ES/SEV-SNP・TDX・`KVM_X86_SW_PROTECTED_VM`）、
  nested virtualization、Hyper-V・Xen の enlightenment、SMM、vPMU、VFIO の device fd、統計の fd である。
- **数（§2 の表を数えたもの。付録 A に内訳）**: Linux の header の ioctl 150 個のうち amd64 に関係する 106: **A 82 / B 2 / C 22**（他 arch の 44 は N/A、
  aarch64 は別の検討）。kvm_run の exit 24: A 19 / C 5、field 12: A 10 / C 2。capability 233 のうち amd64 に関係する 137: **A 90 / B 5 / C 42**（他 arch の約 95 は N/A）。
- **ただし A の前提として、zedBSD の ioctl の ABI に 3 つの穴がある（レビューで判明、§3.0）**: (1) ioctl は成功のとき **0 しか返せない**
  （`src/kern/file.c` の `file_ioctl`、`src/kern/syscall.c` の `KERN_SYS_ioctl`）ので、`KVM_GET_API_VERSION`（12）・`KVM_CREATE_VM`/`KVM_CREATE_VCPU`
  （fd を返す）・`KVM_CHECK_EXTENSION`・`KVM_GET_VCPU_MMAP_SIZE`・`KVM_GET/SET_MSRS`（処理した数）・`KVM_GET_TSC_KHZ` が Linux の形では実装できない。
  (2) libc の `ioctl()` は番号の size field が 0 の request（`_IO`）で **第 3 引数を kernel に渡さない**（`userland/base/libc/posix.c` の
  `ioctl_has_argument`）ので、`KVM_CHECK_EXTENSION(cap)`・`KVM_CREATE_VCPU(id)`・`KVM_SET_TSS_ADDR(addr)` 等の値渡しの引数が常に 0 になる。
  (3) `KERN_SYS_ioctl` は SA_RESTART の handler の後に **自動で restart される**（`syscall_restartable`）ので、`KVM_RUN` の EINTR（kick）が握り潰される。
  3 つとも HAL ではなく **kernel と libc の ABI の小さな変更**で解ける（§3.0 に案）。実装の最初の Phase にする。
- **zedBSD 側の土台で足りないもの**: (1) VMX（VT-x）の有効化・VMCS・VM entry/exit の trampoline・EPT の無効化・host の MSR という
  **HAL の新しい責務**（§3.10 に差分の一覧。差分ごとの承認が要る。CR0.NE の設定と reset/panic の VMXOFF を含む）、
  (2) guest memory の pin（`vmspace_wire_range` と `vmspace_pin_user_pages` は既にあるが呼び出し元が無く未検証）と EPT の table の構築、
  **CPU 間の EPT の無効化と memslot の更新の並行制御**、(3) MMIO の exit のための **x86 命令の decoder/emulator**（Linux KVM の emulator は GPL なので
  独自に書く。最大の実装量）、(4) in-kernel の LAPIC・IOAPIC・PIC・PIT の model（QEMU の既定と firecracker・cloud-hypervisor が要る）、
  (5) unix socket の datagram を kernel が送る・受け取る hook（§4。**packet の pool は system 全体で 32 個**なので登録ごとに 1 個を予約する設計）、
  (6) 1 ms より細かい timer（guest の timer の精度。当面は VMX preemption timer と tick で代用）、(7) `/dev/kvm` の権限（devfs の既定は 0666）と
  wire する memory の上限（RLIMIT_MEMLOCK は無い）。
  **XSAVE**: 今の HAL は FXSAVE だけを使い CR4.OSXSAVE を立てない（`src/hal/amd64/asm.c`・`task.c`）。guest に AVX 等を見せるには HAL の FP 文脈を
  XSAVE に変える責務の変更（signal frame・ucontext の大きさに波及）が要る。最初の到達点では guest の CPUID から XSAVE/AVX を隠す。
- **最初の到達点の案（§5.3）**: M1 = 自作の最小 VMM（C、数百行）で real mode の code が `OUT` で文字を出し `HLT` で止まる。M2 = patch した QEMU
  `-accel kvm -machine kernel-irqchip=off`（virtio は `ioeventfd=off`）で Linux の guest が serial console の shell まで起動する。
  M3 = QEMU の既定（in-kernel irqchip、SMP、virtio の ioeventfd/irqfd を unix socket で）。firecracker・cloud-hypervisor は Rust の toolchain が
  zedBSD に無いので、ioctl・capability の集合は満たせても実行は別の WS（Rust の target の追加、toolchain の変更は main の許可）。
- **大きなリスク（§7）**: 命令 emulator の量と正しさ、HAL の責務の追加の承認とその大きさ、vCPU thread の CPU 間の移動（work stealing は M1 から起きる）に
  伴う VMCS と EPT の扱い、nested VMX（QEMU の上の zedBSD の中の guest）でしか試験できない期間が長いこと、poll の「全員起こす」設計
  （`src/kern/poll.c`。system 全体で 1 本）、amd64 の kernel image の 16 MiB の上限（KVM は config option にする）、QEMU の移植（host OS の追加、
  glib・pixman・meson のクロスビルド）が別の WS として要ること、誰でも開ける device が guest から host を攻撃する面（emulator・irqchip）になること。

## 1. 前提と範囲

- **対象**: amd64（Intel VT-x = VMX を先に。AMD-V = SVM は後の Phase）。aarch64 は主対象の platform だが、この検討の中心ではない（§8 に要点だけ）。
  i386 は対象外。
- **API の基準**: host の Linux の header（`KVM_API_VERSION 12`、ioctl の一意な名前 150 個、`KVM_CAP_*` 233 個。他 arch 専用の定義を含む）。QEMU 10.0 の
  `kvm_required_capabilites` と `kvm_arch_required_capabilities`（x86）、Firecracker の `DEFAULT_CAPABILITIES`（x86_64、14 個）、
  cloud-hypervisor の `check_required_kvm_extensions`（x86_64、18 個）を必須の集合の根拠にした（§5）。
- **ioctl の番号**: zedBSD の `_IOR/_IOW/_IOWR` は `include/uapi/ioctl.h` の encoding（方向の bit が Linux と逆、size は 13 bit）なので、
  **同じ名前の ioctl でも数値は Linux と違う**。`_IO(KVMIO, n)`（`KVM_RUN` = 0xAE80 等）だけ同じ。目標は source の互換（zedBSD の
  `<uapi/kvm.h>` で build した VMM が動く）であり、Linux の binary の互換ではない。size 13 bit（最大 8191）に対し最大の引数は `struct kvm_xsave`
  （4096）なので収まる。
- **ユーザーの制約**: eventfd 等の非 POSIX の fd は作らない。MMIO・PIO・IRQ の非同期の通知は unix socket の message にする（§4）。通常の
  `KVM_RUN` の exit（`kvm_run` の `KVM_EXIT_MMIO`・`KVM_EXIT_IO`）は同期の往復であり fd を使わないので、そのまま A である。
- **用語**: GPA = guest physical address、HPA = host physical、GVA = guest virtual。memslot = `KVM_SET_USER_MEMORY_REGION` の 1 区画。
  irqchip = in-kernel の PIC/IOAPIC/LAPIC。split irqchip = LAPIC だけ kernel、PIC/IOAPIC は userland。

## 2. KVM の API の分類

記号: **A** 直接の移植（同じ ioctl・同じ意味。§3.0 の ABI の修正が前提）、**B** 同じ ioctl・別の仕組み（unix socket）、**C** 実装できない・しない、
**N/A** 他 arch 専用（s390・ppc・arm64・riscv・mips・loongarch。arm64 のものは aarch64 の検討で扱う）。「fd」列は ioctl を受ける fd（S = `/dev/kvm`、
V = VM、C = vCPU、D = device）。「時期」は §6 の Phase の記号（M1〜M3 は到達点、L = その後、— = 実装しない）。「戻り値」は Linux で正の値を返す ioctl（§3.0）。

### 2.1 VM・vCPU の lifecycle、mmap、capability の問い合わせ

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_API_VERSION` | S | A | M1 | 12 を返す（戻り値） |
| `KVM_CREATE_VM` | S | A | M1 | type は `KVM_X86_DEFAULT_VM`(0) だけ（値渡しの引数、戻り値 = fd）。`KVM_X86_SW_PROTECTED_VM`・SEV・TDX の type は EINVAL（C、§2.11）。VM は作った process の `vmspace` に縛る（fork した子からの ioctl は拒む。Linux の mm の縛りと同じ） |
| `KVM_CHECK_EXTENSION` | S, V | A | M1 | 値渡しの引数、戻り値。VM の fd でも受ける（`KVM_CAP_CHECK_EXTENSION_VM`）。値の表は §2.13 |
| `KVM_GET_VCPU_MMAP_SIZE` | S | A | M1 | 戻り値。3 page（`kvm_run`、PIO data の page（`KVM_PIO_PAGE_OFFSET` 1）、coalesced MMIO ring（offset 2））。dirty ring を有効にしたら 64 page 目から足す |
| `KVM_CREATE_VCPU` | V | A | M1 | 値渡しの引数（vcpu id）、戻り値 = fd。新しい pseudo file（`file_create_pseudo` + `filedesc_install`。pipe.c と同じ道）。vcpu id は `KVM_CAP_MAX_VCPU_ID` まで |
| vCPU fd の `mmap` | C | A | M1 | `file_ops.mmap` → `vm_device_create()`（kernel RAM の `kern_pmem_alloc` の区画、`max_prot` RW、`VM_BACKING_DEVICE`）。`src/drivers/audio/audio.c` の `audio_dsp_mmap` と同じ形 |
| `KVM_RUN` | C | A | M1 | §3.3。EINTR を restart しない（§3.0） |
| `KVM_SET_SIGNAL_MASK` | C | A | M2 | `KVM_RUN` の間だけ `thread->signal_mask` を差し替える |
| `KVM_ENABLE_CAP` | V, C | A | M2 | cap ごと（§2.13 の「enable」列） |
| `KVM_GET_STATS_FD` | V, C | C | — | 統計の binary の fd。実装しない。代替: sysctl か `/dev/kvm` の ioctl で数を返す（後で決める） |
| `KVM_SET_BOOT_CPU_ID` | V | A | M3 | 値渡しの引数 |
| `KVM_GET_DEVICE_ATTR`・`KVM_HAS_DEVICE_ATTR` on S（`KVM_CAP_SYS_ATTRIBUTES`、`KVM_X86_XCOMP_GUEST_SUPP`） | S | C | — | XSAVE の拡張 component の問い合わせ。HAL が XSAVE を使うまで cap 0 |
| `KVM_SET/GET/HAS_DEVICE_ATTR` on V（`KVM_CAP_VM_ATTRIBUTES`） | V | C | — | x86 では SEV の VMSA feature 等。cap 0 |
| `KVM_SET/GET/HAS_DEVICE_ATTR` on C（`KVM_CAP_VCPU_ATTRIBUTES`、`KVM_VCPU_TSC_OFFSET`） | C | A | L | TSC offset の読み書き。小さい（付録 A では名前として C に数えた） |
| s390・ppc・riscv・loongarch・mips の ioctl と `KVM_GET_REG_LIST`（44 個） | — | N/A | — | zedBSD の対象外 |
| `KVM_ARM_VCPU_INIT`・`KVM_ARM_PREFERRED_TARGET`・`KVM_ARM_VCPU_FINALIZE`・`KVM_ARM_SET_DEVICE_ADDR`・`KVM_ARM_MTE_COPY_TAGS`・`KVM_ARM_SET_COUNTER_OFFSET`・`KVM_ARM_GET_REG_WRITABLE_MASKS` | — | N/A | — | aarch64 の検討（§8）で扱う |

### 2.2 memory slot

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_SET_USER_MEMORY_REGION` | V | A | M1 | §3.4。slot の追加・削除（size 0）・移動・flags（`KVM_MEM_LOG_DIRTY_PAGES`、`KVM_MEM_READONLY`）。userspace_addr は呼び出し process の user の範囲で、**anonymous（private または shared）の region だけ**受ける（file backed・device backed は最初は EINVAL、§3.4）。`KVM_CAP_NR_MEMSLOTS` は 32 から（QEMU は 32 未満なら 32 と仮定する） |
| `KVM_SET_USER_MEMORY_REGION2` | V | A（`guest_memfd` の field は C） | M2 | `guest_memfd`/`guest_memfd_offset` が 0 で `KVM_MEM_GUEST_MEMFD` 無しのときだけ受ける。`KVM_CAP_USER_MEMORY2` は 1 で返し `KVM_CAP_GUEST_MEMFD` は 0 |
| `KVM_SET_NR_MMU_PAGES`・`KVM_GET_NR_MMU_PAGES` | V | A | M2 | 値渡し・戻り値。shadow paging の cache の数。EPT では意味が無いので保存するだけ |
| `KVM_SET_TSS_ADDR`・`KVM_SET_IDENTITY_MAP_ADDR` | V | A | M2 | 前者は値渡し。unrestricted guest の無い CPU で real mode を vm86 で真似るための領域。**unrestricted guest を必須の hardware にする**（§3.1）ので値を保存するだけ。QEMU x86 は両方を必須の cap にしている |
| `KVM_REGISTER_COALESCED_MMIO`・`KVM_UNREGISTER_COALESCED_MMIO` | V | A | M3 | ring は vCPU の mmap の page 2（`struct kvm_coalesced_mmio_ring`、`KVM_COALESCED_MMIO_MAX` 個）。PIO も同じ ring（`KVM_CAP_COALESCED_PIO`） |
| `KVM_PRE_FAULT_MEMORY` | C | A | L | EPT を先に埋める。`guest_memfd` 無しでも意味がある |
| `KVM_SET_MEMORY_ATTRIBUTES` | V | C | — | private/shared の属性は `guest_memfd` の機構。§2.11 |
| `KVM_CREATE_GUEST_MEMFD` | V | C | — | §2.11 |
| `KVM_TRANSLATE` | C | A | M2 | guest の page table を software で歩く（§3.8 の walker を使う） |

### 2.3 register（GPR・segment・FPU・XSAVE・debug register・LAPIC の状態・event・MP state）

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_REGS`・`KVM_SET_REGS` | C | A | M1 | GPR・RIP・RFLAGS。VMCS の field と HAL の run の引数（§3.10 `hal_virt_gpregs`） |
| `KVM_GET_SREGS`・`KVM_SET_SREGS`、`KVM_GET_SREGS2`・`KVM_SET_SREGS2` | C | A | M1 / M2 | segment・CR0/2/3/4/8・EFER・APIC base・interrupt_bitmap。SREGS2 は PDPTR と flags を足す（`KVM_CAP_SREGS2`）。CR の guest/host mask と read shadow の扱いは §3.2 |
| `KVM_GET_FPU`・`KVM_SET_FPU` | C | A | M2 | FXSAVE の像の変換 |
| `KVM_GET_XSAVE`・`KVM_SET_XSAVE`・`KVM_GET_XSAVE2` | C | A（制限つき） | M2 | 4096 byte の XSAVE の像。**HAL が XSAVE を使うまで legacy 領域（x87/SSE）だけ**が有効で、XSTATE_BV は 0x3 まで。`KVM_CAP_XSAVE` 1、`KVM_CAP_XSAVE2` は 4096 を返す。QEMU x86 は `KVM_CAP_XSAVE` を必須にしている |
| `KVM_GET_XCRS`・`KVM_SET_XCRS` | C | A（制限つき） | M2 | XCR0 = 0x3 だけ受ける（AVX を隠す間） |
| `KVM_GET_DEBUGREGS`・`KVM_SET_DEBUGREGS` | C | A | M2 | DR0-3・DR6・DR7。guest の DR は VMCS（DR7）と entry/exit の手動の保存 |
| `KVM_GET_LAPIC`・`KVM_SET_LAPIC` | C | A | M3 | in-kernel LAPIC の 1024 byte の register の像 |
| `KVM_GET_VCPU_EVENTS`・`KVM_SET_VCPU_EVENTS` | C | A | M2 | pending の exception/interrupt/NMI・shadow・SMM の flag（SMM は常に 0）。`KVM_CAP_VCPU_EVENTS` は QEMU の必須 |
| `KVM_GET_MP_STATE`・`KVM_SET_MP_STATE` | C | A | M2 | RUNNABLE/UNINITIALIZED/INIT_RECEIVED/HALTED/SIPI_RECEIVED。INIT/SIPI は in-kernel LAPIC（M3）で意味を持つ |
| `KVM_GET_ONE_REG`・`KVM_SET_ONE_REG` | C | A | L | x86 では最近の MSR の register 空間だけ。cloud-hypervisor は arm64 で多用、x86 では使わない |
| `KVM_GET_NESTED_STATE`・`KVM_SET_NESTED_STATE` | C | C | — | §2.10 |
| kvm_run の sync regs（`kvm_valid_regs`・`kvm_dirty_regs`・`s.regs`、`KVM_CAP_SYNC_REGS`） | C | A | M2 | REGS/SREGS/EVENTS を mmap の page で同期。QEMU は使えれば使う |

### 2.4 MSR

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_MSR_INDEX_LIST` | S | A | M2 | 保存・復元できる MSR の表（kernel の emulation の表と同じ） |
| `KVM_GET_MSR_FEATURE_INDEX_LIST`、`KVM_GET_MSRS` on S | S | A | M2 | feature MSR（`IA32_ARCH_CAPABILITIES`・`MSR_IA32_UCODE_REV` 等。`IA32_VMX_*` は nested を隠すので返さない）。`KVM_CAP_GET_MSR_FEATURES` 1 |
| `KVM_GET_MSRS`・`KVM_SET_MSRS` | C | A | M1（最小）/ M2 | 戻り値 = 処理した数（QEMU は assert で確かめる）。emulation する MSR の表: EFER・STAR/LSTAR/CSTAR/FMASK/KERNEL_GS_BASE・FS/GS base・SYSENTER・TSC・TSC_ADJUST・APIC_BASE・PAT・MTRR（固定値）・MISC_ENABLE・MCG_*・x2APIC の range・TSC_DEADLINE・KVM の pv MSR（M3 の kvmclock）。未知の MSR は `KVM_CAP_X86_USER_SPACE_MSR` が有効なら `KVM_EXIT_X86_RDMSR/WRMSR`、無効なら #GP |
| `KVM_X86_SET_MSR_FILTER` | V | A | L | 範囲の bitmap で userland へ exit。`KVM_CAP_X86_MSR_FILTER` |
| `KVM_CAP_X86_USER_SPACE_MSR`（enable） | V | A | L | 上と組 |
| `KVM_SET_TSC_KHZ`・`KVM_GET_TSC_KHZ` | V, C | A | M2 | 値渡し・戻り値。host の TSC の周波数は HAL の virt の probe（H1）から取る（`hal_rtc_read_counter()` は契約上 TSC そのものではない）。M2 は host と同じ値だけ受ける（`KVM_CAP_TSC_CONTROL` 0）。TSC scaling で違う値を受けるのは L |

### 2.5 CPUID

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_SUPPORTED_CPUID` | S | A | M1 | host の CPUID に mask を掛けた表。**隠すもの**: VMX/SVM（nested 無し）、XSAVE/OSXSAVE/AVX/AVX2/AVX-512/MPX/AMX（HAL が XSAVE を使うまで）、PMU（leaf 0xA を 0）、SGX・TME・PT・MONITOR/MWAIT（`KVM_CAP_X86_DISABLE_EXITS` まで）、hypervisor の leaf は KVM の pv leaf（0x40000000〜、M3）に置き換える。x2APIC は M3 から見せる。`KVM_CAP_EXT_CPUID` は 3 つの VMM の全員が必須 |
| `KVM_GET_EMULATED_CPUID` | S | A | M2 | emulator で真似できる feature（MOVBE 等）。最初は空 |
| `KVM_SET_CPUID`・`KVM_SET_CPUID2`・`KVM_GET_CPUID2` | C | A | M1 | guest の CPUID の表（CPUID exit で返す）。`KVM_MAX_CPUID_ENTRIES` は Linux と同じ 256 |
| `KVM_GET_SUPPORTED_HV_CPUID` | S, C | C | — | Hyper-V の enlightenment（§2.12） |

### 2.6 割り込み（irqchip・GSI routing・MSI・irqfd・NMI・SMI）

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_INTERRUPT` | C | A | M2 | userspace irqchip の外部割り込みの注入（`kvm_run.request_interrupt_window`・`ready_for_interrupt_injection`・`KVM_EXIT_IRQ_WINDOW_OPEN` と組。VMX の interrupt-window exiting） |
| `KVM_NMI` | C | A | M2 | NMI の注入（`KVM_CAP_USER_NMI`。cloud-hypervisor の必須） |
| `KVM_SMI` | C | C | — | SMM の emulation は行わない（`KVM_CAP_X86_SMM` 0）。代替: QEMU `-machine smm=off`（OVMF の Secure Boot の variable の保護は使えない） |
| `KVM_CREATE_IRQCHIP` | V | A | M3 | in-kernel の PIC（master/slave）・IOAPIC・LAPIC（§3.6）。`KVM_CAP_IRQCHIP`。firecracker の必須 |
| `KVM_CAP_SPLIT_IRQCHIP`（enable） | V | A | M3 | LAPIC だけ kernel。IOAPIC の EOI は `KVM_EXIT_IOAPIC_EOI` で userland へ。cloud-hypervisor の必須、QEMU の `kernel-irqchip=split` |
| `KVM_IRQ_LINE`・`KVM_IRQ_LINE_STATUS` | V | A | M3 | GSI の level/edge。`KVM_CAP_IRQ_INJECT_STATUS` |
| `KVM_GET_IRQCHIP`・`KVM_SET_IRQCHIP` | V | A | M3 | PIC/IOAPIC の状態の保存・復元 |
| `KVM_SET_GSI_ROUTING` | V | A（`KVM_IRQ_ROUTING_HV_SINT`・`KVM_IRQ_ROUTING_XEN_EVTCHN` の entry は C） | M2（表だけ）/ M3 | `KVM_CAP_IRQ_ROUTING` は QEMU x86 の必須なので M2 から表を受け、注入は M3。entry の種類: IRQCHIP・MSI（`KVM_MSI_VALID_DEVID` は使わない、`KVM_CAP_MSI_DEVID` 0） |
| `KVM_SIGNAL_MSI` | V | A | M3 | MSI の address/data を LAPIC へ。`KVM_CAP_SIGNAL_MSI` は QEMU x86 の必須の cap だが、ioctl 自体は in-kernel irqchip のとき（`kvm-apic` device の MSI の region → `kvm_irqchip_send_msi`）だけ呼ばれ、userspace irqchip の M2 では userland の APIC が受ける。M2 では cap を 1 で返し ioctl は ENOTSUP |
| `KVM_IRQFD` | V | **B** | M3 | §4。fd は AF_UNIX/SOCK_DGRAM の接続済みの socket。`KVM_IRQFD_FLAG_RESAMPLE` の resamplefd も socket（kernel → VMM の向き） |
| `KVM_CREATE_PIT`・`KVM_CREATE_PIT2`・`KVM_GET_PIT`・`KVM_SET_PIT`・`KVM_GET_PIT2`・`KVM_SET_PIT2`・`KVM_REINJECT_CONTROL` | V | A | M3 | i8254 の model（channel 0 → IRQ0、speaker）。firecracker の必須（`KVM_CAP_PIT2`・`PIT_STATE2`）。QEMU は in-kernel irqchip のとき `kvm-pit` を使う。`KVM_REINJECT_CONTROL` は Linux でも `_IO` なのに pointer を渡す例外（§3.0 の libc の修正で扱う） |
| `KVM_TPR_ACCESS_REPORTING`・`KVM_SET_VAPIC_ADDR` | C | A | L | 古い vapic の最適化。cap（`KVM_CAP_VAPIC`）0 の間 QEMU は使わない |
| `KVM_CAP_X2APIC_API`（enable） | V | A | M3 | 32 bit の APIC ID の routing |
| `KVM_HYPERV_EVENTFD`、`KVM_XEN_HVM_*`・`KVM_XEN_VCPU_*` | V, C | C | — | §2.12 |

### 2.7 ioeventfd

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_IOEVENTFD` | V | **B** | M3（cap は M2 から 1、§10.8） | §4。PIO/MMIO の address・length（0 = any、`KVM_CAP_IOEVENTFD_ANY_LENGTH`）・datamatch・deassign。QEMU は `KVM_CAP_IOEVENTFD` と `ANY_LENGTH` を必須にしている（無いと起動しない）ので、**M2 では cap を 1 で返しつつ virtio の `ioeventfd=off` で動かす**（QEMU は ioctl の失敗で abort するため、M2 の QEMU の起動の手順に含める） |
| `KVM_CAP_IOEVENTFD_NO_LENGTH` | — | B | M3 | length 0 の登録 |

### 2.8 dirty logging

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_DIRTY_LOG` | V | A | L1 | memslot の bitmap。EPT の write-protect（EPT violation で bit を立てて W を戻す）か EPT の A/D bit。QEMU は `-vga std` の VRAM の更新の検出にも使う（`-display none` なら不要） |
| `KVM_CLEAR_DIRTY_LOG`、`KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2`（enable） | V | A | L1 | 部分の clear |
| `KVM_CAP_DIRTY_LOG_RING`・`_ACQ_REL`・`_WITH_BITMAP`（enable）、`KVM_RESET_DIRTY_RINGS`、`KVM_EXIT_DIRTY_RING_FULL` | V, C | A | L2 | vCPU の mmap の 64 page 目からの ring。後 |

### 2.9 guest debug

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_SET_GUEST_DEBUG` | C | A | L1 | single step（RFLAGS.TF + #DB exiting）・hardware breakpoint（DR7 を kernel が持つ）・software breakpoint（#BP exiting）・`KVM_GUESTDBG_INJECT_DB/BP`・`BLOCKIRQ`。`KVM_CAP_SET_GUEST_DEBUG`・`SET_GUEST_DEBUG2`。`KVM_EXIT_DEBUG`。QEMU の必須 `KVM_CAP_X86_ROBUST_SINGLESTEP` は「single step が MOV SS の shadow で壊れない」の意味で、cap を 1 と返し L1 で実装する（gdbstub を使うまで呼ばれない。§10.8） |

### 2.10 clock・TSC・nested

| ioctl | fd | 分類 | 時期 | 根拠・注意 |
| --- | --- | --- | --- | --- |
| `KVM_GET_CLOCK`・`KVM_SET_CLOCK` | V | A | M2（stub）/ M3 | kvmclock の基準（`KVM_CLOCK_REALTIME`・`HOST_TSC` の flag）。`KVM_CAP_ADJUST_CLOCK` は QEMU・firecracker・cloud-hypervisor の必須。M2 は host の単調時刻を ns で返す stub、M3 で pvclock（§3.7） |
| `KVM_KVMCLOCK_CTRL` | C | A | M3 | 「vCPU を止めた」の印（guest の softlockup の抑止） |
| `KVM_CAP_TSC_DEADLINE_TIMER` | — | A | M3 | in-kernel LAPIC の TSC-deadline mode。cloud-hypervisor の必須。精度は §3.7 |
| `KVM_GET_NESTED_STATE`・`KVM_SET_NESTED_STATE`、`KVM_CAP_NESTED_STATE`、`KVM_CAP_HYPERV_ENLIGHTENED_VMCS`、kvm_run の `KVM_RUN_X86_GUEST_MODE` | C | **C** | — | nested virtualization（guest の中の hypervisor）は実装しない。VMX の emulation（VMCS の shadow・EPT の合成）は Linux でも数万行。CPUID から VMX を隠し `IA32_VMX_*` MSR を返さない。代替: 無し |

### 2.11 SEV/TDX・guest_memfd・userfaultfd

| 項目 | 分類 | 根拠・代替 |
| --- | --- | --- |
| `KVM_MEMORY_ENCRYPT_OP`・`KVM_MEMORY_ENCRYPT_REG_REGION`・`UNREG_REGION`、VM type SEV/SEV-ES/SEV-SNP、`KVM_EXIT_AP_RESET_HOLD`、`KVM_CAP_VM_COPY/MOVE_ENC_CONTEXT_FROM`、`KVM_X86_SEV_VMSA_FEATURES` | C | AMD の機密 VM。firmware（PSP）と暗号化 memory の管理が要り、対象機（Intel）に無い。代替: 無し |
| TDX（`KVM_X86_TDX_VM`、`KVM_TDX_*`） | C | Intel TDX module・SEAM の loader・firmware の対応が要る。対象機に無い。代替: 無し |
| `KVM_X86_SW_PROTECTED_VM`、`KVM_CREATE_GUEST_MEMFD`、`KVM_SET_MEMORY_ATTRIBUTES`、`KVM_CAP_GUEST_MEMFD`・`MEMORY_ATTRIBUTES`・`VM_TYPES`、`KVM_EXIT_MEMORY_FAULT`、`KVM_CAP_MEMORY_FAULT_INFO` | C | `guest_memfd` は memfd 風の fd（非 POSIX）で、private な guest memory を host の page table に写さない機構。zedBSD に memfd は無く、機密 VM を扱わないので要らない。代替: 通常の memslot |
| `userfaultfd`（KVM の ioctl ではなく VMM の postcopy migration の依存） | C | zedBSD に無い。代替: precopy の migration だけ（dirty log は A）。将来 zedBSD 独自の「page fault の通知」を作るなら別の WS |
| memfd（QEMU の `memory-backend-memfd`、cloud-hypervisor の memfd_create） | C（VMM 側の patch） | `shm_open` + `shm_unlink` した fd で代替（audiod の決定と同じ形、master の 2026-09-24 の表）。ただし §3.4 の理由で shared の memslot は M3 の後 |

### 2.12 Hyper-V・Xen・PMU・その他

| 項目 | 分類 | 根拠・代替 |
| --- | --- | --- |
| Hyper-V の enlightenment（`KVM_CAP_HYPERV*` 14 個、`KVM_GET_SUPPORTED_HV_CPUID`、`KVM_HYPERV_EVENTFD`、`KVM_EXIT_HYPERV`、routing の HV_SINT） | C | Windows の guest の性能のための機構。SynIC・hypercall・TLB flush・IPI・stimer の model が要り、eventfd（SynIC の connection）も含む。最初は行わない。代替: 無くても Windows は起動する（遅い）。将来の WS |
| Xen の HVM（`KVM_XEN_*`、`KVM_CAP_XEN_HVM`、`KVM_EXIT_XEN`、routing の XEN_EVTCHN） | C | Xen の guest ABI の emulation。要らない |
| vPMU（`KVM_SET_PMU_EVENT_FILTER`、`KVM_CAP_PMU_EVENT_FILTER`・`PMU_CAPABILITY`・`PMU_EVENT_MASKED_EVENTS`） | C | host に perf の subsystem が無く、PMU の register の仮想化が要る。CPUID leaf 0xA を 0 にして隠す。代替: 無し |
| `KVM_CREATE_DEVICE`・device fd の `KVM_SET/GET/HAS_DEVICE_ATTR`（`KVM_CAP_DEVICE_CTRL`） | C | x86 の device type は `KVM_DEV_TYPE_VFIO` だけ。zedBSD に VFIO（IOMMU の userland への委譲）は無い（`src/` に IOMMU/VT-d の code は無い）。aarch64 では VGIC のために A になる（§8） |
| `KVM_CAP_IOMMU`・legacy の device assignment | C | 同上 |
| `KVM_CAP_X86_SMM`・`KVM_SMI`・`KVM_RUN_X86_SMM` | C | §2.6 |
| `KVM_CAP_SGX_ATTRIBUTE` | C | SGX の enclave。要らない |
| `KVM_CAP_X86_BUS_LOCK_EXIT`・`KVM_EXIT_X86_BUS_LOCK`、`KVM_CAP_X86_NOTIFY_VMEXIT`・`KVM_EXIT_NOTIFY` | A（hardware があれば） | bus-lock detection・notify VM exit は Sapphire Rapids/Alder Lake 以降。Skylake-SP（検証 host）には無い。cap は hardware で決める |
| `KVM_CAP_EXIT_HYPERCALL`・`KVM_EXIT_HYPERCALL`（`KVM_HC_MAP_GPA_RANGE`） | A | L。hypercall の転送自体は小さい |
| `KVM_CAP_X86_DISABLE_EXITS`（HLT/PAUSE/MWAIT/CSTATE の exit を止める） | A | L |
| `KVM_CAP_X86_TRIPLE_FAULT_EVENT`・`KVM_EXIT_SYSTEM_EVENT`（`KVM_SYSTEM_EVENT_SHUTDOWN`）、`KVM_CAP_EXCEPTION_PAYLOAD` | A | M2 |
| `KVM_X86_SETUP_MCE`・`KVM_X86_GET_MCE_CAP_SUPPORTED`・`KVM_X86_SET_MCE` | A | M2。bank の数を返し、`KVM_SET_MCE` は #MC の注入。QEMU の必須（`KVM_CAP_MCE`）。最小の実装 |
| `KVM_CAP_HALT_POLL`・`KVM_CAP_STEAL_TIME`・`KVM_CAP_ASYNC_PF*`・`KVM_CAP_MSR_PLATFORM_INFO`・`KVM_CAP_ENFORCE_PV_FEATURE_CPUID` | A | L（kvmclock と同じ pv の枠） |
| `KVM_CAP_X86_APIC_BUS_CYCLES_NS`・`KVM_CAP_DISABLE_QUIRKS`・`DISABLE_QUIRKS2` | A | L |
| `KVM_CAP_MULTI_ADDRESS_SPACE` | C | SMM の address space のため。1 を返す（QEMU は ≤1 なら 1 と扱う）。SMM が無いので C に数える |
| `KVM_CAP_SYNC_MMU` | C（最初）→ A（後） | host の page の入れ替え（swap・COW）を EPT に追従させる「MMU notifier」に相当する仕組みが zedBSD に無い。最初は memslot を wire して 0 を返す（QEMU は postcopy/balloon 以外では要らない） |
| `KVM_CAP_READONLY_MEM` | A | M2（EPT の W を落とすだけ） |
| `KVM_CAP_MAX_VCPUS`・`NR_VCPUS`・`MAX_VCPU_ID` | A | host の CPU 数（`hal_cpu_count()`、`AMD64_SMP_MAX_CPUS` 64）と同じ上限から始める |

### 2.13 capability の表（`KVM_CHECK_EXTENSION` で返す値）

Linux の 233 個のうち他 arch 専用（`KVM_CAP_S390_*` 29、ppc の `PPC_*`・`SPAPR_*`・`IRQ_MPIC`・`IRQ_XICS`・`SW_TLB` の 41、`ARM_*`・`COUNTER_OFFSET` の 20、
`MIPS_*` 5）の約 95 個は N/A。残り 137 個の amd64 に関係するものを A（値 ≥1 で返す時期つき）・B・C（0 を返す。例外は注記）に分ける。
「enable」は `KVM_ENABLE_CAP` で有効にする cap。**実装より先に 1 を返す cap**（VMM の必須の検査を通すため）は §10.8 でユーザーの判断を仰ぐ。

| 分類 | capability（時期） |
| --- | --- |
| A（M1、17） | `HLT`・`USER_MEMORY`・`SET_TSS_ADDR`・`EXT_CPUID`・`NR_VCPUS`・`NR_MEMSLOTS`・`MP_STATE`・`DESTROY_MEMORY_REGION_WORKS`・`JOIN_MEMORY_REGIONS_WORKS`・`INTERNAL_ERROR_DATA`・`IMMEDIATE_EXIT`・`MAX_VCPUS`・`MAX_VCPU_ID`・`CHECK_EXTENSION_VM`・`ENABLE_CAP`・`ENABLE_CAP_VM`・`USER_NMI` |
| A（M2、25） | `SET_GUEST_DEBUG`（実装は L1、§10.8）・`X86_ROBUST_SINGLESTEP`（同）・`SET_IDENTITY_MAP_ADDR`・`IRQ_ROUTING`（表だけ、§10.8）・`SIGNAL_MSI`（§10.8）・`DEBUGREGS`・`XSAVE`・`XSAVE2`・`XCRS`・`VCPU_EVENTS`・`MCE`・`ADJUST_CLOCK`・`SYNC_REGS`・`SREGS2`・`GET_TSC_KHZ`・`READONLY_MEM`・`USER_MEMORY2`・`EXCEPTION_PAYLOAD`（enable）・`X86_TRIPLE_FAULT_EVENT`（enable）・`SYSTEM_EVENT_DATA`・`GET_MSR_FEATURES`・`SMALLER_MAXPHYADDR`・`LAST_CPU`・`INTR_SHADOW`・`MMU_SHADOW_CACHE_CONTROL` |
| A（M3、17） | `IRQCHIP`・`PIT`・`PIT2`・`PIT_STATE2`・`REINJECT_CONTROL`・`IRQ_INJECT_STATUS`・`SPLIT_IRQCHIP`（enable）・`X2APIC_API`（enable）・`TSC_DEADLINE_TIMER`・`COALESCED_MMIO`・`COALESCED_PIO`・`SET_BOOT_CPU_ID`・`CLOCKSOURCE`・`KVMCLOCK_CTRL`・`IOAPIC_POLARITY_IGNORED`・`HALT_POLL`（enable）・`MSR_PLATFORM_INFO`（enable） |
| A（L、31） | `TSC_CONTROL`・`VCPU_ATTRIBUTES`・`MANUAL_DIRTY_LOG_PROTECT`・`MANUAL_DIRTY_LOG_PROTECT2`（enable）・`DIRTY_LOG_RING`・`DIRTY_LOG_RING_ACQ_REL`・`DIRTY_LOG_RING_WITH_BITMAP`（enable）・`X86_USER_SPACE_MSR`（enable）・`X86_MSR_FILTER`・`X86_DISABLE_EXITS`（enable）・`EXIT_HYPERCALL`（enable）・`EXIT_ON_EMULATION_FAILURE`（enable）・`VAPIC`・`ONE_REG`・`STEAL_TIME`・`ASYNC_PF`・`ASYNC_PF_INT`・`ENFORCE_PV_FEATURE_CPUID`（enable）・`X86_BUS_LOCK_EXIT`（hardware）・`X86_NOTIFY_VMEXIT`（hardware）・`X86_APIC_BUS_CYCLES_NS`（enable）・`DISABLE_QUIRKS`・`DISABLE_QUIRKS2`・`PRE_FAULT_MEMORY`・`VM_TSC_CONTROL`・`VM_GPA_BITS`・`EXT_EMUL_CPUID`・`GUEST_DEBUG_HW_BPS`・`GUEST_DEBUG_HW_WPS`・`SET_GUEST_DEBUG2`・`NOP_IO_DELAY` |
| B（5） | `IOEVENTFD`（M2 から 1、実装 M3、§10.8）・`IOEVENTFD_ANY_LENGTH`（同）・`IOEVENTFD_NO_LENGTH`（M3）・`IRQFD`（M3）・`IRQFD_RESAMPLE`（M3） |
| C（42、0 を返す。`MULTI_ADDRESS_SPACE` だけ 1） | `IOMMU`・`ASSIGN_DEV_IRQ`・`PCI_2_3`・`PCI_SEGMENT`・`DEVICE_CTRL`・`VM_ATTRIBUTES`・`SYS_ATTRIBUTES`・`X86_SMM`・`MULTI_ADDRESS_SPACE`・`NESTED_STATE`・`HYPERV`・`HYPERV_VAPIC`・`HYPERV_SPIN`・`HYPERV_TIME`・`HYPERV_SYNIC`・`HYPERV_SYNIC2`・`HYPERV_VP_INDEX`・`HYPERV_EVENTFD`・`HYPERV_TLBFLUSH`・`HYPERV_SEND_IPI`・`HYPERV_ENLIGHTENED_VMCS`・`HYPERV_CPUID`・`HYPERV_DIRECT_TLBFLUSH`・`HYPERV_ENFORCE_CPUID`・`SYS_HYPERV_CPUID`・`XEN_HVM`・`PMU_EVENT_FILTER`・`PMU_CAPABILITY`・`PMU_EVENT_MASKED_EVENTS`・`SGX_ATTRIBUTE`・`VM_COPY_ENC_CONTEXT_FROM`・`VM_MOVE_ENC_CONTEXT_FROM`・`MEMORY_FAULT_INFO`・`MEMORY_ATTRIBUTES`・`GUEST_MEMFD`・`VM_TYPES`(0 = default だけ)・`BINARY_STATS_FD`・`SYNC_MMU`（最初）・`PTP_KVM`・`VM_DISABLE_NX_HUGE_PAGES`・`X86_GUEST_MODE`・`PV_MMU`（廃止済み） |

### 2.14 `kvm_run` の mmap と exit の理由

| 項目 | 分類 | 時期 | 注意 |
| --- | --- | --- | --- |
| `request_interrupt_window`・`ready_for_interrupt_injection`・`if_flag`・`immediate_exit`・`exit_reason`・`cr8`・`apic_base`・`kvm_valid_regs`/`kvm_dirty_regs`/`s.regs` | A | M1〜M2 | `immediate_exit` は IRQ を切った後、entry の直前に読んで EINTR（§3.3）。`cr8` は TPR |
| `flags` の `KVM_RUN_X86_SMM`・`KVM_RUN_X86_GUEST_MODE` | C | — | SMM・nested |
| `flags` の `KVM_RUN_X86_BUS_LOCK` | A | L | hardware |
| `KVM_EXIT_UNKNOWN`・`IO`・`HLT`・`MMIO`・`IRQ_WINDOW_OPEN`・`SHUTDOWN`（triple fault）・`FAIL_ENTRY`・`INTR`（EINTR と組）・`INTERNAL_ERROR`（`KVM_INTERNAL_ERROR_EMULATION` 等、`ndata`） | A | M1〜M2 | MMIO は §3.8 の emulator |
| `KVM_EXIT_SET_TPR`・`TPR_ACCESS`・`NMI`・`DEBUG`・`EXCEPTION`（未使用）・`SYSTEM_EVENT`・`X86_RDMSR`・`X86_WRMSR`・`IOAPIC_EOI`・`HYPERCALL`・`DIRTY_RING_FULL`・`X86_BUS_LOCK`・`NOTIFY` | A | M2〜L | |
| `KVM_EXIT_HYPERV`・`XEN`・`AP_RESET_HOLD`・`MEMORY_FAULT`・（`KVM_EXIT_S390_*`・`DCR`・`OSI`・`PAPR_HCALL`・`WATCHDOG`・`EPR`・`ARM_NISV`・`RISCV_*`・`LOONGARCH_IOCSR` は N/A） | C | — | |
| PIO data の page（offset 1）、coalesced MMIO/PIO ring（offset 2、`struct kvm_coalesced_mmio_ring`、`first`/`last` の index を userland が進める） | A | M1 / M3 | ring は vCPU ごと。kernel は `last` を進め、満杯なら通常の exit に落とす（Linux と同じ） |
| dirty ring（offset 64〜） | A | L2 | |

## 3. zedBSD の側に要る基盤

### 3.0 ioctl の ABI の 3 つの穴（kernel と libc。HAL ではない。実装の最初の Phase）

レビュー（§12）で見つかった、A の前提になる事実:

1. **成功の ioctl は 0 しか返せない。** `include/kern/file.h` の `int (*ioctl)(struct file *, unsigned long, uintptr_t)` は errno（正）か 0 を返し、
   `src/kern/file.c` の `file_ioctl()` と `src/kern/syscall.c` の `KERN_SYS_ioctl` は `error != 0 ? -error : 0` を返す。既存の driver は正の errno を
   「失敗」として返すので、正の「結果」と両立しない。Linux の KVM は `KVM_GET_API_VERSION`（12）・`KVM_CREATE_VM`・`KVM_CREATE_VCPU`（fd）・
   `KVM_CHECK_EXTENSION`（値）・`KVM_GET_VCPU_MMAP_SIZE`・`KVM_GET/SET_MSRS`（処理した数。QEMU は assert で確かめる）・`KVM_GET_TSC_KHZ`・
   `KVM_GET_NR_MMU_PAGES` で正の値を返す。
   **案（採る）**: `struct file_ops`・`struct cdev_ops` に任意の op `int (*ioctl_value)(struct file *, unsigned long, uintptr_t, intptr_t *value)` を足し、
   `file_ioctl_value()` を syscall が呼ぶ。op が無い file は今の `ioctl` に落ち `*value = 0`。既存の driver は変えない。syscall の戻り値は
   `error != 0 ? -error : *value`。libc の `ioctl()` は `call()` の戻り値をそのまま返す（今も int）。**選ばなかった案**: 全 driver の `ioctl` の
   signature を変える（機械的だが大きい）、KVM の ioctl だけ pointer で結果を返す（VMM の source の互換を失う）。
2. **libc は `_IO` の request の第 3 引数を渡さない。** `userland/base/libc/posix.c` の `ioctl()` は `ioctl_has_argument(request)`（size field ≠ 0 か
   特定の SIOC*）のときだけ `va_arg` を読む。`KVM_CHECK_EXTENSION(cap)`・`KVM_CREATE_VCPU(id)`・`KVM_CREATE_VM(type)`・`KVM_SET_TSS_ADDR`・
   `KVM_SET_TSC_KHZ`・`KVM_SET_NR_MMU_PAGES`・`KVM_SET_BOOT_CPU_ID`・`KVM_REINJECT_CONTROL`（pointer）は `_IO` で値を渡す。
   **案（採る）**: `ioctl_has_argument()` に group `KVMIO`（0xAE）を足す（既存の SIOC* の例外と同じ形）。**選ばなかった案**: 常に `va_arg` を読む
   （amd64・aarch64 の ABI では実害は無いが、規格上は未定義）。host 試験は host の macro を使う（`include/uapi/ioctl.h` の `KERN_UAPI_HOST_LIBC`）ので
   この穴を検出できない → guest での試験（cap ≠ 0 の問い合わせ、vcpu id 1 の作成）を M1 の受け入れに入れる。
3. **`KERN_SYS_ioctl` は SA_RESTART で restart される。** `src/kern/syscall.c` の `syscall_restartable()` に `KERN_SYS_ioctl` がある。Linux の
   `KVM_RUN` は -EINTR を返し restart されない（VMM の kick と `immediate_exit` の前提）。
   **案（採る）**: `ioctl_value` の driver が「restart しない」を印す（`thread` の flag、または戻りの errno を `EINTR` の別名にして dispatcher が
   restart を抑える）。SA_RESTART の handler で kick する試験を M1 に入れる。

### 3.1 VMX の有効化と hardware の要件

- **hardware の要件（必須）**: VMX、EPT、**unrestricted guest**（real mode と protected mode の非 paging を直接走らせる。無いと real mode の全命令の
  emulation が要り、SeaBIOS/OVMF の起動が不可能に近い）、MSR bitmap、TSC offsetting、"activate secondary controls"。**任意**: VPID、EPT の A/D bit、
  VMX preemption timer、APICv（TPR shadow・virtualize APIC accesses・virtual-interrupt delivery・posted interrupt）、TSC scaling、PML、
  `IA32_FEATURE_CONTROL` の lock bit が VMXON を許していること（UEFI の firmware が設定。禁止されていたら `/dev/kvm` の open が ENXIO）。
  検証 host（Xeon Gold 6130、Skylake-SP、`kvm_intel nested=Y`）と実機（Latitude 5330、Alder Lake）はどれも満たす。
- **VMXON**: CPU ごとに 4 KiB の VMXON region（`kern_pmem_alloc`）を持ち、CR4.VMXE を立てて `vmxon` を実行する。CR4 は HAL が所有する（`amd64_cpu_init`）。
  **CR0 の固定 bit**: `IA32_VMX_CR0_FIXED0` は CR0.NE を要求するが、AP の trampoline（`ap-trampoline.S`、PG|PE だけ）も `amd64_cpu_init()`（MP を立て
  EM/TS を落とす）も NE を立てない。BSP の CR0 は firmware 次第（p003 で gdbstub で確かめる）。**VMXON の前に CR0.NE を立てる**のは host の x87 の
  エラーの報告の方式の変更（#MF の直接の配送）で、HAL の責務の変更として H2 に含める。
  全 CPU での実行は、`/dev/kvm` の最初の open のときに CPU ごとの kthread を `sched_set_cpu()` でその CPU に置いて行う（kernel に「この CPU で
  関数を走らせる」API は無いが、`sched_set_cpu` と `kthread_create` で作れる）。最後の close で `vmxoff`。
- **VMX root の副作用**: VMX root では INIT は遮断され（VM exit ではない）、NMI は「NMI exiting」で guest から exit する。**`hal_reset()`・
  `hal_poweroff()`・panic（`amd64_lapic_panic_all`・NMI の stop、`int_handler` の `vector == 2`）の道に VMXOFF が要り**、guest の中の CPU に届いた
  panic の NMI は exit の後に host の `int_handler` の道へ出し直す（H2 の設計項目）。
- **QEMU TCG は VMX を emulate しない**（SVM は emulate する）。VMX の試験は **nested KVM**（host の `-accel kvm -cpu host` の中の zedBSD、
  `plan/tools/guest/guest.py` は既にそうしている）か実機。SVM の backend は TCG の `-cpu ...,+svm` でも試せる。

### 3.2 VMCS と host の状態

- VMCS（4 KiB、revision id は `IA32_VMX_BASIC`）を vCPU ごとに 1 つ。`vmptrld` は CPU に紐づくので、**vCPU thread が別の CPU へ移ると
  旧 CPU で `vmclear` が要る**。zedBSD では thread の移動は珍しくない: idle の CPU が runnable の thread を盗む（`src/kern/sched.c` の
  `take_stealable_locked`）、tick は割り込みの中から `sched_yield()` する（同 976 行付近）、`sched_set_cpu()` は走っていない thread を 1 回動かすだけで
  固定はしない。`guest.py` の既定は 4 CPU なので **M1 から移動は起きる**。したがって「loaded VMCS の CPU ごとの list」と「他 CPU への `vmclear` の
  依頼」は最初（p003）から持つ。`vmclear` の依頼を **IRQ を切ったまま待つと 2 つの CPU が互いに待って deadlock する**ので、依頼と待ちは IRQ を有効にして行い、
  最後の entry だけを IRQ を切って行う。
- **host の状態の field**（VM exit で復元される）は HAL の私有の事実そのもの: `HOST_CR0/3/4`、`HOST_CS/SS/DS/ES/FS/GS/TR` の selector
  （`SEG_KERNEL_CODE` 0x08・`SEG_KERNEL_DATA` 0x10・`SEG_TSS` 0x28、`src/hal/amd64/defs.h`）、`HOST_GS_BASE` = per-CPU の pointer
  （`amd64_percpu_current()` は `%gs:0` を読む。`IA32_GS_BASE` に kernel の pointer を置いていて `swapgs` を使っていない。WS080 の案 A（swapgs）が入ると
  ここも変わる）、**`HOST_FS_BASE` = 今の thread の user の TLS**（`task.c` は context switch で `IA32_FS_BASE` を読んで保存し次の task の値を書く。
  exit で `HOST_FS_BASE` が load されるので、**entry のたびに今の task の値を書く**。書かないと VMM の TLS（errno 等）が壊れる）、`HOST_TR_BASE`・
  `HOST_GDTR/IDTR_BASE`（**CPU ごとに違うので、VMCS を別の CPU に load するたびに書き直す**。Linux の `vmx_vcpu_load_vmcs` と同じ）、
  `HOST_IA32_EFER/PAT`、`HOST_IA32_SYSENTER_*`（未使用、0）、`HOST_RSP/RIP`（exit の trampoline）。
  **これが「VMX の入口・出口は HAL の責務」である理由**である。
- **guest の状態の field**: GPR は VMCS に無いので trampoline が保存・復元する（`struct hal_virt_gpregs`）。RSP/RIP/RFLAGS・segment・CR・DR7・
  `IA32_EFER/PAT/DEBUGCTL`・SYSENTER・interruptibility・activity state・pending debug exceptions・PDPTR は VMCS。
- **MSR の切り替え**: `STAR/LSTAR/CSTAR/FMASK/KERNEL_GS_BASE/TSC_AUX` は guest が syscall を使うなら guest の値が要り、exit で host の値（`defs.h` の
  `AMD64_MSR_STAR` 等、`percpu.c`）に戻す: VM-entry/exit の MSR load list か、`hal_virt_vcpu_run` の中で手動。`KERNEL_GS_BASE` は VMCS に無いので
  guest の値を手動で保存・復元する（zedBSD は swapgs を使わないので host の値は問わないが、guest の値は守る）。`FS_BASE/GS_BASE` は VMCS の field。
- **CR0/CR4 の guest/host mask と read shadow**: CR0.PG/PE/NE と CR4.VMXE/PAE/OSXSAVE 等は host が握り、guest には shadow を見せる。
  CR4.OSXSAVE は HAL が XSAVE を使うまで guest に立てさせない（XSETBV の exit で #GP）。
- **FPU**: `hal_virt_vcpu_run` の前に host（= VMM の user thread）の FXSAVE の像を保存して guest の像を `fxrstor` し、exit の後に戻す。
  HAL の task は `fxsave64` を context switch で使う（`task.c`）ので、guest の像は `hal_virt_vcpu` の中の 512 byte + 将来の XSAVE 領域に置く。
  amd64 の kernel は `-mgeneral-regs-only` で build される（`platform/amd64/vmunix.mk` の `AMD64_CFLAGS`）ので、exit の後の kernel の code が guest の
  FP の像を壊すことは無い。ただし kernel の libc の object（`AMD64_KERNEL_LIBC_CFLAGS` はこの flag を外す）の規則に KVM の source が乗らないよう、
  KVM は `src/drivers/` か `src/kern/` の規則の下に置く。

### 3.3 VM exit の処理と `KVM_RUN` の loop（状態遷移）

```
KVM_RUN(vcpu)
  ├─ vcpu->mutex（1 vCPU に同時に 1 thread。EBUSY）
  ├─ userland の exit の完了（前回の MMIO read の値を emulator へ、PIO の in の値、IRQ_WINDOW の後の注入）
  ├─ loop:
  │   ├─ mp_state が HALTED/UNINITIALIZED → 起こされるまで waitq で眠る（in-kernel irqchip）または KVM_EXIT_HLT（userspace irqchip）
  │   ├─ memslot の世代を読み、EPT/VPID の flush の要求があれば invept/invvpid（§3.4）
  │   ├─ VMCS の CPU が今の CPU と違えば IRQ 有効のまま旧 CPU に vmclear を依頼して待ち、vmptrld と host の field の書き直し（§3.2）
  │   ├─ event の注入（exception > NMI > 外部割り込み。interrupt-window exiting の設定）
  │   ├─ hal_irq_disable()
  │   │    vcpu->mode = IN_GUEST（他 CPU の kick が IPI を選ぶ印）、hal_mb()
  │   │    immediate_exit / signal_pending_unblocked(curthread) / kick の要求 を **ここで** 確かめる（IRQ を切る前に確かめると、
  │   │      確認と disable の間に届いた IPI を host が受けてしまい kick が失われる）→ あれば mode を戻して EINTR（KVM_EXIT_INTR）
  │   │    HOST_FS_BASE を書く → hal_virt_vcpu_run() → vcpu->mode = OUT
  │   │    hal_irq_enable()（external-interrupt exiting、acknowledge 無し: ここで host の割り込みを IDT で受け、tick が preempt を要求すれば sched_preempt_point()）
  │   ├─ exit の分岐:
  │   │    CPUID/RDMSR/WRMSR/XSETBV/CR access/DR access/INVD/WBINVD/RDTSC(P)/HLT/PAUSE/MOV to CR8 → kernel で処理して loop
  │   │    EPT violation: memslot にある → EPT を埋めて loop（lazy）/ 無い（MMIO） → emulator（§3.8）→ KVM_EXIT_MMIO で userland へ
  │   │    EPT misconfig → KVM_EXIT_INTERNAL_ERROR
  │   │    I/O instruction → KVM_EXIT_IO（string は emulator の walker で guest memory を写す）
  │   │    interrupt window / NMI window → 注入して loop、または KVM_EXIT_IRQ_WINDOW_OPEN（userspace irqchip）
  │   │    external interrupt → host が処理済み。signal/preempt の確認へ
  │   │    exception/NMI（#PF は EPT では起きない。#DB/#BP は guest debug、#MC、#UD → emulator の invalid opcode の注入。NMI は host へ出し直す）
  │   │    triple fault → KVM_EXIT_SHUTDOWN、APIC access/write（APICv）→ LAPIC model、preemption timer → LAPIC timer の満了
  │   │    entry failure → KVM_EXIT_FAIL_ENTRY
  │   ├─ signal / immediate_exit / need_resched を確認して loop か抜ける
  └─ kvm_run に exit を書いて return 0（EINTR は restart しない、§3.0）
```

- **他 thread からの kick**（QEMU の `qemu_cpu_kick` = `pthread_kill(SIG_IPI)`、cloud-hypervisor の `immediate_exit` + signal）: zedBSD の
  `signal_send_thread()` → `sched_interrupt()` は走っている thread の CPU へ `notify_cpu()`（IPI、vector 0xf0）を送る（`sched.c` 456 行、
  `signal.c` 520 行）。guest の中の CPU は external-interrupt exiting で exit し、loop の IRQ を切った区間で `signal_pending_unblocked()` を見る。
  in-kernel irqchip の割り込みの kick（§3.6）は `vcpu->mode` を見て IN_GUEST なら `hal_cpu_notify(cpu)`。**追加の仕組みは要らない**。
- **preemption**: kernel の中の thread は tick で preempt される（`sched.c` の `preempt_count` と `need_resched`）。VM entry の間は IRQ を切るので、
  guest の中では tick は exit を起こし、その直後の `hal_irq_enable()` で tick の handler が走り、`need_resched` が立てば loop の先頭で
  `sched_preempt_point()`。guest の quantum の扱いは Linux と同じ「host の scheduler に任せる」。

### 3.4 EPT・memslot・guest memory の pin と `vm_object`

- **memslot の表**: GPA の区間 → user の address。重なりは EINVAL。`KVM_CAP_NR_MEMSLOTS` 個。
- **並行制御（レビューで追加）**: `invept` は実行した CPU にしか効かない。slot の削除・移動・unpin のとき他の vCPU が guest の中にいる（または他の CPU に
  EPT の translation が残っている）と、**解放・再利用された host の page へ guest が書ける**（host の memory の破壊）。設計:
  (1) memslot の表は writer の mutex と **世代番号**で守り、vCPU の exit の処理は世代を読んで表を参照する（Linux の SRCU に当たる、読み手は lock 無し）。
  (2) 表を変えたら世代を進め、全 vCPU に「flush 要求」を立て、IN_GUEST の vCPU に IPI で exit させ、**全 vCPU が新しい世代で `invept` を終えるまで待ってから**
  unpin する。(3) VM の破棄も同じ道。`-smp 4` で走っている間に slot を削除する試験を p004 に入れる。
- **pin**: zedBSD は anonymous の page を swap へ出す（`VM_PAGE_SWAPPED`、`vm_private_page` の reclaim queue、`vm_object_page` の reclaim）。
  EPT に HPA を書いたら host が page を動かせないので、**memslot の全 page を `vmspace_wire_range()` で wire**（`src/kern/vmspace.c`。
  `wire_count` で reclaim から外れる。**今は呼び出し元が無い未検証の道**）し、HPA は `vmspace_pin_user_pages()` から得る。pin の配列は page ごとに
  約 48 byte（`struct vmspace_pinned_page`）で 8 GiB の guest に約 96 MiB、しかも範囲の全体で `vm->lock` と metadata の lock を持つので、
  **2 MiB 程度の塊に分けて pin する**。commit の会計は design policy 10（over commit 禁止、裏打ちは物理 + swap）に従い、8 GiB の guest には 8 GiB の
  commit が要る（VMM の `mmap` の時点で取られる。`MAP_NORESERVE` は受け入れて無視する仕様）。**8 GiB の host の中で試験するなら L2 の guest は 512 MiB〜1 GiB**。
- **wire の時期**: `KVM_SET_USER_MEMORY_REGION` で全 page を wire・fault させて EPT を一度に埋める（eager）。単純で、`KVM_CAP_SYNC_MMU` 0 と整合し、
  性能も予測できる。lazy は後（`KVM_PRE_FAULT_MEMORY` と同居できる）。
- **user の region の変化**: `munmap` は wire された page があれば **既に EBUSY を返す**（`vmspace.c` の unmap の検査。§3.4 の問いは解けている）。
  `mprotect` は wire を見ない（`vmspace_protect_locked`）ので、memslot の範囲の `mprotect` は EPT に反映されない。**wire された範囲の `mprotect` を
  EBUSY で拒む**（vmspace の小さな変更）か、制限として文書に書く。p004 で前者。
- **fork**: 他 thread が pin した private page は fork で **即座に複製される**（`vmspace.c` の fork の COW の道: pinned なら `prepare_cow_copy`）。
  8 GiB を pin した VMM が `fork()` すると 8 GiB の複製（commit の失敗か長い停止）になる。`MADV_DONTFORK` は無い。QEMU の patch では `fork()` を
  `posix_spawn`/`vfork` に置き換える（QEMU の fork は helper の起動だけ）。制限として文書に書く。
- **file backed・shared の memslot**: `vm_object_page_pin()` は reclaim から守るが **resize からは守らず**（`vm-object.h`「resize は古い frame を孤立
  させうる」）、EPT からの書き込みは `vm_object_page_pin_write()` を通らないので dirty の追跡（writeback）も迂回する。regular file を `MAP_SHARED` した
  memslot は、`ftruncate` の後に guest と他の見る内容が分かれ、file に書き戻されない。**M1〜M3 は anonymous（private・shared）の region だけを受け、
  file backed（QEMU の `-mem-path`）と device backed（`vm_device_mapping`、cache 属性が WB でない）は EINVAL**。shm（tmpfs の object）の shared は
  resize を禁じる印が要るので M3 の後。
- **EPT の table**: 4 段（`kern_pmem_alloc` の 4 KiB）。memory type は WB（VMM が MMIO と RAM を分けるので guest の MTRR は無視する）。
  **2 MiB の large page は当面使わない**（zedBSD の anonymous page は物理的に連続しないため。性能のためには「物理的に連続な anonymous の object」が要る →
  Future Work）。TLB の cost は VPID と `invept` の single-context で抑える。
- **EPT の table は誰が持つか**: EPT は host の page table でなく「guest 物理 → host 物理」の表で、hal.h の「kernel は page table に触れない」
  （`hal_space_*` の契約）の文言に直接は当たらないが、解釈の余地がある（§10.2 でユーザーの判断）。案 B では **EPT の構築は driver が行い**、root の
  物理 address と無効化だけを HAL に頼む。理由: format は VMX の仕様で公開されており、HAL に置くと memslot の意味（KVM の API）が HAL に漏れる。

### 3.5 vCPU thread と scheduler

- vCPU = VMM の user thread（`thread_create` の 1:1 の thread）が `KVM_RUN` を呼んで kernel の中で guest を走らせる形（Linux と同じ）。kthread は作らない。
- **CPU の移動**: §3.2 のとおり固定はできない。移動に追従する（VMCS の載せ替え、host の field の書き直し、VPID/EPT の flush）。
- **halt**: in-kernel irqchip では HLT で vCPU thread が `waitq_sleep`（`WAITQ_INTERRUPTIBLE`、deadline は LAPIC timer の次の満了）で眠り、割り込みの
  注入（他 thread の `KVM_IRQ_LINE`・socket の irqfd・LAPIC timer）で `waitq_wake_one`。halt polling（`KVM_CAP_HALT_POLL`）は後。
- **優先度**: vCPU thread は普通の user thread の優先度。real-time は扱わない。

### 3.6 割り込みの注入・LAPIC・APICv・posted interrupt

- **M2（userspace irqchip）**: 注入は VM-entry interruption-information field。IF=0 や interrupt shadow のときは interrupt-window exiting を立てて
  window が開いたら注入（`KVM_INTERRUPT` の 1 個の pending を kernel が持つ）。NMI は NMI-window exiting。exception は `KVM_SET_VCPU_EVENTS` と emulator から。
- **M3（in-kernel irqchip）**: LAPIC の model（register 1 KiB、xAPIC の MMIO と x2APIC の MSR、ISR/IRR/TMR、TPR/PPR、LVT、timer の one-shot/periodic/
  TSC-deadline、IPI（ICR）による他 vCPU への配送、INIT/SIPI → mp_state、EOI と level の resample）、IOAPIC（24 pin、redirection table、EOI の
  broadcast）、PIC 8259 × 2、i8254 PIT、GSI routing の表、MSI の address/data の decode。**Linux の相当部分（GPL）は読まず独自に書く（約 4,000 行）**。
  HAL の LAPIC（`bsp-pcat/lapic.c`、xAPIC の MMIO mode）とは無関係（guest の model）。
- **APICv / posted interrupt（L）**: virtual APIC page・TPR shadow・APIC-register virtualization・virtual-interrupt delivery で EOI/TPR/ICR の exit を
  減らし、posted interrupt で他 CPU から exit 無しに注入する。posted interrupt は **notification vector（IDT の 1 本）が要り、HAL の vector の
  配置（`defs.h`: MSI 0xd0〜0xdf、IRQ 0xe0〜0xef、notify 0xf0、TLB 0xf1、error 0xfe、spurious 0xff）に足す HAL の変更**。
  nested（QEMU の上）で APICv が使えるかは L0 の KVM の `enable_apicv` 次第なので、実機での確認になる。
- **IRQ の kick の道（M3）**: 他 thread が vCPU に割り込みを立てたら、その vCPU が (a) 眠っていれば `waitq_wake_one`、(b) `vcpu->mode` が IN_GUEST なら
  `hal_cpu_notify(cpu)`（IPI）で exit させ、loop で注入。(c) kernel の中なら loop の IRQ を切った区間で見る。

### 3.7 TSC と時刻

- **guest の TSC** = host の TSC × scale + offset（VMCS の TSC offset、Skylake 以降は multiplier）。`RDTSC` は exit させない。`KVM_SET_TSC_KHZ`・
  `IA32_TSC` の書き込み・`TSC_ADJUST` を offset に写す。host の TSC の周波数は HAL の virt の probe（H1）が返す（`hal_rtc_read_counter()` は
  「線形化した単調な counter」であって生の TSC ではない: `hal.h` の契約と `timecounter.c` の skew の clamp）。
- **kvmclock（M3）**: guest の `MSR_KVM_SYSTEM_TIME_NEW`/`WALL_CLOCK_NEW` に pvclock の構造を書く（memslot の HPA 経由）。`KVM_GET/SET_CLOCK` はその基準。
  KVM の pv の CPUID leaf（0x40000000〜0x40000001）を見せる。無くても TSC（invariant を見せる）や ACPI PM timer（QEMU の emulation）で動く。
- **guest の timer の精度**: LAPIC timer の満了は、vCPU が走っている間は **VMX preemption timer**（TSC の単位）で正確に取れる。vCPU が眠っている
  （HLT）間は host の timer で起こす必要があり、zedBSD の timer は **1 ms の tick**（`HAL_TIMER_FREQUENCY` 1000、`clock.h`「hrtimer は pending の
  HAL item」）。つまり **halt 中の guest の timer は最大 1 ms 遅れる**。改善は HAL の one-shot の高分解能 timer（H10）の追加（後）。

### 3.8 命令の decoder/emulator（最大の実装項目）

- **なぜ要るか**: EPT violation は「GPA・読み書き」しか教えず、**MMIO の命令の長さ・operand・値は命令を decode しないと分からない**。
  `KVM_EXIT_MMIO` は `len`・`data`・`is_write` を要求する。PIO の `IN/OUT` は exit qualification で足りる（decode 不要）が、`INS/OUTS` は
  guest の linear address から guest memory を読み書きするので **guest の page table を歩く walker**（GVA → GPA、`KVM_TRANSLATE` と共用）が要る。
- **範囲（MMIO の subset）**: 実際の guest（Linux の driver、SeaBIOS、OVMF）が MMIO に使う命令: `MOV` r/m（8/16/32/64、`MOVZX/MOVSX`）、
  `MOVS/STOS`（`REP` 付き）、`ADD/OR/AND/XOR/SUB/CMP/TEST` の memory operand、`BT*`、`XCHG`、`CMPXCHG`、`PUSH/POP` mem、`INC/DEC`、`NOT/NEG`、
  `MOVNTI`、`MOVD/MOVQ`、prefix（REX・operand/address size・segment・LOCK・REP）、16/32/64 bit の各 mode、segment の base と limit（real mode・big real）。
  **独自実装（GPL の Linux の emulator は読まない・写さない）**で 3,000〜4,000 行、host 試験（decoder の表を host で compile して数千の encoding を照合）が
  要る。未対応の命令は `KVM_EXIT_INTERNAL_ERROR`（`KVM_INTERNAL_ERROR_EMULATION`、`KVM_CAP_EXIT_ON_EMULATION_FAILURE` で命令 byte も渡す）。
  **guest が host を攻撃する面**なので、decoder の fuzz（乱数の byte 列を host 試験で decode させて crash と範囲外の access が無いこと）を試験に入れる。
- **real mode の emulation は不要**（unrestricted guest）。A20・vm86 も不要。
- **APIC access の exit（APICv）** も同じ decoder を使う。

### 3.9 FPU・XSAVE（HAL の責務の変更）

- 今: `amd64_cpu_init()` は CR4.OSFXSR/OSXMMEXCPT だけ立て、task の FP 文脈は 512 byte の FXSAVE（`task.h` `fpregs[512+15]`、signal frame も 512）。
  **host が XSAVE を使わないので、guest の AVX/AVX-512 の state を exit で保存できない** → guest には XSAVE/AVX を見せない（CPUID の mask、XCR0=3）。
- HAL を XSAVE に変えると task の文脈が最大約 2.7 KiB（AVX-512）になり、signal frame・`ucontext`（libc の ABI）・`hal_fpregs/hal_vregs`（ptrace）に
  波及する。**別の承認項目**として §3.10 に置く。最初の到達点には要らない。

### 3.10 HAL の責務と hal.h の差分（差分ごとの承認が要る）

**方針の案（採る案 B）**: HAL には「privileged な CPU の状態に触る最小の primitive」だけを置き、KVM の意味（memslot・EPT の table・
emulator・irqchip・ioctl）は driver `src/drivers/virt/kvm/`（`drv_kvm_` の symbol、`CONFIG_DRIVER_KVM`、amd64 だけ）に置く。driver は HAL を
直接呼ばず（`include/kern/device-io.h`「driver は kernel を通す」の規則）、**kernel の薄い wrapper `include/kern/virt.h`（`kern_virt_*`、
`src/kern/virt.c`）を通す**（`kern_io_*`・`kern_irq_*` と同じ形。gpu.c が `hal_space_map_device` を直接呼ぶ前例はあるが規則には従う）。
理由: (1) VMCS の host state・trampoline・CR0/CR4/MSR・vector の配置は HAL の私有の事実（§3.2）。(2) EPT・emulator・irqchip は仕様が公開された
x86 の機構で、HAL の「kernel の移植の契約」に入れると HAL が数万行に肥える（O4 の最小の HAL に反する）。(3) design policy 2.6（late abstraction）:
まず動かし、aarch64 の時に共通の API を抽出する。

選ばなかった案: **A（全部 HAL）**: `hal_vm_create/run/get_regs` のような高い API。aarch64 で同じ API を再利用できる利点はあるが、KVM の x86 の
struct をそのまま HAL に置くことになり、HAL の変更の承認の単位が大きすぎる。**C（全部 driver、inline asm）**: hal.h を変えずに済むが、host の
GS base・TSS・GDT・IDT・MSR の値を driver が HAL の private header（`defs.h`・`percpu.h`）から読むことになり、層の境界を壊す。driver に
`__asm__` がある前例（`pcat-ide.c` 等の `in/out`）は port I/O の範囲で、CR4/VMCS とは違う。

**header の置き方（§10.2 でユーザーの判断）**: 案 B-1 `include/hal/hal.h` に節「Virtualization」を足す（design policy 2.6「少数のまとまった header を
好む」に沿う。ただし無い port は宣言だけ持つ）。案 B-2 新しい `include/hal/virt.h`（任意の機能。無い port は `hal_virt_probe()` が false）。
どちらも「hal.h の宣言・契約・責務」の変更として承認の対象。本書の推奨は **B-1**（policy 2.6 の文言に沿う。2.6 は「追加は明示の決定で」とも言うので、
この Phase がその決定になる）。

| # | 差分（案の名前） | 責務 | 承認 |
| --- | --- | --- | --- |
| H1 | `hal_virt_probe(struct hal_virt_info *)`: backend（VMX/SVM）、EPT/NPT・unrestricted・VPID・APICv・preemption timer・TSC scaling・A/D の有無、物理 address の bit 数、VMCS の revision、**TSC の周波数** | 能力の問い合わせ | hal.h の追加 |
| H2 | `hal_virt_cpu_enable()`/`hal_virt_cpu_disable()`: 今の CPU で VMXON/VMXOFF（VMXON region、CR4.VMXE、**CR0.NE**、`IA32_FEATURE_CONTROL`）。**`hal_reset`・`hal_poweroff`・panic の道の VMXOFF**、NMI exiting の後の NMI の再配送 | CR0/CR4・VMX の状態・reset | 同上、`asm.c`/`int.c`/`smp.c` の実装 |
| H3 | `hal_virt_vcpu_create/destroy(config)`: VMCS の確保、host state の field、control の既定（MSR bitmap、外部割り込み exiting、NMI exiting、EPT/VPID の有効化）、guest 領域の FP の像 | VMCS の host 側 | 同上 |
| H4 | `hal_virt_vcpu_load/put`: `vmptrld`/`vmclear`、**CPU ごとの host の field（GS base・TR・GDTR・IDTR）の書き直し**、他 CPU に載っている VMCS の IPI での clear（IRQ 有効で待つ。`AMD64_VECTOR_NOTIFY` を再利用するか新 vector） | CPU の紐づけ | 同上。新 vector なら `defs.h` |
| H5 | `hal_virt_vcpu_read/write(field, value)`: VMCS の field の読み書き（field の番号は VMX の仕様、driver が渡す） | `vmread/vmwrite` | 同上 |
| H6 | `hal_virt_vcpu_run(vcpu, gpregs, exit)`: entry/exit の trampoline、GPR・FP・host MSR・**`HOST_FS_BASE`（今の task の TLS）・guest の `KERNEL_GS_BASE`** の保存復元、`vmlaunch/vmresume`、exit reason・qualification・guest linear/physical address・interruption info・instruction length を返す。呼び出しは IRQ 無効で | 入口・出口 | 同上、`trap.S` 相当の新 `.S` |
| H7 | `hal_virt_nested_root_set(vcpu, paddr, flags)`、`hal_virt_nested_invalidate(paddr)`（`invept`、**今の CPU だけ**。CPU 間の同期は driver の世代と kick、§3.4）、`hal_virt_vpid_invalidate(vpid)` | EPT の root と無効化 | 同上 |
| H8 | `hal_virt_msr_read/write(msr)`: host の MSR（`IA32_VMX_*` の能力 MSR、TSC、MTRR/PAT の値、APIC base）。**一般の MSR の API を driver に開けない**ので、名前で限った accessor にする | MSR | 同上 |
| H9 | （L）posted interrupt の notification vector と handler の登録 | vector の配置 | `defs.h`・`int.c` |
| H10 | （L）one-shot の高分解能 timer（`hal_timer_oneshot(deadline_counter)`、LAPIC の TSC-deadline mode） | timer | hal.h の追加。KVM 以外（`kern_usleep_range` の代替）にも効く |
| H11 | （L）FP 文脈を XSAVE に（task・signal frame・ptrace の register set） | task | HAL の責務の変更 + libc の ABI |
| H12 | （L）SVM の backend（同じ H1〜H8 の interface、VMCB） | AMD | `src/hal/amd64/virt/svm.c` |

**kernel と libc の側の追加（HAL ではない。承認の単位は Phase）**:

| # | 変更 | 場所 |
| --- | --- | --- |
| K1 | ioctl の正の戻り値（`ioctl_value` op）、restart しない EINTR（§3.0） | `include/kern/file.h`・`cdev.h`、`src/kern/file.c`・`syscall.c` |
| K2 | libc の `ioctl()` が `KVMIO` の group で引数を渡す（§3.0） | `userland/base/libc/posix.c` |
| K3 | `/dev/kvm` の権限（devfs の既定は 0666。`event*` 0640・`input-inject` 0600 の例外と同じ形で **0660 root:kvm**、`kvm` の group を `/etc/group` の既定に） | `src/kern/devfs.c`、rootfs |
| K4 | wire する memory の上限（uid ごと・VM ごと。RLIMIT_MEMLOCK は無い → sysctl `kern.kvm.wired_max`、既定は物理の 1/2 程度） | driver + sysctl |
| K5 | wire された範囲の `mprotect` を拒む（§3.4） | `src/kern/vmspace.c` |
| K6 | unix socket の kernel の消費者 hook・予約した packet・受信の callback（§4.3） | `src/kern/net/unix-socket.c`・`socket.c`・`packet-buf.h` |
| K7 | `include/kern/virt.h`・`src/kern/virt.c` の wrapper、`CONFIG_DRIVER_KVM`、`/dev/kvm` の cdev（`cdev_register_managed`、device 番号は他に倣う） | kernel・config |

### 3.11 kernel の image の大きさ・構成

amd64 の kernel は 16 MiB の上限（`AMD64_KERNEL_MAX_BYTES`）で、i915 入りの build はその近く（master の Tools の注意）。KVM は
全体で 400〜600 KiB の code になる見込みなので **`CONFIG_DRIVER_KVM`（既定 n。KVM を使う image の config だけ y）**。loadable module は無い（monolithic）。

## 4. unix socket による通知の設計（eventfd の代替）

### 4.1 何を置き換えるか

| Linux | 向き | zedBSD |
| --- | --- | --- |
| `KVM_IOEVENTFD`: guest の PIO/MMIO の write が address（と datamatch）に合うと eventfd の counter を +1。userland へ exit しない | kernel → VMM | kernel が VMM の socket へ datagram を送る |
| `KVM_IRQFD`: VMM（または vhost・別 process）が eventfd に書くと GSI を注入 | VMM → kernel | VMM が socket へ datagram を送ると kernel の消費者が受けて注入 |
| `KVM_IRQFD_FLAG_RESAMPLE`: level の IRQ の EOI で resamplefd に書く | kernel → VMM | resample の socket へ datagram |
| `KVM_HYPERV_EVENTFD`・Xen の evtchn の eventfd | — | C |

通常の `KVM_EXIT_IO/MMIO`（`kvm_run` の同期の往復）は変えない。

### 4.2 fd の種類と登録（検査を含む）

- 受け入れる fd: **AF_UNIX・SOCK_DGRAM・接続済み（`socketpair` か `connect` 済み）・path に bind していない** socket。SOCK_STREAM は message の
  境界が無いので受けない（SOCK_SEQPACKET は zedBSD に無い）。**path に bind した datagram の socket は、その path に書ける他の process が `sendto`
  できる**（`unix-socket.c` の `unix_resolve_endpoint`）ので、irqfd に使うと他人が割り込みを注入できる → 拒む。
- 渡す fd は **kernel が使う端**: ioeventfd なら kernel が送る端（peer を VMM が読む）、irqfd なら kernel が受ける端（peer に VMM が書く）。
  socketpair の両端は対称なので VMM は好きな端を渡せる。
- kernel は `filedesc_get_ref(fd)` で `struct file` を取り、socket の file（`socket-file.c`）であることを確かめて登録の間 reference を持つ。
  **登録のとき、その socket の受信 queue に溜まっている datagram は捨て（irqfd）、ancillary の rights（SCM_RIGHTS）を含む datagram は拒む**:
  VM の fd 自身を socket に送っておくと VM → socket → VM の fd の循環ができ、pin した guest memory ごと永久に漏れる（unix socket の送信中の fd の
  GC は無い）。登録の後に届いた rights 付きの datagram は kernel の消費者が rights を解放する。
- **SCM_RIGHTS は kernel 側には要らない**（Linux の `eventfd_ctx_fdget` と同じく呼び出し process の fd 番号で解決する）。
- `KVM_IOEVENTFD`/`KVM_IRQFD` の struct は Linux と同じ（`fd`・`addr`・`len`・`datamatch`・`flags` / `fd`・`gsi`・`flags`・`resamplefd`）。
  `KVM_IOEVENTFD_FLAG_DEASSIGN`・`KVM_IRQFD_FLAG_DEASSIGN` で解除。**deassign の意味**: hook を外し、実行中の hook の呼び出しが終わるのを待ってから
  socket を通常の queue に戻す。deassign の前に届いて hook が処理した datagram は注入済み、後に届いたものは通常の queue に残る（QEMU の MSI-X の
  mask の道が irqfd を外して userland で受けるのはこの形）。

### 4.3 message の形と意味

```c
/* include/uapi/kvm.h（zedBSD の追加。native endian、32 byte） */
struct kvm_notify {
	uint32_t magic;      /* KVM_NOTIFY_MAGIC = 0x6e6d766b ("kvmn") */
	uint16_t version;    /* 1 */
	uint16_t kind;       /* KVM_NOTIFY_IOEVENT 1, KVM_NOTIFY_RESAMPLE 2, KVM_NOTIFY_IRQ 3 (VMM->kernel), KVM_NOTIFY_IRQ_LEVEL 4 */
	uint32_t flags;      /* KVM_NOTIFY_PIO 1, KVM_NOTIFY_COALESCED 2 (前の通知と合体した), KVM_NOTIFY_DATAMATCH 4 */
	uint32_t sequence;   /* 登録ごとの連番: 合体の数が分かる */
	uint64_t address;    /* ioevent: guest の address。irq: gsi */
	uint64_t data;       /* ioevent: 書かれた値（len で切る）。irq: level */
};
```

- **kernel → VMM（ioeventfd・resample）**: guest の write に一致したら vCPU thread の文脈（exit の処理の中）で `struct kvm_notify` 1 通を送る。
  **VMM は payload を読まなくてよい**（QEMU の `EventNotifier` は読み捨てる）。
- **packet の予約（レビューで変更）**: unix の datagram は `struct packet_buf` に載り、その pool は **system 全体で 32 個**（`include/kern/net/packet-buf.h`
  `PACKET_BUF_POOL_COUNT`、tcp・udp・arp・NIC と共有）、socket ごとの受信は 8 通まで（`SOCKET_RECEIVE_MESSAGES_MAX`）。pool から取ると guest が
  host の network を止められ、pool が尽きた時の再送の契機も無い。そこで **登録ごとに 1 個の packet を登録時に確保して持ち、peer の queue に無いとき
  だけ積む**（積んである間の write は `sequence` を進めるだけ = 合体。eventfd の counter と同じ「最後の write の後に少なくとも 1 通」の意味）。
  VMM が受信して packet が解放されるとき、packet の owner の callback（`packet_buf` に owner/release の hook を足す）で登録へ戻す。
  socket の受信の上限（8 通）に当たって積めなかったときは `pending` を立て、その socket のどれかの packet の解放の callback で積み直す。
  `unix-socket.c` に既にある `reserved_packet` の仕組みを手本にする。
- **送信の規律**: kernel の送信は **決して眠らず**（`socket_enqueue_packet_wait` を通らない、MSG_DONTWAIT 相当）、peer が閉じていても **SIGPIPE を
  vCPU thread に送らない**（`unix_send_epipe` は `MSG_NOSIGNAL` 無しだと `signal_send_thread(SIGPIPE)` する → kernel 内部の送信は NOSIGNAL 相当の flag）。
- **VMM → kernel（irqfd）**: socket に **kernel の消費者 hook** を登録する（`socket` に `(*kernel_receiver)(socket, packet, arg)` を足し、
  `unix_datagram_send` が peer の queue に積む代わりに hook を呼ぶ。hook は送り手の syscall の文脈で走り、payload を見て packet を解放し、GSI を注入
  （LAPIC model、他 vCPU への IPI）する）。**payload は無視してよい**（どんな datagram でも「assert」。`kvm_notify` の `KVM_NOTIFY_IRQ_LEVEL` なら level 付き）。
  VMM 側は `write(fd, &one, 8)`（eventfd と同じ呼び方）で動く。hook の登録・解除は VM の mutex の下で行い、解除は実行中の呼び出しを待つ。
- **resample**: level の GSI を irqfd で立て、guest の EOI（in-kernel IOAPIC）で kernel が resample の socket へ `KVM_NOTIFY_RESAMPLE` を送る。
- **順序**: datagram は socket ごとに FIFO。1 つの socket に複数の登録を束ねてもよい（`address`・`sequence` で見分けられる。eventfd に無い利点）。
- **閉じたとき**: VMM が peer を閉じたら送信は失敗し、kernel は黙って捨てる（登録は VM の close まで残る）。kernel の端の file は VM が持つので VMM が
  閉じても壊れない。VM の破棄で全登録を解除し packet を返す。

### 4.4 遅延と throughput の見積もり

根拠の数字: zedBSD の syscall は 450 ns（q422、`plan/history/index.md` 205 行、QEMU の guest の kbench）。pipe の往復は kbench にあるが値が history に無い
（p002 で `plan/tools/kbench` を走らせて取る）。Linux の eventfd の write は約 0.3〜0.5 µs、irqfd → 注入 → vCPU の exit/entry は 2〜4 µs（posted
interrupt 無し）、VM exit/entry の往復は約 1〜1.5 µs（Skylake、nested では 5〜10 倍）。

| 経路 | Linux（eventfd） | zedBSD（socket）の見積もり | 主な差 |
| --- | --- | --- | --- |
| ioeventfd: guest の write → VMM の thread が起きる | exit 1.5 µs + eventfd_signal 0.3 µs + wakeup 1〜2 µs ≈ 3〜4 µs | exit 1.5 + 予約 packet の enqueue 0.3 + `poll_notify` 1〜2（+ 他の poller の起床）≈ 3〜4 µs | `poll_notify` が **system 全体の全 poller を起こす**（`src/kern/poll.c`: 1 本の channel、`waitq_wake_all`）ので、VMM 以外の process の poll も含めて O(N) |
| irqfd: VMM の write → guest に割り込み | write 0.5 + 注入 + IPI + exit/entry 2〜3 ≈ 3〜4 µs | sendto 0.5〜1（syscall 450 ns + hook）+ 注入 + IPI + exit/entry 2〜3 ≈ 3〜4 µs | ほぼ同じ |
| throughput（1 core、通知だけ） | eventfd write ≈ 1.5〜2 M/s | sendto ≈ 0.7〜1 M/s | virtio-net の 100k〜500k kick/s の範囲では律速にならない |

**最大の懸念は poll の設計**（system 全体で全員起こす）で、通知のたびに無関係の process の poller も起きて再走査する。M3 の 1 process の QEMU でも
desktop の他の process（compositor 等）が poll していれば起きる。KVM の問題ではなく zedBSD の poll の改善（file ごとの wait queue）として別に記録する
（Future Work の候補）。p012 で「他の process が poll している状態」で測る。

### 4.5 VMM への影響

| VMM | 変更 | 大きさ |
| --- | --- | --- |
| QEMU 10 | (1) `util/event_notifier-posix.c`: `CONFIG_EVENTFD` 無しの pipe の fallback を **socketpair(AF_UNIX, SOCK_DGRAM)** に（`rfd`/`wfd` の 2 端。`event_notifier_set` は `send`、`test_and_clear` は `recv` を EAGAIN まで）。(2) `accel/kvm/kvm-all.c` の `kvm_set_ioeventfd_mmio/pio` に `event_notifier_get_wfd(e)`（kernel が送る端）を渡す（今は `get_fd` = rfd）。irqfd は `get_fd`（rfd、kernel が受ける端）のままでよい。(3) meson: KVM は `get_option('kvm').allowed() and host_os == 'linux'`（`meson.build` 870 行）で Linux だけなので zedbsd を足す。`linux-headers/` の代わりに zedBSD の `<uapi/kvm.h>`。(4) `memfd`・`eventfd`・`epoll`・`timerfd`・`signalfd` は QEMU 自身が fallback を持つ。(5) `KVM_CAP_*` の必須の集合は §5.1。(6) `fork()` を `posix_spawn` に（§3.4）。**QEMU に新しい host OS を足す作業（meson の host_os・osdep・thread・coroutine の backend・`qemu/osdep.h` の欠けた関数）と、glib 2.66+・pixman・zlib・meson/ninja のクロスビルドは、数十行では済まず数百〜千行の patch と package の整備で、別の WS**（package の WS の規則）。M2/M3 はこの WS に依存する | KVM に固有の patch は百行程度。移植の WS は大 |
| Firecracker（Rust、Apache-2.0） | `EventFd`（`vmm-sys-util`）を socketpair に差し替える trait の実装、`kvm-ioctls`/`kvm-bindings` の zedBSD 対応（bindgen で zedBSD の header から生成 = ioctl の数値が違うので必須）、`epoll` → `poll`、`memfd`/`MAP_NORESERVE`・`timerfd`・`signalfd` の代替、seccomp（Linux 専用、無効化）。**zedBSD 向けの Rust の target（`x86_64-unknown-zedbsd`）が無い**ので toolchain の追加が先（main の許可、別の WS） | 大。Rust の移植が前提 |
| cloud-hypervisor（Rust、Apache-2.0/BSD-3） | 同上 + `hypervisor` crate の KVM の backend、`memfd_create` → `shm_open`（shared の memslot は M3 の後、§3.4）、`vhost-user` の fd の受け渡しは SCM_RIGHTS（zedBSD は 1 message に 8 個まで = vhost-user の `VHOST_MEMORY_MAX_NREGIONS` と同じで足りる） | 同上 |
| 自作の最小 VMM（M1、C） | 無し（zedBSD の API をそのまま使う） | 数百行 |

**SCM_RIGHTS の要否**: kernel の登録には不要。VMM の多 process 構成（vhost-user の backend に kick/call の fd を渡す）では eventfd の代わりの socket の
端を SCM_RIGHTS で渡す。zedBSD は SCM_RIGHTS を持つ（`KERN_MSG_FD_MAX` 8）。kernel が VMM に socket を「返す」形は採らない: userland が作る方が
Linux と同じ責務の分担で、fd の所有が明快。

## 5. 各 VMM の最小の集合と最初の到達点

### 5.1 必須の ioctl と capability（source から読んだ集合）

| VMM | 必須の capability（無いと起動しない） | 主に呼ぶ ioctl |
| --- | --- | --- |
| QEMU 10.0（共通 `kvm_required_capabilites`） | `USER_MEMORY`・`DESTROY_MEMORY_REGION_WORKS`・`JOIN_MEMORY_REGIONS_WORKS`・`INTERNAL_ERROR_DATA`・`IOEVENTFD`・`IOEVENTFD_ANY_LENGTH` | `GET_API_VERSION`（=12）・`CREATE_VM`・`CHECK_EXTENSION`・`GET_VCPU_MMAP_SIZE`・`SET_USER_MEMORY_REGION(2)`・`CREATE_VCPU`・`RUN`・`IOEVENTFD`（virtio の ioeventfd=on のとき）・`IRQ_LINE(_STATUS)`・`SET_GSI_ROUTING`・`SIGNAL_MSI`・`IRQFD`（in-kernel irqchip のとき。`kvm_irqchip_create` は `kernel_irqchip_allowed` のときだけ呼ばれ、その中で `KVM_CAP_IRQFD` を要求する） |
| QEMU 10.0 x86（`kvm_arch_required_capabilities`） | `SET_TSS_ADDR`・`EXT_CPUID`・`MP_STATE`・`SIGNAL_MSI`・`IRQ_ROUTING`・`DEBUGREGS`・`XSAVE`・`VCPU_EVENTS`・`X86_ROBUST_SINGLESTEP`・`MCE`・`ADJUST_CLOCK`・`SET_IDENTITY_MAP_ADDR` | `GET_SUPPORTED_CPUID`・`GET_MSR_INDEX_LIST`・`GET_MSR_FEATURE_INDEX_LIST`・`GET_MSRS`(S)・`SET_TSS_ADDR`・`SET_IDENTITY_MAP_ADDR`・`SET_CPUID2`・`GET/SET_REGS/SREGS/FPU/XSAVE/XCRS/MSRS/DEBUGREGS/VCPU_EVENTS/MP_STATE/LAPIC`・`X86_SETUP_MCE`・`X86_GET_MCE_CAP_SUPPORTED`・`INTERRUPT`・`NMI`・`GET/SET_CLOCK`・`GET_TSC_KHZ`・`CREATE_IRQCHIP`/`SPLIT_IRQCHIP`・`CREATE_PIT2`・`GET/SET_PIT2`・`GET/SET_IRQCHIP`・`ENABLE_CAP`（任意）・`GET_DIRTY_LOG`（display・migration）・`SET_GUEST_DEBUG`（gdb） |
| Firecracker（x86_64 `DEFAULT_CAPABILITIES`、14 個） | `IRQCHIP`・`IOEVENTFD`・`IRQFD`・`USER_MEMORY`・`SET_TSS_ADDR`・`PIT2`・`PIT_STATE2`・`ADJUST_CLOCK`・`DEBUGREGS`・`MP_STATE`・`VCPU_EVENTS`・`XCRS`・`XSAVE`・`EXT_CPUID` | `CREATE_IRQCHIP`・`CREATE_PIT2`・`SET_TSS_ADDR`・`SET_GSI_ROUTING`（IRQCHIP+MSI）・`IRQFD`・`IOEVENTFD`・`GET_SUPPORTED_CPUID`・`SET_CPUID2`・`GET/SET_REGS/SREGS/FPU/XSAVE(2)/XCRS/MSRS/DEBUGREGS/VCPU_EVENTS/MP_STATE/LAPIC/IRQCHIP/PIT2/CLOCK`・`GET/SET_TSC_KHZ`・`KVMCLOCK_CTRL`・`GET_DIRTY_LOG`・`RUN`。exit は IoIn/IoOut/MmioRead/MmioWrite/Hlt/Shutdown/FailEntry/InternalError/SystemEvent/Debug |
| cloud-hypervisor（x86_64 `check_required_kvm_extensions`、18 個） | `ADJUST_CLOCK`・`EXT_CPUID`・`GET_TSC_KHZ`・`IMMEDIATE_EXIT`・`IOEVENTFD`・`IRQCHIP`・`IRQFD`・`IRQ_ROUTING`・`MP_STATE`・`SET_IDENTITY_MAP_ADDR`・`SET_TSS_ADDR`・`SPLIT_IRQCHIP`・`TSC_DEADLINE_TIMER`・`USER_MEMORY`・`USER_NMI`・`VCPU_EVENTS`・`XSAVE`（+ `DEVICE_CTRL`・`ENABLE_CAP`・`SET_GUEST_DEBUG` は「cap 無しでも動く」前提） | 上に加え `ENABLE_CAP(SPLIT_IRQCHIP, X2APIC_API)`・`SET_USER_MEMORY_REGION2`（memfd）・`TRANSLATE`・`NMI`・`SET_GUEST_DEBUG`。exit は IoIn/IoOut/MmioRead/MmioWrite/Hlt/Shutdown/SystemEvent/Debug/Hypercall/IoapicEoi/MemoryFault(C)/Hyperv(C) |

**結論**: M3（in-kernel irqchip + split + PIT + TSC deadline + socket の ioeventfd/irqfd）で 3 つの VMM の必須の集合を全て満たす。
`KVM_CAP_TSC_DEADLINE_TIMER` と `SPLIT_IRQCHIP` は cloud-hypervisor だけ、PIT は firecracker だけ、`ROBUST_SINGLESTEP`・`MCE`・`SIGNAL_MSI`・`IRQ_ROUTING` は QEMU だけが要る。

### 5.2 QEMU の起動に要る guest 側の道具

SeaBIOS（QEMU に同梱）または OVMF、Linux の kernel + initramfs（`-kernel`/`-initrd` で fw_cfg 経由。SeaBIOS の linuxboot の option ROM が fw_cfg の PIO を
叩く）、serial（PIO 0x3f8 = `KVM_EXIT_IO`）、virtio-blk/net（MMIO の BAR = `KVM_EXIT_MMIO`、modern virtio-pci は BAR の MMIO 経由なので **emulator が
M2 の前提**）、`-display none`（dirty log 不要）。

### 5.3 最初の到達点の案

| 到達点 | 内容 | 受け入れ | 要る Phase |
| --- | --- | --- | --- |
| **M0** | HAL の VMX の primitive だけで、kernel の self test が VMXON → 手作りの VMCS で 16 bit の guest（`HLT`）を走らせ VM exit を観測する | 結果は sysctl で読む（serial log で判定しない）。zedBSD（QEMU nested）で確認 | p002・p003 |
| **M1** | 自作の最小 VMM `kvm-smoke`（`plan/ws082/tests/`、C、300 行程度: `/dev/kvm` → `CREATE_VM` → memslot → `CREATE_VCPU`（id 0 と 1）→ mmap → `SET_SREGS`（real mode）→ `SET_REGS` → `RUN` の loop。guest は `OUT 0x3f8` で "Hello" を出して `HLT`。加えて `CHECK_EXTENSION` で cap ≠ 0 の問い合わせ、SA_RESTART の handler からの kick で `RUN` が EINTR を返すこと、`-smp 4` の zedBSD で走らせて CPU 間の移動を踏むこと） | zedBSD の guest（SSH、`guest.sh run`）で `kvm-smoke` が exit の列（IO × 5、HLT、EINTR）を stdout に出す。host の QEMU（nested）で確認。実機は未実施と書く | p002b・p004a・p004b |
| **M2** | patch した QEMU `-accel kvm -machine kernel-irqchip=off -device virtio-blk-pci,ioeventfd=off` で Linux（小さい kernel + initramfs、`console=ttyS0`）が shell まで起動 | zedBSD の guest の中で L2 の QEMU を `-serial stdio` で起動し、L1 の SSH（`guest.sh run`）から L2 の console に command を送って応答を読む**対話**で判定（L2 の serial の file を読んで判定しない） | p004〜p006 + QEMU の移植の WS |
| **M3** | QEMU の既定（kernel-irqchip=on、`-smp 2`、virtio-blk/net の ioeventfd/irqfd = socket）で同じ guest が起動し、virtio-net で ping が通る | 同上 + ping | p007・p008a・p008b |
| M4 | cloud-hypervisor の必須の集合（split irqchip、TSC deadline）と firecracker の集合を `kvm-smoke --caps` の検査で満たす（実行は Rust の toolchain の WS の後） | `kvm-smoke --caps` が 3 つの集合を全て 1 で報告 | p009 |

**推奨: M1 → M2 → M3 の順**。M1 は emulator も irqchip も要らず、ABI の修正（p002b）と HAL の差分の承認の直後に着手できる。M2 で emulator（MMIO）が入る。

## 6. Phase の分け方（案）と試験

大きさは行数（C、試験を含まず）の目安。risk は 高/中/低。受け入れは各 Phase の phase.md に写す。大きすぎた p004・p008 は分けた（レビュー）。

| Phase | 内容 | 大きさ | risk | 依存 | 受け入れ |
| --- | --- | --- | --- | --- | --- |
| p001 | この検討 | — | — | — | study.md（本書） |
| p002 | 詳細の設計: hal.h の差分（H1〜H8、B-1 の形）を `plan/ws082/proposed/hal-virt.diff` として plan に置く（承認まで適用しない）、`include/uapi/kvm.h` の草案（Zlib、§9）、emulator の命令の subset の表、EPT・memslot の世代・socket の hook の詳細、kbench の pipe の数字、BSP の CR0 の値（gdbstub） | 文書 | 中（承認の単位） | p001 | ユーザーが hal-virt.diff を承認、または差し戻し |
| p002b | ioctl の ABI（K1・K2）: `ioctl_value` op、restart しない EINTR、libc の `KVMIO` の引数。host 試験と guest の試験（正の戻り値、`_IO` の引数、SA_RESTART） | 300 | 低 | p001 | 既存の ioctl の回帰（audio・gpu・tty の host 試験）、boot test |
| p003 | HAL: VMX の primitive（H1〜H8。CR0.NE、reset/panic の VMXOFF、CPU 間の VMCS の clear、host の field の書き直し、`HOST_FS_BASE`）と kernel の self test（M0）。SVM は含まない | 2,000〜2,500 | 高（nested でしか試せない。VMXON と HAL の SMP/NMI/reset の相互作用） | p002 の承認 | M0。`make -j16`（warning 0）。boot test が KVM 無しの config で変わらない。kbench に退行が無い |
| p004a | `/dev/kvm` の core: cdev（K3 の権限）・VM/vCPU の pseudo file・mmap・memslot（anonymous だけ、K4 の上限、K5）+ 塊で wire + EPT（4 KiB）+ 世代と CPU 間の flush・`KVM_RUN` の loop（IRQ を切った区間の kick の確認、CPU の移動の追従）・IO/HLT/CPUID | 2,500 | 高（並行制御） | p002b・p003 | `-smp 4` で slot の削除と vCPU の移動の stress で hang・panic 無し |
| p004b | MSR(最小)/CR/regs/sregs/cpuid・immediate_exit・EINTR・`kvm-smoke` | 1,500 | 低 | p004a | M1（QEMU nested） |
| p005 | 命令の decoder/emulator（MMIO の subset）+ guest の page walker + string PIO + `KVM_TRANSLATE`。host 試験（decode の表、数千の encoding、fuzz） | 3,000〜4,000 | 高（正しさ、攻撃面） | p004b | host 試験 PASS。`kvm-smoke` の 32/64 bit の guest が MMIO の read/write の exit を出す |
| p006 | userspace irqchip の割り込み・vCPU の状態: `INTERRUPT`・NMI・interrupt window・`VCPU_EVENTS`・`MP_STATE`・FPU/XSAVE(FX)/XCRS・DEBUGREGS・MCE の最小・`GET/SET_CLOCK` の stub・TSC khz・`SET_SIGNAL_MASK`・sync regs・`SET_GSI_ROUTING` の表・`KVM_CAP_*` の M2 の集合・QEMU の patch の案 | 2,000〜2,500 | 中 | p005、QEMU の移植の WS | M2 |
| p007 | unix socket の通知（B、K6）: `KVM_IOEVENTFD`・`KVM_IRQFD`（resample を含む）、予約した packet と解放の callback、消費者 hook、検査（§4.2）、QEMU の EventNotifier の patch、**zedBSD の guest での**試験（受信の上限に当てた合体、peer の close、rights の拒否） | 1,000 | 中（poll の設計との相性） | p006（irqfd の注入先は p008a の LAPIC。p007 は ioeventfd を先に、irqfd は p008a と同時に） | virtio の `ioeventfd=on` で M2 の guest が起動 |
| p008a | in-kernel LAPIC（xAPIC/x2APIC、timer、TSC-deadline、IPI、INIT/SIPI）・halt の眠り・kick・`GET/SET_LAPIC`・irqfd の注入 | 2,500 | 高（量、SMP の競合） | p007 | `-smp 2` の guest が LAPIC timer で tick する |
| p008b | IOAPIC・PIC・PIT・routing・MSI・`IRQ_LINE`・`GET/SET_IRQCHIP/PIT2`・`SET_BOOT_CPU_ID`・coalesced MMIO ring | 2,000 | 中 | p008a | M3（`-smp 2`、ping） |
| p009 | split irqchip・`IOAPIC_EOI` の exit・`X2APIC_API`・`X86_DISABLE_EXITS`・kvmclock（pvclock、`GET/SET_CLOCK`、`KVMCLOCK_CTRL`、pv の CPUID）・cap の M3/M4 の集合 | 1,500 | 中 | p008b | M4（`kvm-smoke --caps`） |
| p010 | dirty logging（`GET_DIRTY_LOG`・`CLEAR_DIRTY_LOG`・`MANUAL_DIRTY_LOG_PROTECT2`、EPT の write protect）と `-vga std` の表示、`READONLY_MEM`、shared（shm）の memslot | 1,000 | 低 | p008b | QEMU の VNC/`-display` で guest の画面が更新される（`boot-test.sh` の流儀で PNG） |
| p011 | guest debug（`SET_GUEST_DEBUG`、`KVM_EXIT_DEBUG`、gdbstub）、`X86_USER_SPACE_MSR`・`MSR_FILTER`、`EXCEPTION_PAYLOAD`・triple fault event、`EXIT_ON_EMULATION_FAILURE`、TPR reporting/vapic | 1,000 | 低 | p008b | QEMU の gdbstub で L2 の Linux に breakpoint |
| p012 | 性能: APICv/posted interrupt（H9）・VPID・TSC scaling・halt polling・`PRE_FAULT_MEMORY`・lazy の EPT・2 MiB の EPT（物理連続の backing が要る → 別途）、測定（exit の往復、kick の遅延、他 process が poll している状態） | 1,500 | 中（実機でしか測れない） | p009 | 実機（Latitude 5330）で exit/entry と irqfd の遅延を測って §4.4 の見積もりと比べる |
| p013 | SVM の backend（H12、VMCB・NPT）。QEMU TCG の `+svm` で試験、AMD の実機は無し | 1,500 | 中 | p003 | TCG で M1 |
| p014 | dirty ring・`GET_STATS`（代替）・`ONE_REG`・`VCPU_ATTRIBUTES`・bus lock/notify（hardware があれば）・HAL の one-shot timer（H10）による guest timer の精度 | 1,000 | 低 | p010 | — |
| p015 | 規約の全文の適合（`plan/coding-style.md`、`style-check.py`）、host 試験の整理、`plan/tools/` への移動 | — | 低 | 全部 | AGENTS.md の conformance Phase |
| 別 WS | QEMU の移植と package（host OS の追加、glib・pixman・meson のクロスビルド、patch の管理、ライセンスの監査）。Rust の target と firecracker/cloud-hypervisor。aarch64 の KVM の検討（§8）。XSAVE の HAL（H11）。poll の file ごとの wait queue | — | — | — | — |

### 6.1 試験の計画

- **host 試験**（guest を起動しない）: emulator の decoder（`plan/ws082/tests/emulator/`、host の clang で kernel の断片を compile、encoding の表を
  照合、fuzz）、EPT の table の構築（GPA → HPA の擬似の pin）、LAPIC/IOAPIC/PIT の model（register の読み書きと timer の満了の順序）、ioctl の
  `ioctl_value` の dispatcher。`plan/tools/driver-fragments/prepare.py` の形で断片を切り出す。**socket の通知（packet の予約・合体・上限）は Linux の
  socketpair では試せない**ので zedBSD の guest で試す（次項）。
- **QEMU（nested VMX）**: host の `-accel kvm -cpu host`（`kvm_intel nested=Y` を確認済み、`guest.py` の既定、4 CPU）の zedBSD の中で M0〜M3。
  L2 の guest の memory は 512 MiB〜1 GiB。判定は `kvm-smoke` の stdout（SSH の `guest.sh run`）と、L2 の console との対話（§5.3）。zedBSD 自身の
  起動の判定は `boot-test.sh` だけ。QEMU の serial log を読んで判定しない。
- **実機（VFIO ではなく素の実機）**: Latitude 5330（Alder Lake）。KVM 入りの image を USB で起動し、`kvm-smoke` と QEMU の M2/M3 を実機で再現。
  性能（p012）は実機だけ。VFIO の device passthrough は範囲外（IOMMU 無し）。実機の証拠と QEMU の証拠は分けて書く。
- **回帰**: KVM 無しの config の boot test（framebuffer の login）。HAL の差分の Phase では amd64 の boot test と `plan/tools/kbench`（syscall・fork の
  退行が無いこと）。p002b では既存の ioctl を使う host 試験（audio・gpu・tty）。

## 7. 敵対的レビュー（自問と答え。design-reviewer の指摘は §12）

| 攻撃 | 答え・残る懸念 |
| --- | --- |
| 「B は ioctl の形を保つが、eventfd の counter の意味（何回 write されたか）が失われる」 | 数える利用者は無い（virtio の kick は level の意味）。`sequence` で合体の数は分かる。counter の意味が要る利用は C と明記 |
| 「datagram の確保が vCPU の exit の path に乗り、VM exit の遅延が増える」 | 予約した packet を積むだけ（確保しない）。+0.3 µs の見積もり。p012 で測る |
| 「`poll_notify` が system 全体の全 poller を起こす」 | 事実（`src/kern/poll.c`）。KVM の外の問題として Future Work に出す。p012 で他 process が poll している状態で測る |
| 「emulator を独自に書くのは無理がある。Linux の emulator を見れば早い」 | GPL なので見ない（§9）。subset に絞れば 3〜4k 行。host 試験で encoding を網羅し fuzz する。未対応は `INTERNAL_ERROR` で止まる。**risk 高のまま** |
| 「unrestricted guest を必須にすると古い CPU で動かない」 | 対象機（Alder Lake・Skylake-SP）は持つ。無い CPU は `/dev/kvm` の open で ENXIO。real mode の emulation は行わない（§10.3） |
| 「HAL の差分が 8 個もあり、承認の単位が大きい」 | p002 で 1 つの差分（hal.h の節）にまとめ、SVM・APICv・timer・XSAVE（H9〜H12）は後の別の差分にする。案 C（承認なし）は層を壊すので採らない |
| 「memslot の eager の wire は 8 GiB の guest で数秒かかり、host の memory を全部取る」 | 事実。lazy（p012）で改善。commit の会計（policy 10）は変えない。K4 の上限で他の process を守る。8 GiB の host の中では小さい guest で試す |
| 「VMM が memslot の user memory を `munmap`/`mprotect` したら EPT が dangling」 | `munmap` は wire で EBUSY（確認済み）。`mprotect` は K5 で拒む |
| 「他の CPU に古い EPT の translation が残ったまま page を解放する」 | §3.4 の世代と全 vCPU の flush の待ち。p004a の stress で確かめる |
| 「vCPU thread が別の CPU に盗まれた瞬間に VMCS と host の field が古い」 | §3.2。移動の追従は p003/p004a に置く（p013 の後回しを撤回） |
| 「nested でしか試せない期間が長い。nested の VMX は L0 の KVM の癖を映す」 | 事実。M1 から実機でも走らせる（USB の image）。HAL の差分の Phase は実機の確認を受け入れに含めない（QEMU の証拠と分けて書く） |
| 「ioctl の数値が Linux と違うので Linux の binary は動かない」 | 目標は source の互換。Linux の ELF を動かす計画は無い（master の範囲外「Linux の kernel ABI の互換」） |
| 「1 ms の tick では guest の timer が粗い」 | vCPU が走る間は preemption timer で正確。halt 中だけ 1 ms。H10 で解消できる |
| 「`KVM_CAP_IOEVENTFD` 等を M2 で 1 と返しながら ioctl を実装しないのは嘘」 | VMM が cap 0 で起動を拒むための便法。全部を §10.8 に列挙してユーザーの判断を仰ぐ。ioctl は p007 まで ENOTSUP |
| 「誰でも `/dev/kvm` を開けて全 CPU で VMXON し、物理 memory を無制限に wire し、kernel の emulator と irqchip を guest に攻撃させられる」 | K3（0660 root:kvm）・K4（上限）・decoder の fuzz（p005）。guest → host の攻撃面は emulator・irqchip・EPT の walker の 3 つで、いずれも入力の検査を受け入れに含める |
| 「Firecracker・cloud-hypervisor は動かせないのに集合を満たす意味があるか」 | API の完成度の基準として使う。実行は Rust の toolchain の後 |
| 「aarch64 が主対象なのに x86 だけ」 | ws.md の範囲が VMX/SVM。aarch64 は §8 に要点、別の検討 |
| 「zedBSD の kernel は 16 MiB の上限に近い」 | `CONFIG_DRIVER_KVM` 既定 n。KVM の image は i915 と同時に有効にしない構成から始める |
| 「VMM の fork で 8 GiB が複製される」 | 事実（§3.4）。QEMU の patch で `posix_spawn`。制限として文書に書く。`MADV_DONTFORK` の追加は Future Work |

## 8. aarch64 について（要点だけ、別の検討へ）

Pi 4（Cortex-A72）は EL2 を持ち、firmware（armstub8）は kernel を EL2 で起動する。zedBSD の arm64 の HAL がどの EL で動いているかは未確認
（`src/hal/arm64/locore.S` を p002 で読む）。KVM 風の hypervisor には **EL2 の code（nVHE の形: EL2 の小さな stub と EL1 の kernel の分離）か、A72 に無い
VHE** が要る。stage-2 page table、GICv2（Pi 4 は GIC-400、virtualization の interface は GICH/GICV）、generic timer の virtual timer、
`KVM_ARM_VCPU_INIT`・`KVM_GET/SET_ONE_REG`・`KVM_GET_REG_LIST`・`KVM_CREATE_DEVICE(VGIC_V2)`（device fd が A になる）・`KVM_ARM_PREFERRED_TARGET`。
QEMU の raspi4b は EL2 を emulate しない（`virt` machine の `virtualization=on` で TCG は EL2 を emulate できる）。HAL の責務は x86 より大きい
（例外の level の分離）。本検討の結論は変わらない: A/B/C の分類は同じで、B の socket の設計と §3.0 の ABI の修正は arch に依らない。

## 9. ライセンスと転記の扱い

- **参照した Linux の UAPI header**（`/usr/include/linux/kvm.h`、`asm/kvm.h`）: SPDX `GPL-2.0 WITH Linux-syscall-note`。ioctl の名前・番号・struct の
  layout・cap の番号は **interface の事実**として使い、zedBSD の `include/uapi/kvm.h` は Zlib で独自に書く（名前と layout は VMM の source の互換のために
  同じ。この形は `include/uapi/input.h`（Linux evdev の名前と layout、Zlib、WS006）と同じ前例）。comment・説明文は写さない。
- **Linux の KVM の実装（`arch/x86/kvm/`、`virt/kvm/`）は読まない・写さない**（GPL-2.0-only。policy 2.1「外部の実装を base system に取り込まない」）。
  設計の根拠は Intel SDM Vol.3C（VMX）・AMD APM Vol.2（SVM）・KVM の API 文書（`Documentation/virt/kvm/api.rst`。仕様として読む。転記しない）。
- **bhyve（FreeBSD、BSD-2-Clause）・NVMM（NetBSD、BSD-2-Clause）・HAXM（Intel、BSD-3）** は寛容なライセンスの hypervisor で、読んで理解の参考にできる。
  **code の取り込みは policy 2.1 に反するので行わない**。取り込みを許すかはユーザーの判断（§10.4）。
- **QEMU**（GPL-2.0-or-later）は `userland/packages/` の外部 package の境界で扱い、patch も package の patch として置く（`plan/ws032/provenance.md` の監査）。
  Firecracker・cloud-hypervisor（Apache-2.0）は Rust の toolchain の後。
- 本書の VMM の必須の集合は各 VMM の source を読んで**事実を要約**したもので、code の転記は無い。

## 10. 判断が要る点（ユーザーへ）

1. **進め方**: M1 → M2 → M3 の順と、p002（HAL の差分の提案）と p002b（ioctl の ABI の修正）から始めることの可否。実装の Phase は「空いた枠で」の
   位置づけのまま。
2. **HAL の置き方**: 案 B（最小の primitive を HAL、KVM の意味は `src/drivers/virt/kvm/`、driver は `include/kern/virt.h` の wrapper を通す）でよいか。
   header は hal.h の節（B-1、推奨）か別の `include/hal/virt.h`（B-2）か。**EPT の table を driver が作る**ことは hal.h の「kernel は page table に触れない」の
   趣旨に反しないか（本書の解釈: EPT は host の page table ではない）。
3. **hardware の要件**: unrestricted guest と EPT を必須にし、real mode の emulation と shadow paging は行わない。
4. **寛容なライセンスの hypervisor（bhyve・NVMM）の code の扱い**: 読むだけ（既定）か、部分的な取り込みを許すか。
5. **QEMU の移植の WS** を別に立てること（host OS の追加、glib・pixman・meson のクロスビルド）。KVM の M2 の受け入れがこれに依存する。
6. **Rust の target**（firecracker・cloud-hypervisor の実行）は toolchain の変更なので、別の WS と main の許可。今回は集合を満たすだけでよいか。
7. **XSAVE の HAL の変更（H11）** の時期: KVM のためだけなら後回し（guest に AVX を見せない）。libc の ABI（signal frame）に波及する。
8. **実装より先に 1 と返す cap の便法**: M2 で `IOEVENTFD`・`IOEVENTFD_ANY_LENGTH`（実装 p007）、`SET_GUEST_DEBUG`・`X86_ROBUST_SINGLESTEP`（実装 p011）、
   `IRQ_ROUTING`・`SIGNAL_MSI`（実装 p008b。M2 の QEMU は呼ばない）。許すか、M2 の QEMU の patch で必須の検査を外すか。
9. **ioctl の ABI の変更（K1・K2）**: kernel の `file_ops` の任意の op と libc の `ioctl()` の例外は system の ABI の変更（HAL ではない）。承認の要否。
10. **`/dev/kvm` の権限の形（K3）**: devfs の code に固定（既存の例外と同じ）か、devfs の権限の仕組みを作るか。`kvm` の group の追加。
11. **CR0.NE の設定（H2）**: host の x87 のエラーの報告が変わる（AP は今 NE=0）。KVM と無関係に host の挙動に影響しうる点。

## 11. 残課題

- kbench の pipe の往復の数字（§4.4 の見積もりの根拠の補強）。
- BSP の CR0.NE の値（firmware 次第）と AP との差の確認（gdbstub）。
- arm64 の HAL の起動 EL の確認（§8）。
- QEMU の `kernel-irqchip=off` の道が `KVM_CAP_IRQFD` を要らないことの再確認（`kvm_irqchip_create` の呼び出し条件は読んだが、その他の道は未確認）。
- p002 の成果物: `plan/ws082/proposed/hal-virt.diff`、`include/uapi/kvm.h` の草案、emulator の命令の表、cap の表の機械的な生成。

## 12. design-reviewer のレビュー（2026-09-28）と反映

design-reviewer（subagent）に本書の初版の敵対的レビューを依頼し、24 件の指摘を受けた。本書の作成者が根拠の file を読み直して確かめたうえで反映した。

| # | 指摘（重大度） | 確認 | 反映 |
| --- | --- | --- | --- |
| 1 | ioctl は正の値を返せない（高） | `file.c`・`syscall.c` で確認 | §3.0 K1、p002b |
| 2 | libc が `_IO` の引数を捨てる（高） | `posix.c` の `ioctl_has_argument` で確認 | §3.0 K2、M1 の受け入れ |
| 3 | datagram が system 全体の packet の pool（32）を使い切る（高） | `packet-buf.h`・`socket.h` で確認 | §4.3 の予約した packet と解放の callback |
| 4 | 他 CPU の EPT の無効化と memslot の並行制御が無い（高） | 設計の欠落 | §3.4 の世代と全 vCPU の flush、p004a の stress |
| 5 | vCPU の CPU 間の移動は M1 から起きる、固定は不可、IRQ 無効での IPI 待ちは deadlock（高〜中） | `sched.c` の work stealing・tick の `sched_yield` で確認 | §3.2・§3.5・H4、p013 → p003/p004a |
| 6 | `HOST_FS_BASE`（user の TLS）と guest の `KERNEL_GS_BASE`（中〜高） | `task.c` の FS_BASE の保存で確認 | §3.2・H6 |
| 7 | VMXON の CR0.NE（中） | `ap-trampoline.S`・`asm.c` で確認 | §3.1・H2、§10.11 |
| 8 | `KVM_RUN` の EINTR が restart される（中） | `syscall_restartable` で確認 | §3.0 K1 |
| 9 | kick の確認と IRQ の disable の間の競合（中） | 設計の欠落 | §3.3（IRQ を切った後に確かめる、`vcpu->mode`） |
| 10 | `/dev/kvm` の権限（devfs 0666）と wire の上限（中） | `devfs.c` で確認 | K3・K4、§7、§10.10 |
| 11 | socket の検査（bind 済みの拒否、非 blocking、SIGPIPE）（中） | `unix-socket.c` で確認 | §4.2・§4.3 |
| 12 | fd の循環と hook の寿命、deassign の意味（中） | 設計の欠落 | §4.2 |
| 13 | fork は pin した page を即座に複製、`vm_object_page_pin` は resize から守らない、munmap は既に EBUSY、mprotect は見ない（中） | `vmspace.c`・`vm-object.h` で確認 | §3.4（anonymous だけ、K5、fork の制限） |
| 14 | HAL の境界（driver が HAL を直接呼ぶ、header の置き方の根拠、EPT の所有）（中） | `device-io.h` の規則 | §3.10 の `include/kern/virt.h` と B-1/B-2、§10.2 |
| 15 | poll の全員起こしは system 全体（中） | `poll.c` で確認 | §4.4 |
| 16 | 試験の計画（L2 の serial の file、Linux の socketpair での試験）（中） | 規則の解釈 | §5.3・§6.1（対話で判定、socket の試験は guest で） |
| 17 | QEMU の移植の大きさ（中） | 推測を修正 | §4.5 |
| 18 | VMX root の reboot・panic・NMI（低〜中） | 設計の欠落 | §3.1・H2 |
| 19 | 実装より先に 1 と返す cap の一覧と `TSC_CONTROL` の矛盾（低〜中） | 表の不整合 | §2.13・§10.8、`TSC_CONTROL` を L に |
| 20 | compile flag の出典（amd64 は `-mgeneral-regs-only`）と `hal_rtc_read_counter` の契約（低） | `platform/amd64/vmunix.mk`・`hal.h` で確認 | §3.2・§3.7 |
| 21 | 数と記述の不整合（低） | 数え直し | §0・§1・§2.13・付録 A |
| 22 | Phase の大きさと ABI・権限の Phase の欠落（低） | — | §6（p002b、p004a/b、p008a/b） |
| 23 | pin の配列の大きさと lock の保持（低） | `vmspace.h`・`vmspace.c` | §3.4（塊で pin） |
| 24 | device backed の memslot の memory type（低） | — | §3.4（拒む） |

反映しなかったもの: 無し。指摘 17 の「`kernel-irqchip=off` で `KVM_CAP_IRQFD` が要るか」は `kvm_irqchip_create` の呼び出し条件（`kernel_irqchip_allowed`）
を読んで要らないと判断したが、§11 に再確認を残した。

## 付録 A. 数の内訳

数え方: ioctl は host の `/usr/include/linux/kvm.h` の `#define KVM_* _IO*` の一意な名前（150 個。`KVM_REGISTER/UNREGISTER_COALESCED_MMIO` の
2 行の define を含む）を、§2 の表の名前と機械的に突き合わせた（scratchpad で `comm`。表に無い名前は全て他 arch の 44 個だった）。

- ioctl（150 のうち amd64 に関係する 106）: **A 82**（§2.1〜2.10・2.12 の A の行。`SET_USER_MEMORY_REGION2` と `SET_GSI_ROUTING` は制限つきの A、
  `SET/GET/HAS_DEVICE_ATTR` は vCPU の TSC attribute だけ A だが名前としては C に数えた）、**B 2**（`KVM_IOEVENTFD`・`KVM_IRQFD`）、
  **C 22**（`GET_STATS_FD`・`SMI`・`GET/SET_NESTED_STATE`・`GET_SUPPORTED_HV_CPUID`・`HYPERV_EVENTFD`・`XEN_HVM_CONFIG`・`XEN_HVM_GET/SET_ATTR`・
  `XEN_VCPU_GET/SET_ATTR`・`XEN_HVM_EVTCHN_SEND`・`MEMORY_ENCRYPT_OP`・`MEMORY_ENCRYPT_REG/UNREG_REGION`・`SET_PMU_EVENT_FILTER`・`CREATE_DEVICE`・
  `SET/GET/HAS_DEVICE_ATTR`・`SET_MEMORY_ATTRIBUTES`・`CREATE_GUEST_MEMFD`）。他 arch の 44（s390 21、ppc 15、arm 8。`KVM_GET_REG_LIST` は arm/riscv）は N/A。
- kvm_run: exit 24（A 19 / C 5）、field 12（A 10 / C 2）。
- capability（233 のうち amd64 に関係する 137）: **A 90**（M1 17・M2 25・M3 17・L 31）**/ B 5 / C 42**（表の C の行。`MULTI_ADDRESS_SPACE` は 1 を返すが
  SMM 無しなので C に数えた）。他 arch の約 95 は N/A（合計が 232 で 1 つ合わないのは名前の分類の取りこぼし。p002 で表を header から機械的に作り直す）。
