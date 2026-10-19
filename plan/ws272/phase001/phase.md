<!-- awesome-plan project=zedbsd record=ws272-p001 -->

# ws272-p001 MIPS64（mips64el）の QEMU malta の差分の確認

- 親: [WS272](../ws.md)、Queue: q1276 / q1276-i01（P1）、Status: in-progress（確認済み、design-reviewer のレビュー待ち）、disposition: normal
- 設計の本体: [WS271 の design.md](../../ws271/design.md) の §1〜§6（共通）と §8（WS272 の差分）。この Phase は §8 を QEMU の source で確かめる。
- 決定: U3（FPXX は MIPS32 の話、MIPS64 は n64）、U4（CPU は `MIPS64R2-generic`）（[ws.md](../ws.md)、2026-10-19 14:29 ユーザー）。
- 範囲: 文書だけ。code・build・QEMU の起動はしない（2026-10-19 Q1: 共有の LLVM の作り直しの間）。

## 出典

QEMU v10.0.11（一部 v10.0.3 と同一）、worktree p1 の `build/tmp/ws271-docs/`（git に入れない）。SHA-256 は design.md §0 の表と一致（先頭 16 桁を 2026-10-19 14:34 に `sha256sum` で照合: cpu-defs.c.inc a29998857ec9c02a、bootloader.c d14c0f39609a9e4b、malta.c b5066d887fa1218c）。
`target/mips/cpu.h`・`hw/mips/mips.h`（`cpu_mips_kseg0_to_phys`・`cpu_mips_phys_to_kseg0`・`USEG_LIMIT` の定義）と、例外の vector の offset を決める `target/mips/tcg/system/` の helper は**取得していない**。

## 確かめた事（source の行）

| §8 の項目 | 結果 | 出典 |
| --- | --- | --- |
| CPU の型 | `MIPS64R2-generic`: `insn_flags = CPU_MIPS64R2`、`mmu_type = MMU_TYPE_R4000`、Config1.MMU = 63（**TLB 64 entry**）、Config1.FP = 1、FCR0 に F64・L・W・D・S・PS・3D、`CCRes = 2`（Count 160 MHz）、`SEGBITS = 42`、`PABITS = 36`、Config3 = LPA だけ（VInt 無し）、`CP0_Status_rw_bitmask = 0x36FBFFFF`（FR・UX・SX・KX が書ける） | cpu-defs.c.inc 672〜697 |
| 既定の 20Kc を使わない理由 | `20Kc` は `CPU_MIPS64R1`、Config1.MMU = 47、`CCRes = 1`、`SEGBITS = 40` | cpu-defs.c.inc 647〜666、malta.c 1303〜1306（64 bit の build の既定 20Kc） |
| n64 と hardfloat | FPU あり・FCR0.F64 で FR=1 が使える → n64 の hardfloat が成り立つ。FPXX は使わない（U3） | 同上 |
| -kernel の ELF | `load_elf(..., cpu_mips_kseg0_to_phys, ..., EM_MIPS, ...)` は ELF の class を問わない形。entry が `USEG_LIMIT` 以下なら拒む | malta.c 880〜893 |
| 受け渡しの符号拡張 | bootloader は ISA_MIPS3 の CPU で `bl_gen_load_ulong` → `bl_gen_dli`（64 bit の即値を lui/ori/dsll で組む）で sp・a0〜a3・jump の先を入れる。値は `target_ulong` の `ENVP_VADDR` 等そのもの | bootloader.c 227〜274、malta.c 681〜690 |
| 表の pointer | `prom_set` は `tswap32(ENVP_VADDR + table_addr)`（32 bit の entry）。kernel は 32 bit で読み符号拡張する | malta.c 832〜848 |
| memory map | RAM は最大 2 GiB を物理 `0x80000000`、先頭 256 MiB を物理 0 に alias、512 MiB 超は別の alias。全部 32 bit の物理の中 → XKPHYS の direct map で PageGrain.ELPA は要らない | malta.c 1117〜1132 |
| ram の引数 | a3 と `memsize` は `ram_low_size = MIN(RAM, 256 MiB)`、`ememsize` は RAM の全体（`%u`、2 GiB まで 32 bit に収まる） | malta.c 944〜948、1157 |

## 未確認（p002 で gdbstub・monitor で確かめる）

1. `cpu_mips_phys_to_kseg0` が 64 bit の build で `0xffffffff8000_2000` の形（符号拡張）を返すか、`cpu_mips_kseg0_to_phys` が `0xffffffff80100000` を物理 `0x00100000` にするか（定義の file を取得していない）。確かめ方: `-S -gdb`、entry で `info registers` の a1・a2・sp、monitor の `xp /4wx 0x2000`。
2. entry の Status（KX=0・ERL の値）。reset 直後は KX=0 と読める（bootloader は Status を書かない）→ **HAL は XKPHYS に触れる前に Status.KX=1 を書く**（§8 に追記）。
3. XTLB refill の vector が EBase+0x080（BEV=0）である事（PRA の定義、QEMU の helper は未取得）。
4. `-msym32` の n64 の kernel を LLVM の lld が `elf64ltsmip` で link でき、e_entry が符号拡張された値になる事（共有の LLVM の Mips の target の後、build で）。
5. `lld`/`scd` が QEMU で正しく動く事（R4000 の MMU の CPU で LL/SC を持つ、と読めるが試験していない）。
6. 3 段の page table（9+9+9+12）は設計だけ。SEGBITS 42 の xuseg の下 512 GiB だけを使う範囲で破綻が無い事は p003 で。
7. HAL の `mips64.h` の案は [proposed/hal-mips.md](../../ws271/proposed/hal-mips.md) の形（U1 で事前承認）。中身は p002 で書く。

## 設計の修正（改訂 2、2026-10-19 P1、q1277）

design-review-1 の M8 に合わせ、design.md §8 に「物理の範囲」の行を足した: 正の物理の範囲を low の alias（物理 0〜`min(RAM, 256 MiB)`）と `0x80000000 + 256 MiB`〜`0x80000000 + RAM` に決め、物理 `0x80000000`〜`+256 MiB` と `0x20000000` の alias は memory map に入れない。kernel の image（物理 `0x00100000`〜）と ENVP（`0x2000`）は low の側で予約する。direct map（XKPHYS）と CKSEG0 の virt→phys はどちらもこの範囲に入る。上の「memory map」の行の「XKPHYS の direct map」は、この範囲の上で読む。
MIPS64R2-generic の cache（256 set × 32 byte × 4 way、way 8 KiB）は色がある（design.md §3.5、M7）。QEMU では表に出ない。

## 設計の修正（版 1）

design.md §8 の表に「Status の KX は entry で 0、XKPHYS の前に立てる」と「Config3.LPA=1 だが PageGrain.ELPA は立てない（RAM は 32 bit の物理の中）」を足し、確認の水準（*）を source で確かめた所から外した。WS271 の部分は変えていない。

## 実行したコマンド

`git merge main`（worktree p1、fast-forward で b4bd8c980）、`grep`・`sed -n`・`sha256sum`（上の出典）。build・QEMU はしていない。
