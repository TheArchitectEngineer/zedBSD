<!-- awesome-plan project=zedbsd record=ws256 -->

# WS256 StarFive VisionFive 2（JH7110、RISC-V）への移植

- ID: ws256、Master: [master.md](../master.md)、Primary Milestone: MG008（関係: MG003）
- Status: incomplete（2026-10-18 Q1 作成、p002 は Q1 が実施。他の Phase は詳細を Q1 とユーザーで決めてから P2 が開始する）
- 起点（2026-10-18 ユーザー）:「WSを作成します。Vision Five 2ボードに移植します。HALにディレクトリだけ作りました。こちらをP2で実行しますが、さきに詳細をメインエージェントQ1と私で詰めてからにします。」
- ユーザーが作った空の directory: `src/hal/riscv64/bsp-visionfive2/`。
- 担当: P2（開始は詳細の合意の後）。

## ユーザーの決定（2026-10-18、クリック）

1. 最初の到達目標: 「UART の login」。S-mode の kernel が OpenSBI の上で起動し、UART の console で login、SMP 4 core、timer・PLIC の割り込み、root は initrd か SD。まず HAL の芯を固める。
2. 起動の道: 「U-Boot の UEFI＋zedBSD の loader」。U-Boot の EFI で bootloader/uefi の riscv64 版を動かし、amd64・arm64 と同じ道で kernel を渡す。DTB は U-Boot から。
3. toolchain: 「許可、Q1 が行う」。Q1 が toolchain の lock を外して LLVM_TARGETS_TO_BUILD（今は `PowerPC;AArch64;X86`、toolchain/llvm/llvm.mk）に RISCV を足し、作り直して lock し直す。P2 は toolchain に触らない。
4. 試験の環境: 「これから準備」。板はまだ host につないでいない。最初は QEMU の virt（riscv64）で HAL の芯を作り、実機は後。

## 完了の条件（案、Q1。ユーザーとの議論で確定する）

1. QEMU の virt（riscv64）で、UEFI の firmware（U-Boot の EFI か edk2）→ zedBSD の loader → kernel の順に起動し、UART の console で login できる。SMP 4 core、timer と PLIC の割り込みが動く。判定は virt に付けた画面（ramfb）を `boot-test.sh` で撮った login の prompt と、serial で打った command の答えの画面（決定 5、[design.md](design.md) §5）。
2. VisionFive 2 の実機で、板の SPL＋OpenSBI＋U-Boot の EFI → zedBSD の loader → kernel で、UART の console に login できる（SMP 4 core、timer、PLIC）。root は initrd か SD。実機の判定の方法は未決（design.md §7 #15）。
3. HAL の riscv64 の部分（`include/hal/arch/riscv64.h` などの API の追加）は、差分ごとのユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。
4. 変えた C のコードが plan/coding-style.md の全文に合う（最後の規約の Phase）。

## Phase

[design.md](design.md) §6 の表の写し（2026-10-18 改訂 2）。実行の順は ID の順ではない: p009 ∥ p003 → p008 → p007（p010 が途中）→ p004 → p005 → p006。LW は 1 LW ≈ 実時間 15〜25 分、±50%。

