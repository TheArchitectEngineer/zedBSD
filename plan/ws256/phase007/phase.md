<!-- awesome-plan project=zedbsd record=ws256-p007 -->

# ws256-p007 userland の riscv64

Status: cleared（2026-10-19 Q1 の判定、T1-721: riscv64 virt で login・`uname -m` の `riscv64`・dyntest の最後の行 DL:06:PLUGIN-TLS を画面で確認（UART の RX も兼ねる、build/review/t1-721/riscv-login-after.png）、amd64 の既定の image の回帰 PASS（build の warning は外部 package の openssh と host の locale の既存の物）。Noct の riscv64 は proposed/noct-riscv64.diff の案のまま p010 の残りへ（login に不要、Q1 が要る時に適用））

- 親: [WS256](../ws.md)。Queue: q1269 / q1269-i01（承認: 2026-10-19 ユーザー「WS256を実施してください。」）。
- 範囲: [design.md](../design.md) §6 の p007、§4.3。(a) crt0/crt1/setjmp/syscall の .S と user.ld、(b) libc・rtld の枝と relocation、Noct の cmake、`check-dynamic-elf.py`、最小の user program と root.img。
- 受け入れ: build warning 0、host の rtld の確かめ（riscv64 の relocation、TPREL の拒否）、T1: 画面に login の prompt、serial で打った `uname -m` の答え `riscv64`。
- 依存: p008（cleared、T1-720）、p010 (a)（zedbsd9）、p010 (b)（Q1 が [proposed/sysroot-riscv64.diff](../proposed/sysroot-riscv64.diff) を適用、main 536b101d9、`make sysroot-riscv64` rc=0）。

## 作った物・変えた物

### (a) start file・veneer・setjmp・link（commit dab634f16、main dff5c396f）

- `src/libc/crt/crt0-riscv64.S`（static: `_start`、`__syscall6`、`__signal_restorer`）、`crt1-riscv64.S`（PIE: `_start` → `__libc_init` → `main` → `exit`、`.cfi_undefined ra`）。sp の argc/argv/envp を s1〜s4 に保つ。
- `userland/base/libc/syscall-riscv64.S`（共有 libc の `__syscall6`: 番号を a7、引数 a0〜a5、`ecall`。`__signal_restorer`: `lwu` token・`ld` context を sp の frame から、a7 = 80、`ecall`、`ebreak`。HAL の `hal_task_signal_enter` が ra に restorer を置き sp を frame にする形に合わせた）。
- `src/libc/setjmp-riscv64.S`（s0〜s11・ra・sp・fs0〜fs11 の 26 word。FP は `__riscv_flen >= 64` の時だけ）、`include/libc/setjmp.h`・`src/libc/setjmp.c` の `__riscv` の枝。
- `platform/riscv64/user.ld`（static。arm64 と同じく PT_TLS の phdr を並べる: sysroot の smoke の link は gc 無しで libc の TLS の object を残すため。gc-sections の program では空の PT_TLS になり kernel は無視する。空でない PT_TLS は `check-user-elf.py` が riscv64 で拒否。`.sdata`・`.sbss`・`.srodata` を普通の data・bss・rodata に）。
- `tools/build/check-user-elf.py` に `riscv64`（EM 243、user VA の上限 0x40_0000_0000（Sv39 の下半分）、空でない PT_TLS の拒否）。
- libc の枝: `static-tls.c`（`mv %0, tp`）、`pthread.c`（spin の `pause`）、`posix.c`（`uname` の machine `riscv64`）。`src/libc/math/sqrt.c` は riscv64 では software の平方根（`LIBM_SQRT_HARDWARE` 0、他の arch の既定と同じ。`fsqrt.d` は後の最適化）。
- `Makefile` の optional goal に `sysroot-riscv64`。

### (b) rtld・共有 libc・base の program・root

