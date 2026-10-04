<!-- awesome-plan project=zedbsd record=ws049p009 -->

# ws049-p009: 規約の全文の確認と最終の確認

Phase ID: `ws049-p009`
Parent: [WS049](../ws.md)
Status: in-progress（2026-10-04。findings の全件を適用し host の確認は済み。QEMU の回帰（T1）待ち）
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

## 適用（2026-10-04、P1 generation16、ユーザーの承認「承認、全部適用」に基づき P1 自身の session で。孫のサブエージェントは使っていない）

[findings.md](findings.md) を file ごとに上から全件適用した（行番号は内容で探し直した）。適用しないと決めたもの（`ULL`・`unsigned long long`・`%llx`・
`long long`・`%zu`、acpi-dev.c の designated initializer、static 関数の複数行の header comment、findings で「参考（適用しない）」とした default の兼用）は
limitation として残す。共通の型（bare pass-through、成功の return を最後に、段落、allocation と初期化、連鎖の検査、復唱のコメント、非標準の for、
P7 の public comment の最初の文、protocol の flag・counter、critical section の空行）は file 全体に同じ規則で当てた。

### 実害の候補の扱い（全 18 件）

| file | 対処 |
| --- | --- |
| aml-operator.c `match_one` | 挙動は保つ（比較の失敗＝不一致、ACPICA の AcpiExDoMatch と同じ）。ENOMEM を含むことをコメントで明示 |
| aml-operator.c `byte_text` | 常に真の `if (form != STRING_DECIMAL)` を消した |
| aml-sync.c `drv_acpi_thread_end` | release が失敗した mutex は `held_mutex_drop()` で list から外す（release は他の thread が owner のときだけ失敗するので、owner・Global Lock には触れない）。無限 loop の防止 |
| aml-define.c package の要素の名前 | 128 byte を越える名前は "(long name)" の name reference にして Package 全体を失敗させない（`name_reference()`） |
| aml-define.c named field の segment | 4 文字の名前の文字を検査（`segment_valid()`、stream の名前と同じ規則、違反は EIO） |
| aml-eval.c serialize の release | 失敗を log し、body が成功していれば error を返す（return value は release） |
| aml-field.c `connect_visitor` | `run_reg()` の失敗（ENOMEM）を log して walk を続ける。あわせて `run_reg()` の `result` の未初期化の release も直した |
| aml-table.c LoadTable | `table_loaded()` の前に table の長さ（header 36 byte）を確かめる |
| aml-namespace.c `drv_acpi_ns_create` | EEXIST のときだけ `*result` を返す（ENOMEM で未初期化の node を写さない） |
| aml-namespace.c `drv_acpi_ns_init` | root の object・予約名の作成の失敗で `drv_acpi_ns_reset()` して半端な namespace を残さない |
| acpi-kern.c `io_handler` | access の最後の byte が 0xffff を越えるなら EFAULT（64 bit の 2 つ目の dword の port の wrap を防ぐ） |
| acpi-event.c GPE1 の隙間 | `gpe_present()` で GPE0 の終わりと GPE1_BASE の間の番号を初期化・SCI・enable/disable・method の visitor・`drv_acpi_gpe_install`・poweroff で飛ばす。SCI の GPE の走査は block ごとの `record_gpe_block()` に |
| acpi-event.c PM1a control block | 無い FADT は `drv_acpi_events_init` で ENODEV |
| acpi-ec.c `ec.attached` | 誰も読まない flag を消した |
| acpi-dev.c namespace の text | 作成の失敗で text を捨てて作り直す（次の read が後ろに足さない） |
| aml-host-hardware.c `hardware_port` | 副作用の無い `port_modelled()` で判定（EC の data port の probe が OBF を消さない） |
| aml-host.c `action_count` | `OPTION_LIST_MAX` を越える action を拒む（70 個で exit 2 を確認） |
| aml-host.c `evaluate_methods` | walk の失敗（realloc・strdup の ENOMEM）を返す |

### 他の変更

- `plan/tools/style-check.py`（Q1 の委任）: paragraph-comment で critical section の空行（lock の取得の直後・unlock の直前）を除外。src/kern と acpi の 40 file で
  1422→926 件、除外された取得側 201 件を全部確かめた。
- p007 の 2 つの原因（SCI の unmask、event thread の `thread_start`）も同じ期間に acpi-kern.c で直した（[phase007](../phase007/phase.md)）。

### 確認（host、2026-10-04）

| 確認 | 結果 |
| --- | --- |
| `python3 plan/tools/style-check.py src/drivers/acpi/*.[ch] include/drivers/acpi/acpi.h plan/ws049/tests/*.[ch] --summary` | total 0 |
| `make -C plan/ws049/tests`（ASan・UBSan） | warning 0 |
| `python3 plan/ws049/tests/run-asl.py` | 18 passed |
| `sh plan/ws049/tests/check-latitude5330.sh` | 全 table を読み込み、1349 method、既知の TSDD の失敗のみ |
| `compare-firmware.sh latitude5330 dsdt.dat ssdt*.dat` | same（7984 nodes） |
| `compare-corpus.sh`（QEMU の ACPI の test data、acpiexec と比較） | 63 same、0 different |
| `ecdt-check.py` | 500 tables、0 failures |
| `fuzz.py --iterations 300 --seed 9`（5330 の DSDT と control.aml） | 0 failures |
| `make -C plan/ws049/tests kernel-check` | warning 0 |
| `make stack` の host の -Os 版、control.asl の MAIN | 8768 → 8592 byte（減った） |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix`（CONFIG_DRIVER_ACPI=y） | warning 0、include check PASS、amd64 vmunix check PASS |
| `sh plan/ws014/tests/run-pci-service-lifecycle-host.sh` | PASS |
| `git diff --check` | 問題なし |
| QEMU（T1） | **依頼中**（下） |

### T1 への依頼（Q1 経由）

p007 の T1-090 と同じ組: `test-image.sh plan/ws049/tests/config-acpi.mk build/ws049-acpi` → `guest-events.sh`（sci・button・gpe、終了 0、first SCI 1 以上）・
`guest-compare.sh`（namespace same、device の違いは p006 の 11 行）・`boot-test.sh`（login prompt の PNG）。