| Phase | 目的・範囲 | 依存 | LW | 状態 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: JH7110・U74 の CPU（RV64GC、Sv39）・OpenSBI の SBI（timer・IPI・HSM・console）・PLIC・CLINT・UART（8250 の DW）・U-Boot の EFI の調べ、HAL の riscv64 の API の差分の案（承認の前は plan に置くだけ）、loader の riscv64 版、build（platform/riscv64・config）。code は書かない。改訂 2 でレビュー 1 の全件を反映 | — | 済 | cleared（2026-10-18 Q1: 改訂 2 でユーザーが「今の改訂 2 で判断する」、#13・#17・#18 を回答） |
| p002 | toolchain: LLVM に RISCV の target を足す（Q1） | — | 済 | cleared（2026-10-18 Q1: toolchain/llvm/llvm.mk の LLVM_TARGETS_TO_BUILD と config の identity に RISCV。共有の build/llvm を source から作り直し（log build/tmp/llvm-riscv-20261018.log、FAILED・error 0）、`clang --print-targets` に riscv64、rv64gc の object を確認、config-amd64.mk の vmunix の build と check PASS。toolchain は lock し直した。残り: install の identity は version・patch だけで target を見ない、CI の binary cache（rev-0、zedbsd8）に RISCV が無い → CI で riscv64 を build する時に patch level を上げて新しい cache を公開） |
| p009 | 試験の道具（Q1 の tools）: `boot-test.sh` の `BOOT_MODE=riscv64-virt`・`riscv64-uboot`（`-device ramfb`、`-serial unix:`）、`BOOT_PATTERN`、login の後に serial.py で command を打って 2 枚目を読む段、`plan/ws256/tests/` の config.mk と image の手順、QEMU の DTB（dumpdtb）を `tests/virt.dts` に。担当は design §7 #18 | — （p003 の T1 の試験の前） | 2〜3 | cleared（2026-10-18 Q1、T1-707） |
| p003 | HAL の芯（QEMU virt、直の起動）: `include/hal/arch/riscv64.h`（承認後）と `src/hal/riscv64/` の全部、bsp-virt（fw_cfg・ramfb の framebuffer・80x25 の console・font）、最小の `src/kern/platform/riscv-virt.c` と `elf.c` の `EM_RISCV`、`platform/riscv64/` の vmunix の build、`Makefile`・menuconfig・config。受け入れは画面の `RISCV64 PAGING PASS`・SBI の版・`TIMER TICK`・`SMP 4 ONLINE`・root 無しの失敗、`cpus=1`、`-cpu rv64,sstc=off,svpbmt=off` | HAL の差分の承認（#13）が着手の前、p009 が T1 の前 | 28〜36 | cleared（2026-10-18 Q1、T1-708） |
| p008 | kernel 側: platform の残り（console・ramdisk・device の表）、`ns16550.c`（RX）、`serial-console.c`、`ramdisk.c`、MBR の `root.img` の rootfs.mk、FDT・initrd の handoff（#9 の決定の後）、kernel の arch の `#if` の棚卸し。受け入れは画面の ramdisk の mount と「init が無い」 | p003、#9 | 10〜12 | planning |
| p007 | userland の riscv64: crt0/crt1/setjmp/syscall の .S と `user.ld`（先に書いて p010 (b) に渡す）、libc・rtld の枝と relocation（TLS は全部 dynamic）、Noct の cmake、`check-dynamic-elf.py`、最小の user program と root.img。受け入れは画面の login の prompt と、serial で打った `uname -m` の答え `riscv64` | p008、p010 (a) が libc の前、p010 (b) が link の前 | 14〜18 | planning |
| p010 | toolchain（Q1、ユーザーの許可）: (a) LLVM の patch zedbsd9（riscv64 の `__ZEDBSD__`、linker の emulation）、(b) `sysroot.mk` の riscv64、(c) CI の cache | (a) いつでも、(b) は p007 の .S の後 | 2〜3 | in-progress（2026-10-18 Q1: (a)・(c) 済み。patch に riscv64（ZedBSDTargetInfo<RISCV64TargetInfo>・elf64lriscv・試験）、PATCH_LEVEL zedbsd9 で共有の LLVM を作り直し、`riscv64-unknown-zedbsd` で `__ZEDBSD__`・`-m elf64lriscv` を確認、amd64 の vmunix の build と check PASS。cache は rev-0 の asset zedbsd-llvm-23.1.0-zedbsd9-x86_64-linux.tar.gz（sha256 e971118c…、download して一致）。(b) sysroot.mk は p007 の時） |
| p004 | loader の riscv64（PE の header の自作、`.rela.dyn` の自己 relocation、RISCV_EFI_BOOT_PROTOCOL、DTB の複写、ZBL6 の handoff、`satp`=0 で物理の入口へ）、`riscv64-handoff.h`、`bootloader.mk`・`disk-image.mk`（ESP＋root の GPT）、U-Boot `qemu-riscv64_smode` の導入（Q1、apt）、`elf64.c` の arch の書き分け。受け入れは U-Boot → BOOTRISCV64.EFI → vmunix → 画面の login と `uname -m` | p007、p009 | 10〜14 | planning |
| p005 | VisionFive 2: bsp-visionfive2（ccache の dcache、mimpid・marchid の log、errata）、`visionfive2.c`、DW UART の RX の癖、platform/config/menuconfig、SD の 4 partition の image（SPL・itb・ESP・root）、U-Boot の SD からの起動の手順、実機の試験の道具、実機の login（10 回） | p004、板の接続・電源・UART（#10）、#15（実機の判定）、#2・#8 | 8〜12 ＋ debug 4〜8 | planning |
| p006 | 規約の全文の見直し | 全部の後 | 2〜3 | planning |
| p011 | VisionFive 2 の framebuffer（2026-10-19 ユーザー「VisionFive2ってGPUもあるからまずはフレームバッファがあるとうれしい」→「WS256 に Phase を足す」）: (1) 調べ: 使う U-Boot（StarFive の配布版・mainline）が JH7110 の HDMI を初期化して UEFI の GOP を出すか、出すなら p004 の loader が GOP から ZBL6 の framebuffer を渡し、kernel は汎用の framebuffer の console で済む。(2) 出さない時は自前の display の driver の設計（VOUT の PMU・syscrg/voutcrg の clock・reset、Verisilicon DC8200、Innosilicon の HDMI の送信器と EDID。出典は StarFive の JH7110 の TRM、Linux の driver は値の引用だけ）を design-reviewer に通し、Phase に分ける。GPU（PowerVR BXE-4-32）は範囲外 | p004・p005（UART の login の後）、実機 | (1) 1〜2、(2) は設計の後に見積もる | planning |

