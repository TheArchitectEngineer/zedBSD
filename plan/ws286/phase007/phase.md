<!-- awesome-plan project=zedbsd record=ws286-p007 -->

# ws286-p007 userland の runtime（動的 PIE: sysroot・crt・libc の arch の file・rtld の ARM 版、abitest で ABI PASS）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §2.3・§3.9・§7.2〜§7.4・§8 の p007 の行。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p007`、branch `agent/p2-zybo-p007`、基点 main fe7c8aad8）。
- 判断と許可: U3（QEMU zynq と実機は生きた serial で判定）、U4（担当が板を JTAG で直に操作）。toolchain: Q1（main）の許可 2026-10-23「you MAY edit the sysroot rules (the sysroot .mk and its manifests) to add the armv7 entries and builtin manifest … and build that sysroot only inside your worktree's build/」。共有の LLVM は作り直していない（`build/llvm`・`llvm-source`・`llvm-build`・`NoctLang` は main の物への symlink、読むだけ）。HAL の API は変えていない。
- Status: cleared（2026-10-23 Q1: 実機の ZYBO（JTAG、--initrd abi.img、init=/abitest）で ABI 1〜8 と ABI PASS、T1 の QEMU zynq（exit 0、ABI 1〜8・ABI PASS）と virt（boot-test PASS、PNG build/review/ws286-p007-virt.png に ABI PASS）、zybo・virt・amd64・rpi4 の build warning 0。merge 771026557。abitest の link の -z undefs と --provider の確かめは Q1 が承認、login の program の扱いは p010）

## 受け入れの条件（design §8 の p007、§7.2〜§7.4）

1. zybo・armv7-virt・amd64・rpi4 の build が warning 0。
2. 実機（JTAG、UART、`--initrd abi.img`、`init=/abitest`）: `ABI PASS`。
3. QEMU xilinx-zynq-a9（T1、serial）: `ABI PASS`。
4. QEMU virt（T1、boot-test の PNG）: `ABI PASS`。

## 作った物（1062ade64、3e14c7646、e84151a49）

