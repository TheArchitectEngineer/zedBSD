<!-- awesome-plan project=zedbsd record=ws256-p021 -->

# ws256-p021 PCI core の host の INTx の hook

Status: cleared（2026-10-19 Q1 の判定、T1-727: riscv64 virt の usb-kbd で login と uname -m PASS（hook の無い道）、amd64 の xHCI と NVMe の起動は login prompt まで PASS（nvme0n1・input の ls は boot-test の道具の不具合で未確認、Q1 が直した）、Pi 4 の build warning 0。host 試験 PASS。PLDA の hook の道の実機は p020）

- 親: [WS256](../ws.md)。Queue: q1278-i01（P2）。
- 範囲: [design-peripherals.md](../design-peripherals.md) §3.5 の p021（改訂 4、レビュー 3 Minor 3・4・10・11 を当てた形）。`include/drivers/pci/pci.h` の `drv_pci_bus_ops` に省略可能な INTx の 4 つの hook、EOI を送らない `intx_line_run`、線の鍵を (host, vector)、lock の順と drain・戻り値。PLDA の host（`pci-plda.c`）は p020 で、ここでは書かない。HAL の API（hal.h）は変えない。
- 完了の条件: host の試験（design §3.5 の 1〜6）が通り、amd64・arm64（Pi 4）・riscv64（virt）の vmunix が warning 0、T1 の回帰（下）が PASS、Q1 が合格とする。

## 実装（2026-10-19、P2）

- worktree `/home/awe/zedBSD-worktrees/p2`（agent/p2）、main 96ace9cfc の上（改訂 4 の commit aa4b92287 の後）。
- `include/drivers/pci/pci.h`: `typedef void (*drv_pci_intx_run_t)(void *)` と、`drv_pci_bus_ops` の末尾に `intx_establish`・`intx_disestablish`・`intx_mask`・`intx_unmask`（4 つとも設定するか、どれも設定しない）。呼ばれる文脈・lock・戻り値を struct の comment に書いた。designated initializer の既存の host（pci-pcat.c・pci-brcmstb.c・pci-ecam-fdt.c など）は NULL のまま。
- `src/drivers/pci/pci.c`:
  - `struct pci_intx_line` に `ops`・`host`（hook の無い host は両方 NULL = 今の interrupt controller の番号の空間）。`find_intx_line(ops, host, irq)` が (ops, host, vector) で引く。
  - `intx_host_hooks`: bus の ops の hook が 0 個なら NULL・NULL、4 個なら ops・host、1〜3 個なら EINVAL（establish が失敗として返す）。
  - `intx_line_install`・`intx_line_mask`・`intx_line_unmask`・`intx_line_remove`: hook があれば host、無ければ `kern_irq_register`・`_mask`・`_unmask`・`_unregister`（今の道と同じ呼び出し）。
  - `intx_line_run`: 今の `pci_intx_dispatch` の本体（`dispatching` の増減と handler の順の呼び出し）。EOI を送らない。`pci_intx_dispatch` は `intx_line_run` → `kern_irq_send_eoi` の 1 回だけ（hook の無い道は前と同じ）。
  - `establish_intx`・`disestablish_intx` は helper を通るだけで、順（install → publish → unmask、mask → lock を外して remove、失敗なら removing を 0 に戻して unmask、EBUSY 以外は EIO）は前と同じ。
  - **design からの 1 点の決め**: `intx_establish` は pin を mask のまま表に入れ、core が線を publish した後に `intx_unmask` を呼ぶ（hook の無い道の register → publish → unmask と同じ順。design §3.5 の 3 に追記した）。
  - 変えた関数は coding-style の形に書き直した（説明の comment、`Failed:`/`Succeeded:`、critical section の空行、条件の中の呼び出し無し）。動作は hook の無い道で変わらない。
- 試験の runner は mktemp と rm の trap を持たない（出力の directory は呼ぶ側が渡す。レビュー 3 Minor 4）。

## host の試験

`plan/ws256/tests/pci-intx-hook/run.sh OUTDIR`（`pci-intx-hook-test.c` が本物の pci.c と fake の host を link、通常と ASan/UBSan の 2 回）:

1. 2 本が同時: INTA・INTB → 両方の handler が 1 回ずつ、W1C 1 回（handler の後）、EOI 1 回、token が戻る。
2. event の bit だけ・status 0・core が mask した pin: handler 無し、W1C と EOI が 1 回ずつ（mask した pin は配らない: レビュー 3 Minor 11）。
3. 2 つの host・同じ pin 1: 線が 2 本、`intx_establish` が各 host で 1 回、A の割り込みで B の handler が走らない、B の 2 つの device は B の 1 本の線を共有、B の解除が A に触らない。
4. drain（レビュー 3 Minor 4）: fake の host の ISR を `in_flight++` と `run` の間で止めて (b) 上限（fake の timer の 1000 tick）で EBUSY、要素・pin・core の線が元のまま、再開すると handler が走り EOI 1 回。(c) handler の中からの解除は core の EBUSY で host の `intx_disestablish` は呼ばれない。(a) 止めた ISR を tick 10 で再開すると解除は 0、要素は空、pin は mask。
5. 毎回の handler で host の lock が空いている事（`pthread_mutex_trylock`）。
6. hook の無い道: 2 つの device が 1 本の controller の線、`kern_irq_register` 1 回・unmask 1 回、割り込みで両方の handler と EOI 1 回、最後の解除で mask と unregister が 1 回。他に、hook を一部だけ設定した host は EINVAL で host にも controller にも届かない。

