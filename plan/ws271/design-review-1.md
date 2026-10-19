<!-- design-reviewer の報告（2026-10-19、ws271 design.md 版 1 と proposed）。Q1 が報告の本文をそのまま置いた -->

## ws271-p001 設計の敵対的レビュー（design.md・proposed/llvm-mips.md・hal-mips.md・boot-test-mips.md）

読んだもの: `/home/awe/zedBSD-claude1/plan/ws271/design.md`（レビュー中に q1276 が §8 の 3 行を直した。commit 99170e6fa の今の形でレビューした）、proposed の 3 つ、ws271・ws272 の ws.md と phase001、Guardrail、hal.h、arch.h、riscv32.h・riscv64.h、ppc32・riscv32 の space と platform、boot-test.sh、それに QEMU の写し `/home/awe/zedBSD-worktrees/p1/build/tmp/ws271-docs/` の全 file。source・plan は変えていない。build・QEMU・rm・commit もしていない。

U1〜U4 はユーザーがもう決めている（ws272/ws.md「決定（2026-10-19 14:29 ユーザー、クリック）」）。そのため U の項は、決定の前提に誤りがあるかだけを見た。

### Blocker
なし。

### Major

**M1. 片方だけ有効な TLB の対（V=0）が古いまま残る**
- 根拠: design.md 163〜164 行。refill は偶・奇の 2 つの PTE を同時に入れる。hal.h 750〜763 行の `hal_space_map` には「complete any required TLB synchronization」とある。
- 起こる状況: 隣の page の refill で、この page が V=0 のまま TLB に入る。その後 fault の処理で PTE を作って user に戻っても、TLB の entry は V=0 のまま残る。refill はもう来ないので TLB Invalid が無限に続く。`tlbwr` で同じ VPN2・ASID を二重に書くと、実機では machine check になる。
- 修正案: `hal_space_map`・`unmap`・`prot`・`clear_flags` は、その space の ASID で `tlbp` し、当たったら `tlbwi` で書き直すか無効化する。この規則を §3.4 に足す。

**M2. ACCESSED・PROT_NONE・exec を PTE で表せない**
- 根拠: design.md 162 行に「30 = accessed は持たない」とある。kernel は vm.c 5993・9549 行で `HAL_SPACE_PAGE_ACCESSED` を回収の second chance に使う。hal.h 721〜723・806〜838 行の query と clear_flags が、この状態の報告を求める。前例の ppc32/space.c 59・183・497 行は software の accessed を持つ。
- 起こる状況: 回収の順が壊れる。PROT_NONE（present だが V=0）と「無い」を区別できない。R2 で RI/XI の無い CPU では `HAL_SPACE_EXEC` を強制できない。
- 修正案: software の bit として present・accessed・writable を決める（例: V=0 で入れて、最初の TLBL Invalid で accessed と V を立てる。Linux の MIPS と同じ考え）。exec を強制できないことは制限として明記する。

**M3. 今の space でない space の TLB flush と、ASID の再利用の手順が無い**
- 根拠: design.md 165 行は「使い切ったら全 flush・世代」だけで、`hal_space_flush_tlb(space)`・`_range`・`hal_space_destroy` の手順が無い（hal.h 842〜866 行）。
- 修正案: space ごとに (ASID, 世代) を持つ。今の space でない space の flush は、その ASID を捨てるだけにする（次の switch で新しい ASID を取る）。range の flush は割り込みを止めて EntryHi.ASID を一時的に替えて `tlbp`。destroy の時は ASID を解放する。

**M4. user の Status の作り方と消毒が無い（権限の昇格）**
- 根拠: design.md 89 行で入口の Status に CU0=1。trap frame と context switch は Status を保存・復元する（156・130 行）。
- 起こる状況:
  - user の Status に CU0=1 が残ると、user が `mtc0`・`tlbwr` を実行できる。
  - sigreturn や ptrace（hal_gpregs.status、hal-mips.md 59 行）の値をそのまま `eret` すると、KSU=kernel・CU0・EXL を user が選べる。
  - task ごとに Status を丸ごと戻すと、IM・CU1 が古い値に戻る。Linux の r4k の switch は IM・IE を今の値のまま保つ。
- 修正案: user の Status は KSU=user・EXL・IE・IM の固定の mask から作り、CU0=0、CU1 は lazy の owner の時だけ立てる。書き込まれる値は mask で消毒する。context switch で IM・CU1 を保存・復元の対象から外す。kernel は CU0 が無くても CP0 を使えるので、CU0 は立てなくてよい。

