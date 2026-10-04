<!-- awesome-plan project=zedbsd record=ws049p009 -->

# ws049-p009: 規約の全文の確認と最終の確認

Phase ID: `ws049-p009`
Parent: [WS049](../ws.md)
Status: in-progress（2026-10-04。全文のレビューは済み、指摘の適用は未着手。P1 generation16 が引き継ぐ）
Phase disposition: normal
Queue: q693 / q693-i01（P1）

## 目的と受け入れ

WS049 の全 source（`src/drivers/acpi/*.[ch]`、`include/drivers/acpi/acpi.h`、`plan/ws049/tests/*.[ch]`、WS049 が足した HAL・platform の部分
（`src/hal/amd64/bsp-pcat/acpi.c`・`boot.c` の RSDP の handoff、`src/kern/platform/pcat.c` の `drv_acpi_attach()`）と p016 の `src/drivers/pci/pci.c`
の変更）を `plan/coding-style.md` の全文で確かめ、違反を直す。build（warning 0）、host の試験、QEMU の回帰（T1）。

## 経過（2026-10-04、P1 generation15）

1. `python3 plan/tools/style-check.py` を全 source に: acpi-event.c の 3 件（blank-after-brace 2、call-in-condition 1）だけ → 修正（`read_soft_off`）。
2. 手作業の全文のレビューを 5 つの読みのみのサブエージェントで分担（g1: aml-operator・object・sync・osi・thread、g2: aml-eval・define・skip・stream、
   g3: aml-field・table・namespace・internal.h・os.h、g4: acpi-*.c・h と acpi.h、g5: tests と HAL・pcat の WS049 の部分）。指摘の全件は
   [findings.md](findings.md)（数百件と実害の候補 15 件）。
3. 同じサブエージェントに適用を頼んだが、auto-mode の判定で編集が拒否された（起動の指示が「読みのみ」だったため）。g3 の 1 件（aml-field.c の
   `PCI_HEADER_*` の macro のコメント 2 行）だけ適用された。P1 は別の経路で同じ編集をせず Q1 に返した。
4. 2026-10-04 user（Q1 経由、クリックの回答「承認、全部適用」）: 規約の全件と実害の候補の全部を **P1 自身が適用**する。修正に孫のサブエージェントを使わない。
   → 規則どおり generation15 はラップアップし、generation16 が適用する。

## 再開点（generation16）

- [findings.md](findings.md) を上から適用する（行番号は内容で探し直す）。適用しないもの（`ULL` などの C99 の慣行、designated initializer、static の
  複数行の header comment）は findings.md の冒頭に書いた。実害の候補は **[実害]** の印。
- 確認: `python3 plan/tools/style-check.py` で 0（pci.c は新しい指摘 0）、`make -C plan/ws049/tests`（ASan・UBSan）、`python3 plan/ws049/tests/run-asl.py`（18 passed）、
  `plan/ws049/tests/check-latitude5330.sh`、`compare-firmware.sh latitude5330 …`（same）、`make -C plan/ws049/tests kernel-check`、
  `make ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix`（warning 0）、`sh plan/ws014/tests/run-pci-service-lifecycle-host.sh`。
- QEMU の回帰は p007 の再依頼（下の T1-086 の切り分けの後）とまとめて Q1 経由で T1 へ: `guest-compare.sh`（namespace same）、boot test。
