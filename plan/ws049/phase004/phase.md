<!-- awesome-plan project=zedbsd record=ws049p004 -->

# ws049-p004: OperationRegion・Field・IndexField・BankField・BufferField、region の handler と `_REG`

Phase ID: `ws049-p004`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

`aml-field.c` の region と field の読み書きを確かめ、`_REG` を足す。受け入れ（[design.md](../design.md) §11）:

- region の ASL の試験が aml-host と acpiexec の両方で全部成功する。
- q35・pc・PRIMERGY の全 device の `_HID`・`_CID`・`_UID`・`_ADR`・`_STA`・`_CRS` の評価の結果が acpiexec と一致する。

## 変更

- `aml-field.c`: `drv_acpi_region_connect_all()`（public）と、その後の `drv_acpi_region_install()` で、space の region ごとに同じ scope の
  `_REG(space, 1)` を一度だけ呼ぶ。SystemMemory・SystemIO は常に有るので呼ばない（ACPI 6.5 §6.5.4）。PCI_Config・EmbeddedControl ほかは呼ぶ。
- `aml-osi.c`: 省略可能な機能の文字列（Module Device、Processor Device、3.0 Thermal Model、3.0 _SCP Extensions、Processor Aggregator Device）を
  外した。それを扱う driver ができるまで名乗らない（ACPICA の既定と同じ。PRIMERGY の `\_SB.PRAD._STA` が acpiexec と違った原因）。
- harness: `--reg`（読み込みの後に space を connect する）。

## 試験

- `plan/ws049/tests/asl/region.asl`: SystemMemory の byte・word・dword・any の access、同じ byte を別の Field で読む、nibble と byte をまたぐ field の
  Preserve、integer より広い field（buffer）、WriteAsOnes・WriteAsZeros、IndexField（index を書いてから data）、BankField（bank を選んでから）、
  `_BBN`・`_ADR` から決まる PCI_Config、DataTableRegion（DSDT 自身の signature と長さ）、method の引数を番地にする region。
- `plan/ws049/tests/asl/regmethod.asl`（`regmethod.args` で harness に `--reg`）: EmbeddedControl の `_REG(3, 1)` が呼ばれ、SystemMemory のものは
  呼ばれない。PCI_Config の `_REG` は zedBSD だけが呼ぶ（acpiexec は呼ばない）ので、MAIN では確かめず harness で確かめた（`REGP = 1`）。
- `plan/ws049/tests/compare-devices.py NAME TABLE...`: aml-host `--devices` と同じ path を同じ順に acpiexec（`-l -di -dr`、標準入力で命令を渡す）で
  評価し、結果を比べる。`compare-corpus.sh` も namespace と device の評価の両方を比べるようにした。

## 結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `plan/ws049/tests/run-asl.sh` | 8 file（p003 の 6 と region、regmethod）全部 passed（aml-host と acpiexec） |
| `compare-devices.py q35 …/q35/dsdt.dat` | same（94 の評価） |
| `compare-devices.py pc …/pc/dsdt.dat` | same（87） |
| `compare-devices.py primergy dsdt ssdt1 ssdt2 ssdt3` | same（720。field を読む `_STA`、資源 template を組む `_CRS` を含む） |
| `compare-corpus.sh`（QEMU の test data、namespace と device の評価） | 63 組すべて same（2 分 32 秒） |
| `style-check.py`（interpreter と harness） | 0 件 |
| `make -C plan/ws049/tests kernel-check` | warning 0 |

## 未実施・制限

- field の Lock rule（`_GL_` の取得）は未実装（p005 で Global Lock と一緒に）。今は単一の interpreter lock の下で意味が同じ。
- BufferAcc（GenericSerialBus・SMBus・IPMI・PCC）は ENOTSUP で失敗させる。要る WS で足す。
- PCI_Config の bus は root bridge の `_BBN` だけで決める（PCI-to-PCI bridge の二次 bus 番号をたどらない）。bridge の下の device の
  PCI_Config region は、kernel の組み込み（p006）で PCI の列挙の結果から決め直す。
- 対象機の EC は p007・p008。