**M5. FPU の復元で FCSR の Cause を消していない**
- 根拠: design.md 109 行と 113 行の lazy FPU。
- 起こる状況: SIGFPE の後や、ptrace・sigcontext で書かれた FCSR が Cause と Enable の同じ bit を持っていると、kernel の `ctc1` の時点で FPE が起こり、kernel で panic する。
- 修正案: 復元と書き込みの前に FCSR の Cause の bit を消す。FPU の無い CPU と Config1.FP=0 の扱いも §3 にまとめる。実機の 24Kf の Unimplemented Operation（denormal）は SIGFPE になる。これを制限と書くか、FS=1 を既定にするかを決める。

**M6. FR=1 を常に使う前提が WS273 以降の実機で成り立つか、確かめていない**
- 根拠: design.md 109 行、U3 の決定、共通の層を RG350（WS273）に使う方針（138 行）。
- 起こる状況: FCR0.F64=0 の CPU では FR を立てられない（JZ4770 が F64 を持つかは未確認）。その時は hal_fpregs の保存の形（32 個の 64 bit）が合わない。
- 修正案: 起動時に FCR0.F64 を確かめて、無ければ FPU を使わない（hardfloat の user を拒む）と明記する。WS273 の設計でもう一度確かめる、と U3 の記録に添える。

**M7. cache の aliasing が書かれていない**
- 根拠: design.md 168〜171 行は「実機のために正しく書く」と言うが、VIPT の way の大きさ > 4 KiB の扱いが無い。
- 起こる状況: 24K の 32 KiB 4-way や、Qube の RM5230 の 32 KiB 2-way では way が 4 KiB を超えるので、KSEG0 の direct map と user の VA が違う色の line を持つ。`synci` や `Hit_*` を KSEG0 の address で打っても、user 側の色の line は消えない。QEMU では表に出ない。
- 修正案: Config1 から way の大きさを求め、page より大きければ起動で「未対応」と止める。page の色を揃える実装は実機の WS へ回す。どちらにするかを §3.5 に書く。

**M8. MIPS64 で同じ RAM が 2 つの物理 address を持つ（WS272）**
- 根拠: design.md 227 行（今の版）では、RAM を物理 `0x80000000` の XKPHYS で扱う。一方、kernel の image は CKSEG0 で、物理は `0x00100000`（malta.c 1122〜1136 行の low alias。load_kernel は `cpu_mips_kseg0_to_phys` で置く）。
- 起こる状況: allocator が `0x80100000〜` を空きとして kernel の image や ENVP の上に重ねて渡す。kernel の page の virt→phys が allocator の範囲に入らない。
- 修正案: 正の物理の範囲を 1 つに決める。low alias の 256 MiB を正とし、上は `0x80000000+256MiB〜` だけを足す、などの形にする。image と ENVP（`0x2000`）の両方の alias を予約する。§8 に書く。

**M9. 試験の計画では M1〜M5 を見つけられない**
- 根拠: design.md 204 行の判定の文字列。前例の ppc32/space.c 149 行は「PAGING PASS」を何も確かめずに出力している。到達点は VFS の段で、user の process も FPU も動かない。
- 修正案: p003 の self-test の中身を design に書き、通った時だけ PASS を出す。kernel mode から kuseg を使えば次を確かめられる。
  - map の後の refill
  - 片側だけ V=0 の対
  - TLB Mod による dirty
  - unmap の後の fault
  - 2 つの space の ASID の切り替え
  - ASID を小さくして世代が回ること
  - 2 つの task の間の FPU の lazy の保存と復元（Cause の消毒を含む）

### Minor

1. **source を読めば分かる事実が「未確認」のまま**
   - 入口の Status は BEV=1・ERL=0・EXL=0・IE=0・KSU=kernel・CU1=0・FR=0。cpu.c 306 行で BEV|ERL になり、malta.c 1024〜1026 行の `main_cpu_reset` が ERL を消す。
   - Config.K0=2（uncached。cpu-defs.c.inc 24〜25 行）。
   - EBase の reset 値は `0x80000000`（cpu.c 300 行）。
   - timer は IP7（cpu.c の IntCtl `0xe0000000` で IPTI=7）。
   - SOFTRES は `0x1f000500` に `0x42`（malta.c 500〜502 行）。
   - PCI の窓（malta.c 650〜664 行の値と gt64120.c 323〜389 行の式から計算）: I/O は `0x18000000〜0x181fffff`、MEM0 は `0x10000000〜0x17ffffff`（128 MiB）、MEM1 は `0x18200000〜0x1bdfffff`（60 MiB）。VGA の BAR の案は MEM0 に収まる。
   - VGA の slot: 自動で置く device は slot 18 から（gt64120.c 1203〜1206 行の `pci_root_bus_new` の devfn_min が `PCI_DEVFN(18,0)`）。VGA は slot 18 の見込み。slot 0 は GT 自身（11ab）なので、走査で読み飛ばす。
