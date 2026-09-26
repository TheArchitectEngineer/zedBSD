<!-- awesome-plan project=zedbsd record=ws049p002 -->

# ws049-p002: object・namespace・byte 列・DefinitionBlock の読み込み

Phase ID: `ws049-p002`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行し、主 session が merge のときに記録する）

## 目的と受け入れ

AML の byte 列を読み、DSDT・SSDT を読み込んで namespace を作る。受け入れ（[design.md](../design.md) §11）:

- QEMU q35・pc と host の Fujitsu PRIMERGY の DSDT・SSDT の namespace（全 node の path と型と識別の属性）が acpiexec と一致する。
- ASan/UBSan（LeakSanitizer を含む）で誤り 0。
- 規約の検査（`plan/tools/style-check.py`）0 件。

## 成果（source）

`src/drivers/acpi/`（global symbol は `drv_acpi_`）と `include/drivers/acpi/acpi.h`。interpreter は kernel の header に依存せず、
`<kern/kcrt.h>` と `aml-os.h` だけを使う（host で同じ file を compile する）。

| file | 内容 |
| --- | --- |
| `include/drivers/acpi/acpi.h` | driver 向けの API: 型、address space、読み込み、lookup・walk・path、評価、object の読み出し、region の handler、notify の handler |
| `aml-internal.h` | opcode、object・node・table・frame・評価の文脈・target の型、内部の関数 |
| `aml-os.h` | OS の口（memory、log、stack の予算、sleep・stall・timer、thread の識別、LoadTable の table） |
| `aml-object.c` | object の生成・参照数・Store の複製、integer の幅 |
| `aml-namespace.c` | node の木、NameString の解決（search rule）、生成・削除、alias、path、walk、既定の名前 |
| `aml-stream.c` | opcode、PkgLength、NameString、整数、文字列の読み出し（全て term list の終わりで境界を検査） |
| `aml-eval.c` | term list の実行、If・Else・While・Return・Break・Continue、TermArg、target と Store の規則、method の呼び出し、stack の予算 |
| `aml-define.c` | Name・Alias・Scope・Device・Processor・PowerResource・ThermalZone・Method・External・Mutex・Event・OperationRegion・DataTableRegion・Field・IndexField・BankField・Create*Field、Package・Buffer の literal |
| `aml-skip.c` | 評価せずに TermArg を飛ばす（読み込み時の OperationRegion の引数を初回の使用まで遅らせる） |
| `aml-operator.c` | 式の opcode（p003 で試験する） |
| `aml-field.c` | region の handler と field の読み書き（p004 で試験する） |
| `aml-sync.c`・`aml-osi.c` | Mutex・Event・Notify、`\_OSI`（p005 で試験する） |
| `aml-table.c` | 読み込み、読み込み後の object の準備（region の番地の評価）、reset |

p003〜p005 の範囲の file も、評価器の核に要るのでこの Phase で一通り書いた。その受け入れは各 Phase の試験で行う。

## 試験（host）

- `plan/ws049/tests/aml-host.c`（harness。aml-os.h の host 実装、acpiexec と同じく 0 で始まり書いた値を覚える疑似の address space）と
  `plan/ws049/tests/Makefile`（`make -C plan/ws049/tests` で ASan/UBSan 版、`kernel-check` で kernel と同じ target・flag の compile）。
- `plan/ws049/tests/acpiexec-namespace.py`: `acpiexec -l -di -b namespace` の出力を harness の `--dump` と同じ形へ。
- `plan/ws049/tests/compare-namespace.sh NAME TABLE...`: 両者の namespace を比べる。
- `plan/ws049/tests/compare-corpus.sh`: QEMU の source の `tests/data/acpi`（`~/qemu-pc98`）の全 DSDT（variant ごとの SSDT つき）を比べる。
- table は git に入れない（`build/ws049/tables/`）。SHA256:
  - q35 `dsdt.dat` `8ddaf460f38f43bb45fdca72a6227e98fa295004d1e9e0e8fbce9fe669d2ecfa`、pc `dsdt.dat` `12f4583cfddde171c37d5696c78cd74d283f314ca4567c4a631ba152b887fbb3`
    （QEMU 10.0.11、`qemu-acpi-dump.py`）
  - PRIMERGY `dsdt.dat` `1eeffd33…60ef`、`ssdt1.dat` `c7ea2544…31a6`、`ssdt2.dat` `947360e2…fe7e`、`ssdt3.dat` `ce90be60…c32159`（`sudo acpidump -b`）

## 結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `compare-namespace.sh q35 build/ws049/tables/q35/dsdt.dat` | same（244 node） |
| `compare-namespace.sh pc build/ws049/tables/pc/dsdt.dat` | same（338 node） |
| `compare-namespace.sh primergy dsdt ssdt1 ssdt2 ssdt3` | same（9768 node。region の番地・長さ、field の bit の位置・長さ、processor、alias を含む） |
| `compare-corpus.sh`（QEMU の test data） | 63 組すべて same |
| ASan/UBSan/LeakSanitizer | 誤り 0（PRIMERGY の読み込みと q35 の `--devices` で exit 0） |
| `style-check.py src/drivers/acpi/*.[ch] include/drivers/acpi/acpi.h plan/ws049/tests/aml-host.c` | 0 件 |
| `make -C plan/ws049/tests kernel-check`（`x86_64-unknown-zedbsd`、kernel の flag、`-Werror`） | warning 0。最大の frame は `drv_acpi_node_path` 568 byte、再帰の経路の `drv_acpi_build_package` 264 byte |

比べ方の注意: acpiexec は `-l`（読み込みだけ）で走らせる。`-l` が無いと GPE の初期化の途中で device の `_STA` を評価し、
PRIMERGY の `\_SB.SCK0.LSTA` が 0xFF から 0 に変わる（interpreter の違いではない）。

## 未実施・制限

- kernel への組み込み（p006、HAL の承認待ち）。build・boot test は kernel image を変えていないので未実施。
- 対象機（Latitude 5330）の table は未取得。
- namespace の node の削除（method の後始末、将来の Unload）で、他の object が指す node を消すと dangling になる（参照数を node に持たない）。
  実機の table で問題になるかを p005 で確かめる。
