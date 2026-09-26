<!-- awesome-plan project=zedbsd record=ws049p015 -->

# ws049-p015: ECDT（`_REG`・`_INI` の前の Embedded Controller）

Phase ID: `ws049-p015`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27。kernel の上での実行は p007 の後で、未実施）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

p011 の残り（ECDT を扱わない）を埋める。laptop の firmware は `_INI` や `_REG` の中で EC の field を読むことが多く、ECDT はそのために
名前空間の初期化の前の EC を示す（ACPI 6.5 §5.2.16）。受け入れ:

- ECDT があれば、table の読み込みの後・`_REG` の前に EC の address space の handler を入れ、EC の `_REG` が他の空間と一緒に 1 回だけ
  走り、`_INI` から EC を読めること。後の `drv_acpi_ec_attach()` は device の GPE と query を足し、`_REG` を二度走らせないこと。
- 壊れた ECDT（短い、signature 違い、EC_ID の終端が無い、SystemIO でない、port が 0）を拒み、DSDT の EC で従来どおり動くこと。
- 新しい code の style-check が 0、kernel の flag で warning 0。

## 設計と変更

| file | 内容 |
| --- | --- |
| `include/drivers/acpi/acpi.h` | `int drv_acpi_ec_ecdt(const uint8_t *ecdt, size_t length)` |
| `src/drivers/acpi/acpi-ec.c` | `drv_acpi_ec_ecdt`: 検査、EC_CONTROL・EC_DATA（GAS、SystemIO だけ）・GPE_BIT・EC_ID を読み、EC_ID の device があれば `_GLK`、handler を入れる。`drv_acpi_ec_attach` を `attach_device` に分け、interpreter に入ったまま device を探して `_CRS`・`_GPE`・`_GLK` を読む。ECDT と `_CRS` の port が違えば `_CRS` を使って log（ECDT が名前空間と食い違う firmware がある）。ECDT が入れた handler は入れ直さない（`_REG` は 1 回）。device が無ければ ECDT の空間だけ残し query は無し。`read_ports` は結果を引数で返す |
| `src/drivers/acpi/acpi-kern.c` | `start_ecdt`: `drv_acpi_initialize_objects()` の後・`drv_acpi_region_connect_all()` の前に、root table の一覧から ECDT を探して `drv_acpi_ec_ecdt` |
| `plan/ws049/tests/aml-host.c` | `--ecdt FILE`（kernel と同じ位置で呼ぶ）。`--ec-ram` の preset を AML の実行の前へ移した（`_INI` が読むため） |
| `plan/ws049/tests/asl/support/ecdt.asl` | iasl の data table の形の ECDT（port 0x66/0x62、GPE 0x16、`\_SB.EC0`） |
| `plan/ws049/tests/asl/ecdt.*`・`ecdt-check.py` | 新しい試験（harness だけ）と、壊れた ECDT の試験 |

## 検証（2026-09-27、host）

| command | 結果 |
| --- | --- |
| `python3 plan/ws049/tests/run-asl.py ecdt` | passed（`_REG` 1 回、`_INI` が EC の 0x3C を読む、query 0x42 で `_Q42`） |
| 同じ table を `--ecdt` 無しで（対照） | `MAIN returned 0x3`（`_INI` の時に EC の空間が無い。試験が ECDT の経路に依ることの確認） |
| `python3 plan/ws049/tests/ecdt-check.py`（壊れた ECDT 6 種と、byte を変えた ECDT 500 個。seed 7、ASan・UBSan） | 6 種とも passed: short・unterminated・signature・zero-port は EINVAL、SystemMemory は ENOTSUP で拒み、DSDT の EC が attach。EC_ID の device が無い ECDT は受け入れ、walk で見つけた EC0 に移って MAIN も通る。500 個は失敗 0（終了 code 0 が 107、1 が 393） |
| `python3 plan/ws049/tests/run-asl.py` | 16 passed, 0 failed |
| `fuzz.py --iterations 300 --seed 5`（q35 DSDT、ecdt・events・glock の aml） | 失敗 0 |
| `compare-namespace.sh pc`・`compare-devices.py --init pc` | same（338 nodes・87 evaluations） |
| `make -C plan/ws049/tests all release stack kernel-check` | warning 0 |
| `python3 plan/tools/style-check.py src/drivers/acpi/*.[ch] include/drivers/acpi/acpi.h plan/ws049/tests/*.[ch]` | 0 findings |

## 制限と残り

- kernel の上での確認は未実施（QEMU の firmware は ECDT を出さない。対象機の table（p008）で ECDT の有無を確かめる）。
- ECDT の port が誤っている firmware では、`_CRS` で直るまでの間の EC の transaction は 0.5 秒ずつの timeout になる（止まりはしない）。
- ECDT の EC_ID の device が後で見つからない場合、query（`_Qxx`）は走らない。