合計 76〜106 LW（うち Q1 の p009・p010 4〜6、実機の debug 4〜8）。

## 未決（Q1 がユーザーに聞く。詳細と既定の案は [design.md](design.md) §7.2）

- 板の版（v1.2A・v1.3B）と memory の大きさ（2・4・8 GB）（#2）。
- 実機の試験の接続（USB-UART・電源の制御・LAN）（#10）と、**実機の判定の方法**（画面が無い。serial.py の対話を実機に限る例外にするか: #15）。
- root の媒体（initrd か SD: #4、既定 initrd）、SD・eMMC・GbE・USB を後の WS にするか（#3。display は 2026-10-19 に p011 としてこの WS に入れた）。
- `include/kern/boot.h` を変えるか（#9: 既定は変えず HAL の名前付き handoff object）、HAL の差分 2 件＋名前付き object の承認（#13、p003 の前）、toolchain の p010（#14）。
- sifive_u の走りを gdbstub の読み取りで判定に数えるか（#17）、p009 の tools の変更の担当（#18）。
- 決定済み: 参照の方針（6、WS245 と同じ）、virt の判定（5、画面）。

## ユーザーの決定（2026-10-18、クリック、2 回目）

5. 起動の確認: 「virt に画面を付ける」。QEMU の virt に ramfb か virtio-gpu を付け、boot-test.sh の画面で判定する（serial の log で判定する例外は作らない）。
6. 外部の code の参照: 「WS245 と同じ」。手順は参考にし、code・comment・構造は写さず、block ごとに Value(s)/Procedure referenced: を書く。

## 設計（p001）

- [design.md](design.md)（P2、q1243。初版は main fc8e21f8c、改訂 2 は q1243-i02、main e4c67abc5）、HAL の差分の案 [proposed/hal-riscv64.md](proposed/hal-riscv64.md)（差分 1・2 と名前付き handoff object。未承認、適用しない）、boot.h の差分の案 [proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md)（案 (b)、既定は適用しない）。
- レビュー 1: [design-review-1.md](design-review-1.md)（blocker 1・major 13・minor 24）→ 改訂 2 で全件を反映（design.md §11 の表）。
- 改訂 2 で決めた事の要点: initrd は MBR の image（rpi4 と同じ root の道、kernel の vfs.c は変えない）、画面は ramfb を HAL の bsp-virt が fw_cfg で設定して 80x25 の console を描く（U-Boot の GOP に依らない、両方の入口で同じ）、timer は常に SBI・Svpbmt は使わない、TLS は全部 dynamic、PLIC の mask は priority、kernel 半分の table は init で封じる、loader は table を作らず satp=0 で物理の入口へ。

## ユーザーの決定（2026-10-18、クリック、3 回目）

7. 設計は「今の改訂 2 で判断する」（レビュー 2 は回さない）。
8. #13 HAL の差分 3 件は「3 件とも承認」（[Guardrail](../guardrail.md) の表）。
9. #17 sifive_u の走りは「gdbstub で読んで判定」（WS256 の sifive_u に限る例外。serial の log は使わない）。
10. #18 試験の道具の変更は「P2 に許可」（p009 で plan/tools の 3 file だけ。既存の mode は変えない。Q1 が merge の前に読む）。

11. #9 は (a): HAL → kernel は汎用の構造体と `hal_get_arch_handoff` の名前付きの object だけ（2026-10-18 ユーザーの規約、[Guardrail](../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」）。[proposed/boot-h-riscv64.md](proposed/boot-h-riscv64.md) の案 (b) は採らない。
12. `bootloader/include/amd64-kernel-image.h` と同じ種類の riscv64 の約束（kernel の置き場所）を p004 で足す時に、arch ごとの命名と置き場所を揃える（2026-10-18 ユーザー「名前は riscv64 の時に揃える」）。