結果（2026-10-19）: `plan/ws256/tests/pci-intx-hook/run.sh build/tmp/p021-hook2` → `pci intx hook: PASS` ×2、`ordinary, ASan/UBSan PASS`。

変異の確かめ（build/tmp/p021-mut、source は変えず写しを変えた）: 線の鍵から host を外す → 試験 3 の `host_b.establish_calls == 1U` で落ちる。`intx_line_run` に EOI を足す（hook に EOI を送らせる、BL1 の誤り）→ 試験 1 の EOI の数で落ちる。

## build（2026-10-19、BUILD は自分の worktree の build/p021-*）

| 構成 | command | 結果 |
| --- | --- | --- |
| riscv64 virt（PCI・xHCI・NVMe 入り） | `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk BUILD=build/p021-riscv64 -j16 vmunix` | exit 0、warning 0・error 0（log build/tmp/p021-riscv64.log）、pci.c を compile |
| amd64 | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p021-amd64 -j16 vmunix` | exit 0、warning 0・error 0（build/tmp/p021-amd64.log） |
| arm64（Pi 4） | `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p021-rpi4 -j16 vmunix` | exit 0、warning 0・error 0（build/tmp/p021-rpi4.log） |

- build の後に pci.h の struct の comment を 1 か所直した（comment だけ、code は同じ）。
- clang-format は host に無く未実施（`.clang-format` はある）。`git diff --check` は 0。
- vf2 の vmunix は p025 の後に build する（p025 の phase.md）。

## 既存の host の試験（レビュー 3 Minor 4）

- `plan/ws004/tests/pci-shared-intx-test.c`・`pci-hcd-irq-teardown-test.c`・`pci-rescan-test.c`・`pcie-capability-test.c` は **p021 の前から link しない**（`kern_irq_send_eoi` 等の kern の API を定義していない古い hal の stub。main 96ace9cfc の pci.c で確かめた）。`msi-source-test.c` も別の理由（`amd64_msi_source_valid`）で link しない。参照は完了した ws004 の Phase（phase009・phase031）と ws004 の tests/README.md、BUG-158 の記録だけで、Master の一覧・未完了の Phase・tests/ のシナリオ・T1 の未実行の依頼には無い → AGENTS.md の整理の基準では削除の候補。**Q1 に削除（と README の参照を外す事）を頼む**（p021 では直さない）。
- `pci-msi-test.c`（`run-pci-msi-test.sh`、BUG-158）は p021 の後も通る（`cc … pci.c pci-msi-test.c` → exit 0）。

## T1 への依頼（Q1 経由、1 つの QEMU の枠で）

tree は agent/p2 の p021 の code の commit 5eaebdff9 を merge した main。PNG と PASS/FAIL、image の commit を返す。

1. **riscv64 virt の USB keyboard（T1-723 の走り B と同じ。レビュー 3 Minor 10: riscv64 は 2 回目の EOI で HAL_FATAL なので `pci_intx_dispatch` の分け方の回帰）**: image は `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img`。`BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img QEMU_EXTRA='-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0' BOOT_PATTERN='login:' BOOT_THEN=$'keys:root\nexpect:[Pp]assword\nkeys:root\nexpect:^root@\\S+:~\\$\nkeys:uname -m\nexpect:^riscv64\\s*$' plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: USB keyboard の打鍵で login し `uname -m` の答え `riscv64` が画面に出る（HAL_FATAL が無い）。
2. **amd64 の xHCI と NVMe（MSI-X の道と、hook の無い INTx の道の回帰）**: image は `config/ci/config-amd64.mk`。`QEMU_EXTRA='-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -drive if=none,id=nv,file=<T1 の dir の 64M の raw>,format=raw -device nvme,drive=nv,serial=p021'`、`BOOT_PATTERN='login:'`、`BOOT_THEN` で login して `ls /dev`。合格: login prompt、`ls /dev` の答えに `nvme0n1` と `input` が出る（xHCI・NVMe が attach した）。
3. **Pi 4 の build**: `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk -j16 vmunix` が rc 0・warning 0（QEMU の raspi4b に PCIe は無いので Pi 4 の xHCI の INTx の道は実機だけ。実機の Pi 4 の USB keyboard の login は Q1 がユーザーの試験の一覧に足すかを決める）。

## 残り

- T1 の結果と Q1 の判定。
- Q1 への依頼: 上の ws004 の 5 つの古い試験の削除の判断。
- PLDA の host の hook の実装（p020）。drain の上限 1 秒の timer の API（`kern_ms_to_ticks` など）は p020 で選ぶ。
