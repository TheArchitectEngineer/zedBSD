# WS256 design.md のレビュー 1（2026-10-18）

design-reviewer（P2 が起動、結果は Q1 が受け取った）。対象は agent/p2-ws256 の e79c3846d の design.md と proposed/hal-riscv64.md。読み取りだけで、build・試験・QEMU は行っていない。「推測」と書いた項目は確かめていない。結論: **blocker 1・major 13・minor 24**。Q1 が下に要約して残した（原文の根拠の行番号は P2 の版の物）。

## Blocker

- **B1** initrd（ramdisk）を「LBA の scheme、partition の無い device」として root に mount する道が今の kernel に無い。`src/kern/vfs.c:2136-2139` は `rootpart=` の root が `DISK_PARTITION` でなければ EINVAL。legacy autoroot は loader の boot partition を要り（`vfs.c:1790`）、それが見つかるのは MULTIBOOT+MBR・SUN4U・X68K・PC98 の handoff だけ（`vfs.c:1493-1513`）。root の候補は boot partition と同じ disk の `/etc/zedbsd-root` を持つ UFS（`vfs.c:1800-1818`）、image overlay は ARM64 限定（`vfs.c:1820`）。修正案: (a) ramdisk の中身を rpi4 と同じ MBR の disk image（FAT の p1、marker 付き UFS の p2）にし、HAL の handoff を rpi4 と同じ `MULTIBOOT / MBR / index 1 / boot_bios_id 0x80`（`bsp-rpi4/boot.c:27-34`）、platform は `partition_set_scheme(&drv_partition_scheme_mbr)`（`rpi4.c:131`）、ramdisk を bios_id 0x80 で出す。(b) vfs.c に partition の無い root を足す（kernel 全体の変更、別の決定と Phase）。判断 #4・#9 をどちらかに合わせる。

## Major

