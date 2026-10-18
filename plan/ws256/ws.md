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

1. QEMU の virt（riscv64）で、UEFI の firmware（U-Boot の EFI か edk2）→ zedBSD の loader → kernel の順に起動し、UART の console で login できる。SMP 4 core、timer と PLIC の割り込みが動く。
2. VisionFive 2 の実機で、板の SPL＋OpenSBI＋U-Boot の EFI → zedBSD の loader → kernel で、UART の console に login できる（SMP 4 core、timer、PLIC）。root は initrd か SD。
3. HAL の riscv64 の部分（`include/hal/arch/riscv64.h` などの API の追加）は、差分ごとのユーザーの事前承認の後に入れる（[Guardrail](../guardrail.md)）。
4. 変えた C のコードが plan/coding-style.md の全文に合う（最後の規約の Phase）。

## Phase（案）

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 設計: JH7110・U74 の CPU（RV64GC、Sv39）・OpenSBI の SBI（timer・IPI・HSM・console）・PLIC・CLINT・UART（8250 の DW）・U-Boot の EFI の調べ、HAL の riscv64 の API の差分の案（承認の前は plan に置くだけ）、loader の riscv64 版、build（platform/riscv64・config）。code は書かない | planning |
| p002 | toolchain: LLVM に RISCV の target を足す（Q1） | cleared（2026-10-18 Q1: toolchain/llvm/llvm.mk の LLVM_TARGETS_TO_BUILD と config の identity に RISCV。共有の build/llvm を source から作り直し（log build/tmp/llvm-riscv-20261018.log、FAILED・error 0）、`clang --print-targets` に riscv64、rv64gc の object を確認、config-amd64.mk の vmunix の build と check PASS。toolchain は lock し直した。残り: install の identity は version・patch だけで target を見ない、CI の binary cache（rev-0、zedbsd8）に RISCV が無い → CI で riscv64 を build する時に patch level を上げて新しい cache を公開） |
| p003 | HAL の芯を QEMU の virt で: 起動・trap・Sv39 の MMU・SBI・timer・PLIC・UART の console・SMP | planning |
| p004 | loader の riscv64 版（UEFI）と QEMU の UEFI の firmware での起動 | planning |
| p005 | VisionFive 2 の BSP（bsp-visionfive2）: DTB・UART・clock・reset の最小限、実機の login | planning |
| p006 | 規約の全文の見直し | planning |

## 未決（Q1 がユーザーに聞く）

- 板の版（v1.2A・v1.3B）と memory の大きさ（2・4・8 GB）。
- 実機の試験の接続（USB-UART・電源の制御・LAN）。AGENTS.md の「serial の log で判定しない」との関係（UART の login の判定の方法）。
- 参照の方針: Linux・FreeBSD・OpenSBI・U-Boot の code の扱い（WS245 と同じく手順は参考にして code・comment は写さず、block ごとに Value(s)/Procedure referenced: を書くか）。
- root の媒体（initrd か SD）、SD・eMMC・GbE・USB・display を後の WS にするか。