- `src/rtld/elf.h`: `EM_RISCV`、`R_RISCV_*`（NONE・64・RELATIVE・COPY・JUMP_SLOT・TLS_DTPMOD64・TLS_DTPREL64・TLS_TPREL64、`Values referenced:` psABI の表）。
- `src/rtld/rtld.c`: `HAL_ARCH_RISCV64` の `RTLD_MACHINE`・`RTLD_DATA`・`RTLD_RELATIVE`、NONE の早期の return、relocation の switch（RELATIVE、64、JUMP_SLOT、TLS_DTPMOD64、TLS_DTPREL64。TPREL64・COPY・IRELATIVE は「unsupported riscv64 relocation」で止める）、`thread_pointer()` は `tp` を読む。
- **TLS の DTV offset の bias**: ld.lld 23.1 は RISC-V で自分が解決する TLS_DTPREL64 の GOT の値を「変数の offset − 0x800」で書く（host で確かめた: `static __thread int x[4]` の GD の GOT の slot が 0xffff_ffff_ffff_f800）。→ `RTLD_DTV_OFFSET`（riscv64 は 0x800、他は 0）を足し、`__tls_get_addr` は `index->offset + RTLD_DTV_OFFSET` を block の中の位置に、loader が解決する TLS_DTPREL64 は `st_value + addend − RTLD_DTV_OFFSET` を書く。他の arch の値は変わらない（bias 0）。design の §4.3 に無かった事で、実機と QEMU で TLS の module を使う dyntest で確かめる（下の T1 の走り）。
- `src/rtld/entry-riscv64.S`（`_rtld_start`: s1 に sp、`rtld_main(sp)`、sp を戻して `jr a0`。`rtld_syscall6`）。TLSDESC は使わないので tlsdesc の .S は無い。
- `tools/build/check-dynamic-elf.py` に `riscv64`（許す relocation `{0, 2, 3, 5, 7, 9}`、ld.so の bootstrap は RELATIVE（3）だけ）。design の案 `{0,2,3,5,7,9,58}` から IRELATIVE（58）を外した: rtld が実装しない（他の arch も持たない）ので、checker と loader を揃えた。
- `platform/riscv64/userland.mk`（新、`platform/riscv64/Makefile` から include）: arm64 の形の dynamic の userland。flags は `-march=rv64gc -mabi=lp64d -mno-relax -fPIC -ftls-model=global-dynamic -mtls-dialect=trad`、link は `ld.lld -m elf64lriscv --no-relax`。ld.so・libc.so（float・libm・binary128 の software を含む）・libutil.so（`ZEDBSD_LIBUTIL_RULE`）、runtime の試験の object（rpathdep・tlstest・rpathtest・verstest・versuse・dyntest）、sh・sysctl・mount・umount・basic の program（command・pager の helper と）・network の program（netutil・dhcp と）、`ZEDBSD_ROOTFS_TREE_RULE`（arch.id `riscv64`）。desktop の `platform/common/userland-dynamic.mk` は include しない（login に要らない）。
- `platform/riscv64/rootfs.mk` に `$(BUILD)/root.img`（root の tree から `make-riscv64-root-image.py --ufs-tree … --ufs-size-mib 64`）。tool に `--ufs-tree`・`--ufs-size-mib`（`ufs_format.create(size, tree)`、sparcv9 と同じ道。`make-arch-overlay-ufs.noct` の profile は amd64・i386・aarch64 だけなので使わない）。

### toolchain の path（Q1 に渡す、適用していない）

- **Noct**: [proposed/noct-riscv64.diff](../proposed/noct-riscv64.diff)（sha256 `6f3ce02da31a861f76a07006b86bfc50967280bbd537374d78688b07ba9532fe`、`patch -p1 --dry-run` で当たる）: `userland/base/noct/zedbsd-riscv64.cmake`（arm64 の file の riscv64 版: triple、`-march=rv64gc -mabi=lp64d -mno-relax -ftls-model=global-dynamic -mtls-dialect=trad`、`HAL_ARCH_RISCV64`・`KERN_USER_ABI_RISCV64`、必須の file に `include/hal/arch/riscv64.h`・`platform/riscv64/user.ld`）と `userland/base/noct/Makefile` の arch の選択に riscv64（sysroot は `ZEDBSD_SYSROOT_RISCV64`、`$(BUILD)/bin/noct` の規則の条件）。**未試験**: Noct は static な program なので、Noct が `__thread` を使えば riscv64 では link の後に `check-user-elf.py` が空でない PT_TLS で拒む（kernel が static TLS を持たないため）。login の到達には Noct は要らないので、選ばれた config（今の `config-riscv64-virt.mk` は noct を含まない）で試すのは Q1 が適用した後。

## libc・rtld の arch の `#if` の棚卸し（2026-10-19）

| file | riscv64 の扱い |
| --- | --- |
| `userland/base/libc/static-tls.c` 229 | `tp` を読む枝を足した |
| `userland/base/libc/pthread.c` 484（amd64 の stack の揃え）・3735（spin の hint） | 484 は他の arch と同じ既定（16 揃えは thread の入口の C で十分）、3735 に `pause` を足した |
| `userland/base/libc/posix.c` 178・4046・8777（amd64 の vfork の spawn）・5376（uname） | spawn は他の arch と同じ fork の道、uname に `riscv64` |
| `src/libc/setjmp.c`・`include/libc/setjmp.h` | aarch64 と同じ自前の setjmp の枝に `__riscv` |
| `src/libc/math/sqrt.c` 34・151・179 | software（既定） |
| `src/rtld/rtld.c`（27〜45、330、358、503〜509、627〜630、1015〜1043、2078〜2197、2651、3275、3626、4621〜4909、5562〜5617、6185〜6193） | machine の定義・NONE・relocation の switch・`thread_pointer` に riscv64 を足した。static TLS（2078 の空の `layout_static_tls`、x86 だけの配置）・TLSDESC（amd64・arm64 だけ）・SPARC の PLT（sparcv9 だけ）・W+X の segment の拒否（sparcv9 以外）は他の arch の既定のまま |