2. **属性 controller の `0x20` の書き込みには前例がある**: `src/hal/sparcv9/bsp-sun4u/framebuffer.c` 41〜55 行。std VGA の framebuffer の手順は、ramfb の riscv32 ではなく、こちらを手本にする。
3. **platform の binding の file 名が古い**: design.md 196 行は `src/kern/platform/riscv-virt.c` を流用すると言うが、今の riscv32・ppc32・m68k は `src/kern/platform/hal-console.c` を使う（platform/riscv32/vmunix.mk 52 行）。`riscv-virt.c` は riscv64 の FDT 用。
4. **handoff の契約と食い違う**: hal-mips.md 85 行は「無ければ HAL_ERR_NOT_FOUND」と書くが、hal.h 1284〜1291 行の `hal_get_arch_handoff` は pointer を返し、無い時は NULL。`mips.initrd` は使う側が無いので今は足さない方がよい。足すなら値の形（riscv と同じ 2 つの uint64）と名前（`mips32.`・`mips64.` か共通か）を決める。`rd_start` は KSEG0 の virt なので物理に直す。
5. **cmdline・initrd の扱い**: `boot.command-line` に `rd_start=… rd_size=…` の前置きが残る。initrd の範囲（low RAM の上の端）を allocator から除く記述が無い（design.md 212 行）。
6. **toolchain の未検証の点**:
   - FPU の保存の asm は `.set hardfloat` だけでは奇数の 64 bit の register を使えない見込み。`.set fp=64`・`.set oddspreg` が要るかもしれない（LLVM の受け入れは未確認）。
   - clang の `-msym32` の受け入れは未確認。無くても n64 の static の code は動くので、p002 で小さな object で確かめてから使う。
   - kernel は LTO が既定（Makefile 130 行）。`-G 0` は function の属性ではないが、`-mno-gpopt` で gp の相対の参照は作られない見込み。`.sdata` が無いことを p002 で `readelf` で確かめる。
7. **softfloat の既存の仕組みに触れていない**: tree にもう `src/libc/softfloat.mk`（integer だけの compiler の runtime）がある（Makefile 754・760 行）。llvm-mips.md 27〜28 行は「compiler-rt に mips の builtins を足す」とする。hardfloat の userland にも 64 bit の除算の helper が要る。
8. **hal-mips.md 51 行の `pause` の理由が誤り**: PAUSE は R2 の基本の命令（`sll $0,$0,5` の符号化なので、古い CPU では nop）。ただ LLbit を待つ意味なので、`nop` を選んだ結論は妥当。
9. **起動の順**: §7.1（211 行）は EBase の設定が最後。それより前の例外は BEV=1 の `0xbfc00380`（QEMU の小さな bootloader の領域）へ飛んで、何も出さずに壊れる。`_start` の最初に EBase を設定して BEV=0 にする。
10. **細部**:
    - refill は EBase+0x000 の 0x80 byte に収める。
    - kernel の VA（KSEG2/3）の refill は user の root の上の半分（0 のまま）を見る、と明記する。
    - CP0 の hazard（`ehb`・`jr.hb`）の記述が無い。
    - o32 で C を呼ぶ時の引数の 16 byte の領域を trap frame に確保する。
    - 8259 の IRQ の番号は、GT の `0xc34`（IACK）か OCW3 の poll で取る（どちらも QEMU にある）。
    - xuseg の 512 GiB を超える address は refill で拒む。
11. **文書の不整合**:
    - §1.1（33 行）の「§7.5」は存在しない。
    - §0 の版の表記: 取得は v10.0.11 なのに、行の版が v10.0.3 になっている。comment の `Value referenced:` の版がぶれる。
    - 未記録の SHA-256（写しの値、upstream との照合は未確認）:
      - vga-pci.c: `9c497f5f7ea5fd0a72399c4b242b58f32edee391907c941e2f40b9e0c719f03e`
      - i8259.c: `8ea7ab1cce169d826d82b41149f838fa4f50444ba98bc86874176a0bf1c0522e`
      - vl.c: `d065734e676f24075e28e4a0f4339bf8c5f7f7bec1b30d44c5de96063a761f15`
      - cpu.c: `04122500bffacbf6973483a2f58b0490dba594008fd9d1a02a8dd3129899d9a6`
      - cp0_timer.c: `8633c575aae479aeee6c9009aef579ad1304b9c6d8c6637a5b6b0bd1e4f22592`