- **M1** Sv39 の kernel 半分を「space の生成時に root の上半分を複写」で共有すると、後で作る kernel の table（device 窓の 1 GiB ごとの level-1、uncached）が既存の space に届かない。arm64 は TTBR1 で分けていて複写ではない（`arm64/locore.S:141,258`、`space.c:216`、遅れて作る table は `space.c:952,1235,1324`）。修正案: kernel 半分の第 2 段の table を init で全部作り、root の kernel の entry を二度と変えない事を assert する（または全 space の root を更新する仕組み）。
- **M2** trap の入口・出口に sscratch を 0 にする時機と kernel の stack の出所が無い。user から入った後 sscratch に user の tp が残ると、入れ子の trap で誤判定して per-CPU を壊す。percpu に「走っている task の kernel の stack の先頭」「user の sp の退避」が無い。修正案: 入口 `csrrw tp,sscratch,tp` → user の sp を percpu に → kernel の sp → frame → user の tp を frame に → 直ちに `csrw sscratch,zero`。出口は SIE=0 のまま sscratch=CPU の record → tp・sp を戻す → `sret`（sscratch を書いた後に fault し得る命令を置かない）。入口で SUM も落とす。
- **M3** PLIC の mask を enable bit で行うと complete（EOI）が捨てられ、その source が二度と来ない（Linux は mask を priority 0、EOI で一時 enable）。修正案: mask = priority 0、unmask = priority 1（init は 0）。init の claim の空読みも complete まで。
- **M4** direct map を RAM 全体の 1 GiB leaf で張ると OpenSBI の PMP の領域・no-map の領域を map する（Linux の boot.rst「must not map any resident memory, or memory protected with PMPs」、EFI の道でも reserved-memory を読む）。修正案: firmware のいる 1 GiB は 2 MiB／4 KiB の粒度で no-map を外す。UEFI の道でも DT の `/reserved-memory` と `/memreserve/` を予約として重ねる。
- **M5** loader が kernel を写した後に `fence.i` が無い（U74 は I/D 分離、QEMU の TCG では見つからない）。jump の前と secondary の入口に `fence.i`。
- **M6** locore の識別 map が 1 GiB の leaf 1 つで、DTB・handoff・initrd・image が同じ 1 GiB に入る保証が無い（推測: virt の DTB は min(RAM の終わり, 3 GiB)）。修正案: boot table で RAM 全体を 1 GiB leaf で map し、M4 の除外は最終の table で。または配置を契約に明記。
- **M7** handoff の形が二重（§3.3 の `bootloader/include/riscv64-handoff.h` と §7 #9 の `include/kern/boot.h` の `struct riscv64_boot_handoff`）。既存は loader→HAL が ZBL6（magic 0x364c425a）、HAL→kernel が `kern_boot_handoff`。boot.h の rpi4 の形には「XXX: To be removed. Use the common one.」があり、変更は記録された決定の後（`boot.h:11-13`）。修正案: 2 層を分けて定義し、boot.h の変更は差分を示してユーザーの明示の決定を得る。
- **M8** p003 の受け入れ（vmunix が link できる）には p008 の物が要る（`src/kern/elf.c:62` の `#error`、`include/kern/platform.h` の hook 一式）。修正案: 最小の `src/kern/platform/riscv-virt.c`（device 0、halt・reboot・debug_write は SBI）と elf.c の `EM_RISCV` を p003 に。
- **M9** p007 は `toolchain/llvm/sysroot.mk`（amd64・i386・arm64 だけ）を要るが p010 の範囲に無い（user の link は sysroot の builtins・crt0・libc を使う、`check-user-elf.py --machine` にも riscv64）。修正案: p010 に sysroot.mk の riscv64 を足し、Q1 とユーザーの許可を得る。
- **M10** TLS の ABI の説明が今の zedBSD と違う。zedBSD は tp が TCB を指し block はその下（`elf.c:897`、`static-tls.c:84`、`rtld.c:733`）、rtld は Variant I の static TLS を断る（`rtld.c:2078-2089`）、非 x86 の static executable は PT_TLS で ENOEXEC（`elf.c:1347-1351`）、arm64 は `-ftls-model=global-dynamic`＋TLSDESC。修正案: arm64 と同じく static TLS を持たず全部 dynamic（`-ftls-model=global-dynamic`、`-mtls-dialect=trad`、`--no-relax`）、LE・IE が無い事を `check-dynamic-elf.py` で検査。設計で決める。
- **M11** p003・p008 の判定が実質 serial の log を読む方法（`hal.selftest=1` の数を expect、「init が無いで止まる」を serial で読む）。修正案: 判断 #6 に含めて p003 の前に決める（QMP・gdbstub で読む、または p007 の login でまとめて判定）。→ 2026-10-18 ユーザーの回答「virt に画面を付ける」で扱う（Q1 の注）。
- **M12** QEMU の既定の `-cpu rv64`（Sstc・Svpbmt あり、boot hart 0）では、VF2 だけの道（SBI の set_timer、Svpbmt 無し、hart id ≠ logical id、boot hart ≠ 0、S7 の除外、M だけの第 1 context、reg-shift 2 の UART）が実機まで試されない。修正案: (1) WS256 では timer を常に SBI TIME、uncached も Svpbmt を使わない (2) p003 に `-machine sifive_u -smp 5` を足す (3) `-cpu rv64,sstc=off,svpbmt=off`。推測: property の名前は未確認。
- **M13** kernel の窓の式「0xffff_ffff_8000_0000 + 物理 0x8020_0000」は 2^64 を超える。`bootloader/uefi/elf64.c:83`（`vaddr == BASE + paddr`）と `:131-134`（`load_base > 1 GiB` を拒む）は riscv で成り立たない。修正案: 「VMA = VIRT_BASE + (paddr − LINK_PHYS_START)」に定義し直し、elf64.c の検査を arch ごとに書き分け、vmunix.ld の VMA・LMA もこの式で。

## Minor（要点）