- sysroot（`toolchain/llvm/sysroot.mk`、Q1 の許可の範囲）: `ZEDBSD_BUILD_SYSROOT` の armv7 の呼び出し（`armv7-unknown-zedbsd-eabihf`、`ZEDBSD_SYSROOT_ARMV7_CODE_FLAGS` = cortex-a9・ARM・hard float・neon-vfpv3・`-mtp=cp15`・`-mno-unaligned-access`・`-Wno-unaligned-access`（kernel と同じ理由: packed の `struct stat`））、`ZEDBSD_SYSROOT_ARMV7_LIBC_SOURCES`（setjmp）、LLVM の builtin の ARM の manifest（`arm/aeabi_{idivmod,uidivmod,ldivmod,uldivmod,memcpy,memmove,memset,memcmp}.S`・`arm/aeabi_div0.c`・汎用の `fix{,uns}{df,sf}di.c`・`float{,un}di{df,sf}.c`）、builtin だけの flag の 16 番目の引数（`-DCOMPILER_RT_ARMHF_TARGET`: 普通の関数は hard float、`__aeabi_*` は AAPCS の基本の呼び出し）、archive に `.S.o` も入れる、`platform/armv7/user.ld` を `usr/lib/zedbsd/armv7/` へ。出来る物は worktree の `build/armv7/sysroot`。
- crt・libc の arch の file（新規）: `src/libc/crt/crt0-armv7.S`（static: `_start`・`__syscall6`・`__signal_restorer`）、`crt1-armv7.S`（動的）、`userland/base/libc/syscall-armv7.S`（r7 = 番号、r0〜r5、6 つ目までの引数の 3 つは stack から）、`src/libc/setjmp-armv7.S`（r4〜r11・sp・lr・d8〜d15、26 word）、`platform/armv7/user.ld`。枝: `setjmp.h`・`setjmp.c`（`__arm__`）、`static-tls.c`（TPIDRURO）、`posix.c`（`uname` の machine `armv7`）、`pthread.c`（`yield`）。
- `include/libc/limits.h`: char が unsigned の時の `CHAR_MAX` を `255`（int）に。`255U` では 32 bit で `long <= CHAR_MAX` が符号の比較になり `locale.c` が -Werror で止まった（C の規格では CHAR_MAX は整数の格上げの後の型 = int）。`UCHAR_MAX` は変えていない。Q1 ACK（2026-10-23「standard-conforming」、rpi4 の warning 0 の確認を報告に）。
- rtld の ARM 版: `src/rtld/rtld.c`（`RTLD_MACHINE EM_ARM`・`RTLD_RELATIVE R_ARM_RELATIVE`、NONE、`apply_value` の armv7 の枝: REL の暗黙の addend、RELATIVE・ABS32・GLOB_DAT・JUMP_SLOT・TLS_DTPMOD32・TLS_DTPOFF32、TLS_TPOFF32 は拒む、`thread_pointer` は TPIDRURO）、`src/rtld/elf.h`（`R_ARM_*`、Values referenced: LLVM 23.1 `ELFRelocs/ARM.def`）、`src/rtld/entry-armv7.S`（`_rtld_start`・`rtld_syscall6`）。variant I の動的 TLS は riscv64 と同じ（静的 TLS の配置は無し、module 1 は実行 file）。
- kernel: `include/kern/elf.h` に `EF_ARM_ABI_FLOAT_HARD`、`src/kern/elf.c` は armv7 で hard float の印の無い ELF32 を `ENOEXEC`（`EM_ARM` は p004 で既に有った）。
- `platform/armv7/userland.mk`（新規、`platform/armv7/Makefile` から include）: riscv64 の形の `libc.so`・`ld.so`。違い: compiler-runtime.c（soft float の演算）は入れない（FPU が演算し、`__fixdfdi` などが builtin と重複する）、run-time ABI の helper は LLVM の source から `-fPIC` で `libbuiltins-pic.a` に作り、libc.so は使う member を export、ld.so は `--exclude-libs ALL` で自分の物にする（ld.so に入ったのは `__aeabi_uidiv`・`__aeabi_uidivmod` だけ、local）。`abitest` と `abi.img`（`make … build/<board>/abi.img`）。
- abitest のリンクは `-z undefs`: 実行 file の `__thread` の `__tls_get_addr` は ld.so にしか無く、libc.so は定義しない（arm64 の TLSDESC・amd64 の LE では起きない、riscv64 でも実行 file に `__thread` を持つ program はまだ無い）。代わりに `tools/build/check-dynamic-elf.py` に `--provider FILE` を足し、未定義の symbol が全て provider の export か loader の private の名前（`PRIVATE_RTLD`）である事を確かめる。同じ file に `armv7` の machine（ELF32、EM_ARM、許す reloc 0・2・17・18・21・22・23、ld.so は 23 だけ）。
- `platform/armv7/tools/make-armv7-root-image.py`（新規）: MBR 1 つの partition（LBA 2048、type 0xA5）に UFS（既定 4 MiB = format の道具の最小）と `/etc/zedbsd-root`、`--dir`・`--file DEST=SRC`。FAT の boot partition は無い: kernel は MBR の partition 1 を boot partition とし、他に印の UFS が無いので partition 1 自身を root にする（`vfs_mount_legacy_root` の最後の道）。abi.img は 5 MiB（design §7.4 の目安 2 MiB を超える: UFS の最小が 4 MiB。JTAG で約 20 s）。WS289 で root を SD に移す時はこの道具を使わない。
- `plan/ws286/tests/abitest.c`（新規、init として動く）: 1 syscall（getpid・write の長さ・close(-1) の EBADF）、2 tls（`__thread` の template の値と 2 つの thread の別の値、errno）、3 fork と waitpid、4 vfp-switch（親子が別の d0〜d31・FPSCR を asm で置き sched_yield 1000 回）、5 vfp-signal（自分に SIGUSR1、handler が全 register と FPSCR を壊す）、6 vfp-alarm（alarm(1) の SIGALRM を asm の loop で待つ: 割り込みの道）、7 exec（FPSCR を置いて execve、新しい image で FPSCR = 0）、8 uname（`armv7`）。最後に `ABI PASS` か `ABI FAIL <番号>`、その後は sleep し続ける（init は終わらない）。

### design との違い

- design §2.3 は libc の static の `__syscall6` を `syscall-armv7.S` に置くと書いたが、tree の形（riscv64・aarch64）どおり static は crt0、動的は `syscall-armv7.S`。
- builtin の変換は汎用の C（design のとおり）。LLVM 23 には `arm/fixdfdi.S` などの最適化版もあるが使わない。
- `softfloat.c`・`compiler-runtime.c` は sysroot の `libzedbsd-compiler-rt` に共通の manifest のまま残る（static の link で使われない）。動的の libc.so には `softfloat.c`（数値の解析が使う）だけ。
- abi.img は 5 MiB（上）。
- `make-armv7-root-image.py` は新規（riscv64 の道具は 8 MiB の FAT を付ける）。

## 確認