## 確かめた事（host、2026-10-19）

- `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/dynamic/{ld.so,libc.so,libutil.so,dyntest,rpathtest.so}`・`build/riscv64-virt/rootfs/.stamp`・`build/riscv64-virt/root.img`: exit 0、`warning:`・`error:` 0 行（log `build/tmp/p007-build{1..4}.log`、worktree p2）。`check-dynamic-elf.py --machine riscv64` を 39 回（ld.so interpreter、libc、libutil、tlstest module、rpathtest、verstest、versuse、dyntest program、base の program 31 個 application）通った。
- 出来た物の relocation の集計（ld.so・*.so・bin/*）: R_RISCV_JUMP_SLOT 1866、RELATIVE 1061、64 137、TLS_DTPMOD64 17。TPREL64 は無い。
- 初期 exec の TLS（`-ftls-model=initial-exec`）で作った .so は `check-dynamic-elf.py --machine riscv64` が「unsupported relocation 11」で拒む（TPREL の拒否）。
- (a) の事前の確かめ（sysroot の前、toolchain を触らず `build/tmp/p007` で）: sysroot の libc＋compiler-rt の 92 source と LLVM builtins 68 個が sysroot と同じ flag で compile、crt0＋smoke＋libc＋builtins の static link が通り Machine RISC-V・undefined 0、gc-sections の static program は `check-user-elf.py --machine riscv64` を通り、gc 無しの物（libc の socket.c の TLS が残る）は拒まれる。
- root の tree: `/bin`（sh・login・ls・cat・echo・uname・dyntest ほか）、`/sbin`（init・getty・mount・sysctl ほか）、`/lib`（ld.so・libc.so・libutil.so と試験の .so）、`lib/arch.id` = riscv64、`etc/zedbsd-root`。root.img 73 MiB（UFS 64 MiB）、UFS の検査（`check-ufs-image.py` の `UFS.validate` と marker）を tool の中で通る。
- `plan/ws256/tests/ramdisk/run.sh` を tool の変更の後にもう一度: 21 件 PASS。`git diff --check` 0。
- 未実施: rtld を host で動かす fixture（rtld.c は freestanding の system call の上にあり host で動かせない。relocation の switch は code review と checker で代え、実際の動作は T1 の dyntest で）、QEMU（T1）。amd64・arm64 の build（共有の file の変更は `HAL_ARCH_RISCV64`・`__riscv` の条件の中と、bias 0 の `RTLD_DTV_OFFSET` の足し算だけ。check-*-elf.py の他の machine の表は不変）。

## T1 への依頼（Q1 経由）

image の作り方（p2 の commit、または main に merge した後の main。sysroot は main の `build/riscv64/sysroot`）:

```
make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img
```

- 走り A（合否）: `BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img BOOT_THEN=$'type:root\nexpect:[Pp]assword\ntype:root\nexpect:^root@\\S+:~\\$\ntype:uname -m\nexpect:^riscv64\\s*$\ntype:/bin/dyntest\nexpect:DL:06:PLUGIN-TLS' OUTPUT=… plan/tools/boot-test.sh build/riscv64-virt/vmunix`（BOOT_PATTERN は既定の login の prompt）。合格: login の prompt（`login.png`）、`uname -m` の答え `riscv64`、最後の画面（`login-after.png`）に `DL:06:PLUGIN-TLS`（dyntest の最後の行。TLS の module・dlopen・pthread の TLS・rpath・symbol の版を通った事）。root の password は root（serial.py の既定と同じ）。shell の既定の prompt は `root@<host>:~$ `（userland/base/sh/main.c）。打った文字が画面に出て login が進む事が UART の RX の割り込み（PLIC）の証拠（p008 の走り B の分も兼ねる）。
- 走り A の段の途中で止まった時: どの `expect` で止まったかと画面を返す（`uname -m` まで来て dyntest で止まったら、TLS の bias か rtld の riscv64 の relocation の疑い。P2 が gdbstub で解析する）。
- 判定は画面だけ。serial・console の log は判定に使わない。
- SMP 4 は boot の画面の `RISCV64 SMP 4 ONLINE`（上へ流れたら判定に数えず、p003 の T1-708 の証拠のまま）。

## 残り

- T1 の結果の判定（Q1）。
- Noct の差分の適用と試し（Q1、ユーザーの許可の範囲。上の未試験の注）。
- p004（loader）。