1. `KERN_HANDOFF_MAGIC` 0x48323842 の LE の byte は `42 38 32 48`（設計は `42 28 32 48`）。
2. proposed の `hal_atomic_relax` の comment は誤り（pause は `fence w,0`、pred=W）。encoding 0x0100000f は正しい。
3. timer の ack も「EOI が何もしない定数」と明記。claim の loop は 1 件ずつ完結。次の deadline は `kernel_timer_handler` の前に書く（`bsp-rpi4/clock.c:84-96`）。
4. IPI を受ける側は「SSIP を落とす → word を swap」を word が 0 になるまで繰り返す（推測）。
5. SBI: DBCN の console_write は物理 address。`seed` CSR は不正命令になり得る（DT の zkr も見る）。推測: U74 の rdtime は OpenSBI の emulation で遅い、user に許すなら `scounteren.TM`。
6. SUM は要らない（kernel は pin した page の direct map で user を読む、`uaccess.c:292,364`）。常に 0、入口でも落とす。
7. QEMU の CPU は V を持つので user の `sstatus.VS` は Off に固定。
8. Svpbmt の無い機械では `HAL_SPACE_WC`・`NOCACHE`・`WRITETHRU` を `HAL_ERR_UNSUPPORTED` に（hal.h の契約）。
9. A/D は arm64 の扱いに合わせる（`space.c:835-837`）、`clear_flags` の後の TLB の flush、`prot_query` の順。
10. build: `MENU_CPUS` に virt の行（`tools/menuconfig.py:731`）、`ZEDBSD_PLATFORM_DIR`（`Makefile:69-73`）、`ZEDBSD_VARIANTS_riscv64_*`（`:150-155`）、NE2000・LGY98・UHCI の既定 y を riscv で n、BUILD の分け方。
11. §2.7 の hook に `kern_platform_refresh_devices`・`input_init`・`shutdown_prepare` が無い。
12. arch の `#if` の棚卸し: `elf.c:40-63`、vfs.c の ARM64 の overlay、`tcp-socket.h:52`・`packet-buf.h:24`、`entry.c:50`、`klog.c:37`、`syscall.c:9256` の ptrace（範囲外の ENOSYS か枝を足すか）。
13. `include/uapi/reg.h` の riscv の `struct fpreg` は `fp_fcsr` の後に予約を置いて 264 byte に固定。
14. LP64 の `long double` は 128 bit なので softfloat128 が要る（p007）。
15. loader: `-z notext` を外す、.bss を `VirtualSize > SizeOfRawData` で表し checker で確かめる。推測: loader の Sv39 の bootstrap table は要らない（satp=0 で物理の入口へ）。
16. 古い DT は `riscv,isa-extensions` が無いので `riscv,isa` も読む。
17. 推測: QEMU は一番低い p_paddr に入るので `_start` が最も低い LMA にある事を vmunix.ld で assert。
18. guardrail（2026-10-17）: riscv64.h の comment「10 MHz on the QEMU virt machine」と試験用の `hal.selftest=1` は「QEMU の話・試験の進め方を source に書かない」に当たり得る。
19. 記録の整合: ws.md の Phase の表に p007〜p010 が無い、p003 の意味が変わった。
20. LW（推測）: p008・p007 は足りない。
21. Phase の依存に「#13 の承認が p003 の前」「#9 の決定が p008 の前」「serial.py の tty と板の接続が p005 の前」を明記。
22. §0.2 の「GPL の file から値を引用しても license に影響しない」は判断 #1 の後に確定とする。→ #1 は 2026-10-18 ユーザー「WS245 と同じ」で決定（Q1 の注）。
23. 推測: JH7110 の BootROM が SD の SPL を読む位置を disk-image.mk の設計で確かめる。
24. secondary の入口は SBI の仕様の保証（satp=0、SIE=0、a0・a1）だけに頼り、`stvec`・`sscratch=0`・`fence.i` を入口で設定する。

## 問題無しと判断した点

CSR の番号・sstatus の bit・SBI の extension の ID・CIP-453/1200 の値（Linux 6.8.12 と一致）、JH7110 の PLIC の S context が 2h・S7 disabled（jh7110.dtsi）、signal frame の head（16 byte、restorer は ra）、user の範囲 256 GiB と vmspace の配置、`hal_cpu_idle` の wfi の並び、`hal_cpu_panic_all` の制限、`__ZEDBSD__` 無しでも `KERN_UAPI_NATIVE`・`KERN_KCRT_NATIVE` で kernel を build する道。

## 未確認

QEMU 10・OpenSBI 1.6・U-Boot 2025.01 の source（reviewer は読んでいない）、U74 の rdtime の emulation と A/D の hardware の更新、build と試験（一つも流していない）。
