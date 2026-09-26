<!-- awesome-plan project=zedbsd record=ws049p011 -->

# ws049-p011: SCI・GPE・固定 event・EC の核（host の疑似の hardware で試験）

Phase ID: `ws049-p011`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と範囲

p007（SCI・GPE・固定 event・EC・Notify の配送）のうち、kernel image と HAL の差分の承認を待たずにできる部分を分けた Phase。
event と EC の核を書き、host の harness の疑似の hardware（q35 と同じ port）で確かめる。kernel の側は compile だけ確かめる。

受け入れ:

- GPE の `_Lxx`（level）・`_Exx`（edge）が SCI で走り、status が clear され、再び enable される。`_PRW` だけが名指す GPE（wake 用）は
  run time に enable されない。固定の電源 button が handler に届く。ACPI mode への切り替え（SMI_CMD）。
- EC: `PNP0C09` を `_CRS`・`_GPE` から見つけ、EmbeddedControl の region を EC の protocol で読み書きし、`_REG(3, 1)` が走り、
  EC の GPE で query（`_Qxx`）が走り、その中の Notify が driver の handler に届く。
- 規約の検査 0 件、kernel-check warning 0。

## 成果

- `src/drivers/acpi/acpi-event.c`: FADT（`SCI_INT`・`SMI_CMD`・`ACPI_ENABLE`・PM1a/b の event と control・GPE0/1 と長さ・`GPE1_BASE`・flags、
  64 bit の GAS が I/O space ならそちらを優先）を読む `drv_acpi_events_init()`。ACPI mode への切り替え（`SCI_EN` を待つ）、全 event の mask と clear、
  `\_GPE` の `_Lxx`/`_Exx` と `_PRW` の wake GPE の判別、run time の GPE の enable。割り込みの側 `drv_acpi_sci_interrupt()`（register だけを読み、
  発生した event を mask して記録）と thread の側 `drv_acpi_events_process()`（handler と AML、edge は先に・level は後に clear、unmask）。
  `drv_acpi_fixed_event_install()`（固定 button が device として実装された機種では ENODEV）、`drv_acpi_gpe_install()`（driver の C の handler）。
- `src/drivers/acpi/acpi-ec.c`: `drv_acpi_ec_attach()`（`_HID` が EISA ID か文字列の `PNP0C09`、`_CRS` の I/O・固定 I/O の記述子から data と
  command の port、`_GPE`、`_GLK`）。EC の protocol（IBF・OBF を 10 µs ごとに最大 0.5 秒待つ、Read 0x80・Write 0x81・Query 0x84）、
  `_GLK` なら Global Lock の下で。GPE の handler は interpreter に入ってから `SCI_EVT` の間 query を最大 32 回取り、`_Qxx` を走らせる。
- `src/drivers/acpi/acpi-kern.c`: port の I/O（`hal_io_*`）、event の lock（spinlock、割り込みを止めて取る）、SCI（`kern_irq_register`、
  割り込みの側の後に event thread を起こして EOI）、event thread（`kthread_create`、waitq で眠る）、電源 button の記録、`drv_acpi_attach()` から
  event と EC を始める。
- `aml-os.h`: `drv_acpi_os_port_read`・`write`、`drv_acpi_os_event_lock`・`unlock`。
- 名前の探し方: driver の API（`drv_acpi_lookup()`、`drv_acpi_evaluate()` の相対 path）は根へ向かって探さないようにした（ACPICA と同じ。device に
  `_GPE` が無いとき `\_GPE` を拾っていた）。AML の意味の探索（DerefOf の文字列、LoadTable の path、package の名前）は内部の
  `drv_acpi_lookup_path(…, true, …)`。
- harness: `--events`（firmware の FADT か q35 に似せた疑似の FADT）、`--ec`、`--ec-ram A=V`、`--gpe N`、`--power-button`、`--ec-query Q@G`。
  疑似の hardware は `plan/ws049/tests/aml-host-hardware.c`（PM1 と GPE の status は 1 を書いて clear、SMI_CMD で `SCI_EN`、EC は即答）。

## 試験と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `asl/events.asl`（`events.args`: `--reg --events --ec --ec-ram 0x10=0x3c --ec-ram 0x20=1 --notify … --gpe 5 --gpe 7 --gpe 9 --ec-query 0x42@0x16 --power-button`） | passed（harness だけ）: `_L05`・`_E07` が 1 回ずつ、`_L09`（wake だけ）は走らず status が残り enable されない、GPE の status と enable の register の値、EC の `_REG`、EC の byte の読み出し、`_LID`（EC の bit）、16 bit の EC の field の書き込みと読み戻し、`_Q42` と LID への Notify、電源 button |
| QEMU の実際の DSDT と FADT（`make-firmware.py` で並べた q35）に `--events --gpe 2 --gpe 1 --power-button` | `_E02`・`_E01`（CPU と PCI の hotplug の走査）が誤りなく走り、電源 button が届く（QEMU の `ACPI_ENABLE` は 0x02） |
| `run-asl.py` | 14 file 全部 passed |
| PRIMERGY の namespace・初期化の後の device・全 method の acpiexec との比較（名前の探し方の変更の後） | same（9768 / 720 / 2024） |
| `style-check.py`（`src/drivers/acpi/*`、acpi.h、harness と疑似の hardware） | 0 件 |
| `make -C plan/ws049/tests kernel-check` | warning 0 |

## 未実施・制限

- kernel での SCI の割り込み・event thread・EC の実際の動作は p007（p006 の後、QEMU の `system_powerdown` と対象機の EC で）。
- FACS の Global Lock の hardware の手順（firmware との取り合い）は未実装。`_GLK` の EC と Lock の field は `\_GL_` を AML の mutex として取るだけ。
- ECDT（名前空間の前の EC）と、GPE block device（`ACPI0006`）、GPE の番号 256 以上は扱わない。
- SCI の level と極性は MADT の ISO に従う HAL の IRQ の登録に任せる（p007 で確かめる）。