### host（2026-10-23 P2）

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk sysroot-armv7` | rc 0、smoke の link（Machine ARM、未定義 0）。libc.o の未定義は `__aeabi_*`（builtins に有る）・`__rtld_*`・`__syscall6`/`__signal_restorer` だけ |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/abi.img`（e84151a49） | rc 0、warning 0。check-dynamic-elf: ld.so（interpreter、R_ARM_RELATIVE 39 だけ）・libc.so（libc、未定義は `__rtld_exports`・`__tls_get_addr`）・abitest（program、`--provider libc.so`）PASS。e_flags 0x5000400（EABI5・hard float）|
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/abi.img`（同） | rc 0、warning 0 |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-amd64 -j12 vmunix`（3e14c7646 の tree） | rc 0、warning 0、kernel include check PASS（553 objects）、amd64 vmunix check PASS |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-rpi4 -j12 vmunix`（同） | rc 0、warning 0、arm64 vmunix check PASS |
| `llvm-readelf -r`（abitest・libc.so） | TLS_TPOFF32・LE の reloc 無し（lld の ARM は GD を relax しない）。実行 file の自分の TLS は lld が module 1・offset を静的に書く |
| `git diff --check` | 出力なし |

### 実機 ZYBO（JTAG、2026-10-23 13:2x〜13:3x P2、U4、board lock を取って。実機の証拠）

- `plan/tools/zybo/jtag-boot.sh build/zybo/vmunix.bin --initrd build/zybo/abi.img --append 'init=/abitest' --timeout 150 --expect '^ABI 8 uname ok' --expect '^ABI PASS'`（e84151a49 の build、vmunix.bin `a823f9e1…`、abi.img `47ec5fb4…`）→ rc 0。abi.img の load 5242880 byte 20.4 s（250.7 KiB/s）。UART の抜粋:

```
uart| zybo: ramdisk ram0 5120 KiB at 4000000
uart| boot: parameters: init=/abitest
uart| vfs: ram0 partition 1 start=00000000:00000800 data=00000000:00000800 blocks=00000000:00002000
uart| boot: starting init /abitest
uart| ABI armv7 user ABI test
uart| ABI write
uart| ABI 1 syscall ok
uart| ABI 2 tls ok
uart| ABI 3 fork ok
uart| ABI 4 vfp-switch ok
uart| ABI 5 vfp-signal ok
uart| ABI 6 vfp-alarm ok
uart| ABI 7 exec ok
uart| ABI machine=armv7
uart| ABI 8 uname ok
uart| ABI PASS
```

- 同じ結果を変数名を直す前の abitest（3e14c7646）でも 1 回（`--expect '^ABI PASS'`、rc 0）。
- 注: 最初の 2 回は `--expect 'ABI PASS'` が HAL の行の `AEABI PASS` に一致して早く終わった（判定に使っていない）。pattern は `^ABI PASS` にする。

### QEMU（T1 に依頼、未実施）

- 下の「T1 への依頼」。

## T1 への依頼（2026-10-23、Q1 経由）

worktree `/home/awe/zedBSD-worktrees/p2-zybo-p007`（e84151a49）。担当が作った image をそのまま使える。作り直す時は `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/abi.img` と `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix build/armv7-virt/abi.img`（build/llvm・llvm-source・llvm-build・NoctLang は main への symlink が要る）。

- A（zynq、serial、U3）: `plan/tools/zybo/qemu-zynq.sh build/zybo/vmunix --initrd build/zybo/abi.img --append 'init=/abitest' --timeout 150 --expect 'boot: starting init /abitest' --expect '^ABI 8 uname ok' --expect '^ABI PASS'` → exit 0 で PASS。`ABI` で始まる行を全部写して返す。
- B（virt、PNG）: `BOOT_MODE=armv7-virt INITRD=build/armv7-virt/abi.img BOOT_APPEND='init=/abitest' BOOT_PATTERN=$'ABI 7 exec ok\nABI 8 uname ok\nABI machine=armv7' plan/tools/boot-test.sh build/armv7-virt/vmunix.bin` → PASS と PNG（画面に `ABI PASS` の行が有る事も PNG で）。`ABI PASS` だけの pattern は HAL の行の `AEABI PASS` に一致するので使わない。
- 失敗の時: `ABI FAIL <番号>` の行と、その前の `ABI` の行（`ABI d<n> expected … seen …` など）を返す（解析は担当）。

## 残り・引き継ぎ

- p010: `platform/armv7/rootfs.mk` と login の program の組。`userland.mk` は今 libc.so・ld.so・abitest だけ（`platform/common/userland-dynamic.mk` の移植可能な library 群は入れていない）。
- 実行 file が `__thread` を持つ時の `-z undefs` + `--provider` の検査は abitest の link だけ。p010 の program の link に同じ形が要るか（`__thread` を持つ program が有るか）は p010 で見る。riscv64 にも同じ穴がある（実行 file の GD TLS の `__tls_get_addr`）。
- 再開点: T1 の A・B の結果と Q1 の判定・merge。次は p010。
