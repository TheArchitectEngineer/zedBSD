<!-- awesome-plan project=zedbsd record=ws049p005 -->

# ws049-p005: 同期と OS の口（Mutex・Event・Sleep・Notify・`_OSI`・Load 系・`_INI`、stack の予算）

Phase ID: `ws049-p005`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

受け入れ（[design.md](../design.md) §11）: 同期と Load の ASL の試験。PRIMERGY の `_INI` の全体が acpiexec と同じ結果になる。最大の stack の深さの記録。

## 変更

- `aml-thread.c`（新規）: interpreter への入口と出口。OS の lock（`drv_acpi_os_lock`・`unlock`・`lock_owned`、`aml-os.h` に追加）を一番外の入口で取り、
  入れ子の入口（driver の callback の中の評価）は同じ thread に合流する。`drv_acpi_sleep()` は lock を放して眠り、取り直す（Sleep、Acquire と Wait の待ち）。
  stack の予算は入口の thread の frame から測る（`drv_acpi_os_thread()` は不要になり外した）。
- `aml-sync.c`: AML の Mutex を thread ごとの所有と SyncLevel の規則（ACPI 6.5 §19.6.2: 今の level より低い mutex の Acquire と、順序の違う
  Release は誤り）にした。thread が持ったまま評価を終えた mutex は出口で解放する。Global Lock（`\_GL_`）と、Lock rule の field の読み書きの
  `_GL_` の取得（`drv_acpi_global_lock()`）。
- `aml-eval.c`: Serialized の method は method ごとの mutex（method の SyncLevel）を取って走る（同じ thread の再帰は深さを数える）。
- `aml-table.c`: `Load`（region・field・buffer から。結果は ACPI 6.4 の boolean: 成功は Ones）、`LoadTable`（`drv_acpi_os_table()` で firmware の
  table を探す。無ければ 0、読み込み済みなら誤り。RootPath と ParameterPath/ParameterData）、`Unload`（その table の node を全部消す）、
  `drv_acpi_initialize_devices()`（`\_INI`、`\_SB._INI`、`_STA` による device の `_INI` の walk。ACPI 6.5 §6.5.1）。
- `aml-define.c`: 名前が package より後で定義される要素（QEMU の `_PRT` の `LNKx`）を、読み込みの後の `drv_acpi_initialize_objects()` と、
  driver が参照を読むときに解決する（書かれた scope から）。
- harness: `--init`、`--methods`（引数の無い全 method を namespace の順に評価）、`--notify PATH`、`--dynamic FILE`（LoadTable の table）、
  `--shared-pci`（acpiexec と同じく全 function で一つの PCI config の疑似）。疑似の address space を space ごとに分けた（前は memory と I/O が同じ番地を共有していた）。
- 試験の道具: `run-asl.py`（`run-asl.sh` を置き換え。`.args`・`.evals`・`.output`・`.harness-only` と `asl/support/` の table）、
  `compare-devices.py` の `--methods` と `--init`。

## 試験

| file | 確かめること |
| --- | --- |
| `sync.asl`（`.evals`） | Acquire の再取得、level の上昇と逆順の Release、`\_GL`、Event の Signal/Wait/Reset と時間切れ、Serialized の再帰、Timer と Sleep/Stall。`BADO`（低い level の Acquire）と `BADR`（持たない mutex の Release）は誤り、`LEAK` が残した mutex は評価の後に空く |
| `init.asl`（`--init`） | `\_SB._INI` が先、present の device は `_INI` の後に子、present でも functioning でもないものは子ごと飛ばす、functioning だけのものは子だけ。順は `"S1ac4"` |
| `load.asl` | buffer からの Load、operation region（memory）からの Load、結果の Ones、読み込んだ method と device |
| `loadtable.asl`（harness だけ） | 無い table は 0、RootPath と ParameterPath、Unload で名前が消える、再読み込み、二重の LoadTable は誤り（`.evals`） |
| `notify.asl`（`--notify`） | Notify が handler に届く（`.output`）、handler の無い node は捨てる、`_OSI` |

## 結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `plan/ws049/tests/run-asl.py` | 13 file 全部 passed（loadtable は harness だけ、ほかは aml-host と acpiexec の両方） |
| `compare-devices.py --methods` q35 / pc / PRIMERGY | same（38 / 22 / 2024 の評価。引数の無い全 method） |
| `compare-devices.py --init` q35 / pc / PRIMERGY | same（94 / 87 / 720。`_REG`・`_STA`・`_INI` の後の device の評価。acpiexec は通常の初期化） |
| `compare-corpus.sh`（QEMU の 63 組、namespace・device・全 method・初期化の後の device） | 63 組すべて same（時間の大半は acpiexec の While の時間切れ `-to 1` の待ち） |
| stack（`make stack` の clang -Os の host 版、`--stack`） | PRIMERGY の初期化と全 method: 3648 byte。`control.asl`（`FACT (10)` の 10 段の再帰）: 8768 byte。gcc -O2 版では PRIMERGY 4592 byte |
| `style-check.py` | 0 件 |
| `make -C plan/ws049/tests kernel-check` | warning 0。LoadTable の要求（文字列 5 つ）は heap に置き、再帰の経路の frame を 776 byte から減らした |

## stack の予算の判断

実機の table（PRIMERGY）の最深は 3.6 KiB、再帰する ASL の試験で 8.8 KiB。kernel の stack は 16 KiB。kernel の予算は 8 KiB を既定の案とし、
AML は入口の呼び出し元の stack の残りが十分な所（driver の probe や kernel thread）からだけ評価する。対象機の table（p008）で測り直す。
予算を超えた評価は `E2BIG` で失敗する（overflow しない）。

## 未実施・制限

- kernel への組み込み（p006、HAL の承認待ち）。build・boot test は kernel image を変えていないので未実施。
- FACS の Global Lock の hardware の手順（firmware と取り合う）は p007 の SCI と一緒に。今は `_GL_` を AML の mutex として扱う。
- Notify は同期で handler を呼ぶ。kernel では handler が AML を再入しない前提（p007 で遅延の配送を決める）。
- LoadTable の table は `drv_acpi_os_table()`（kernel は XSDT の一覧から、p006）。