12. **boot-test の案**:
    - 2 番目の case（`qemu=`・`required=`、boot-test.sh 168〜200 行）に足すことが明記されていない。足さないと「unknown BOOT_MODE」で止まる。
    - `boot_interval` 0.5 と usage の comment（21 行）の更新が無い。
    - FPGA の `vc:320x200` の text console が screendump の console 0 を取らないことは、QEMU の「graphic の console を先に置く」処理による見込み（未確認）。
13. **Phase**:
    - WS272 p002 の依存に「HAL の差分 D1・D3 の承認」が無い（承認はもう済んでいる）。
    - p003 の 20 LW は、写せる MIPS の HAL が無い状態で、例外・TLB・space・ASID・FPU・8259 を全部含む。楽観的に見える。p003 を「例外・timer・8259・task」と「TLB・space・FPU」に分けて、それぞれに T1 の確かめを置く案がある。
14. **toolchain の現状が文書と違う**: design.md 95 行は「今 Mips が無い」と書く。main の作業ツリーでは `toolchain/llvm/llvm.mk`（`Mips` を追加）と `version.mk`（`zedbsd10`）が未 commit で変わっている。`build/llvm` はまだ zedbsd9 で、`--print-targets` に mips は出ない（確かめた）。

### 確かめて問題が無かった事
- **memory map**: RAM は物理 `0x80000000` に全体、物理 0 に `min(RAM,256MiB)` の alias、512 MiB を超える分は `0x20000000` に alias。上限 2 GiB。
- **GT-64120**: register を `0x1be00000` へ移す（ISD）。LE の時は GT_CPU の bit 12 と bootloader の bswap が打ち消し合い、register には `addr>>21` が入る。PCI の bus address と CPU の物理 address は同じ。config は `+0xcf8`・`+0xcfc` に LE のそのままの store でよい。
- **PIIX4**: devfn 10。PIRQ は 10・10・11・11。8259 は IP2、CBUS UART は IP4（`serial_hd(2)`、reg shift 3）。
- **`-kernel` の受け渡し**: sp・a0〜a3 の値、表の 32 bit の pointer、env（memsize・ememsize・modetty0・rngseed）、USEG の entry の拒否、64 bit で `dli` を使うこと（bootloader.c の `bl_gen_load_ulong`）。
- **CPU**: 24Kf は R2・FPU（F64）・TLB 16 entry・CCRes 2。Count は 160 MHz（refclk 320 MHz / CCRes 2）。Status の FR は書ける（mask `0x3678FF1F`）。MIPS64R2-generic は TLB 64・SEGBITS 42・PABITS 36・LPA。
- **VGA**: 既定は cirrus なので `-vga std` が要る。std VGA の BAR0 は 16 MiB の prefetchable、BAR2 は MMIO（mmio と qext は既定で有効）。DISPI の 0x41。
- **既存の code との整合**: o32 の stat.h の枝（riscv32・ppc32 と同じ 92 byte）、`KERN_USER_ABI_LP64` の使い方。hal-mips.md の arch.h の節、signal frame の head、checks の形は riscv32.h・riscv64.h と同じ並びで、2026-10-18 の事前の承認と同じ形（U1 (a) と合う）。
- **toolchain の分け方**: kernel を `mipsel-unknown-elf -D__ZEDBSD__=1` で build する前例（ppc32 の vmunix.mk 10 行と同じ）、linker の emulation の名前、soft/hard を triple ではなく flag で分けること（llvm-mips.md）は妥当。
- **Guardrail**: 出典の引用の規則（値は事実として使う・block ごと・複数行の comment）と、toolchain・HAL・tools を Q1 に依頼する形は、Guardrail と AGENTS.md に合う。

### 確かめていない事（推測を含む）
- JZ4770・MT7621・RM5230 の cache の形と FCR0.F64（M6・M7 の実機での影響）。
- LLVM 23 の `-msym32`・`.set fp=64` の受け入れと、MIPS の LTO の振る舞い。
- Super I/O の COM1 が `serial_hd(0)` であること（isa-superio の source が手元に無い）。
- 64 bit の `cpu_mips_kseg0_to_phys` と `ENVP_VADDR` の符号拡張（target/mips の該当の source が手元に無い）。
- QEMU の screendump の console の順（ui/console.c が手元に無い）。
- QEMU の起動・gdb による確かめ（していない）。

### 結論
p001 は設計として条件付きで受け入れてよい。p002（入口・PCI・VGA・console）は今の設計で始められる。ただし p003 に入る前に、M1〜M5・M9 を design.md に足す（M8 は WS272 p002 の前、M6・M7 は制限か WS273 への引き継ぎとして明記）ことを条件にする。
