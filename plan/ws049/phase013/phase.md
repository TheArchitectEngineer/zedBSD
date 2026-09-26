<!-- awesome-plan project=zedbsd record=ws049p013 -->

# ws049-p013: 診断の口 `/dev/acpi`（p006 のうち承認なしでできる部分）

Phase ID: `ws049-p013`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27。kernel の上での実行は p006 の後で、未実施）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

kernel の中の namespace と評価の結果を user から確かめる口を用意する（design §7 の「診断」）。受け入れ:

- namespace と評価の結果を文字にする処理を kernel と host の harness で共有し、harness の出力が以前と byte で同じこと
  （acpiexec との比較が kernel の出力にもそのまま使える）。
- `/dev/acpi` の cdev を kernel の flag で warning 0 で compile でき、`drv_acpi_attach()` の最後で登録すること。
- 新しい file の style-check が 0。

## 設計

UAPI（ioctl の番号・構造体）を足さないために、text だけの口にした（design §12 #4 の案。形はユーザーの判断）。

- `open` ごとに文字の buffer を持つ。
- `write` しないで `read` すると namespace の一覧（1 行 1 node。harness の `--dump` と同じ形）。
- path（例 `\_SB.PCI0._CRS`。末尾の改行は無視、511 byte まで）を `write` すると評価し、続く `read` で
  `PATH = VALUE` の 1 行（harness の `--eval` と同じ形）。引数は渡せない（引数の無い method と名前の値だけ）。
- 使い方: `cat /dev/acpi`、`sh -c 'exec 3<>/dev/acpi; printf "\\_SB.PCI0._CRS" >&3; cat <&3'`。
- device 番号は `0x000B0000`（使用中の major は 1・2・3・9（gpu）・0xA（audio）。他の branch と重ならないかは merge で確かめる）。

## 変更

| file | 内容 |
| --- | --- |
| `src/drivers/acpi/acpi-text.[ch]` | 新規。伸びる文字の buffer（`drv_acpi_text_init/release/printf`）、`drv_acpi_text_namespace`、`drv_acpi_text_evaluate`。harness の出力の処理をここへ移した |
| `src/drivers/acpi/acpi-dev.c` | 新規。`/dev/acpi`（open・close・read・write）。open ごとの mutex（`LOCK_RANK_DEVICE`） |
| `src/drivers/acpi/acpi-kern.c` | `drv_acpi_attach()` の最後で `drv_acpi_device_register()` |
| `plan/ws049/tests/aml-host.c`・`Makefile` | `--dump`・`--eval` を `acpi-text.c` で出す（自前の出力の関数 8 つを削除） |

## 検証（2026-09-27、host）

| command | 結果 |
| --- | --- |
| `make -C plan/ws049/tests kernel-check` | warning 0。最大の frame は `namespace_visitor` 584 B、`acpi_write` 552 B（path の 512 B）|
| `make -C plan/ws049/tests` | warning 0 |
| `plan/ws049/tests/compare-namespace.sh q35 build/ws049/tables/q35/dsdt.dat` | same（244 nodes） |
| `plan/ws049/tests/compare-namespace.sh pc build/ws049/tables/pc/dsdt.dat` | same（338 nodes） |
| `plan/ws049/tests/compare-namespace.sh primergy …/dsdt.dat …/ssdt*.dat` | same（9768 nodes） |
| `python3 plan/ws049/tests/compare-devices.py q35 build/ws049/tables/q35/dsdt.dat` | same（94 evaluations） |
| `python3 plan/ws049/tests/compare-devices.py --methods pc build/ws049/tables/pc/dsdt.dat` | same（22 evaluations） |
| `python3 plan/ws049/tests/run-asl.py` | 14 passed, 0 failed |
| `python3 plan/tools/style-check.py src/drivers/acpi/*.c src/drivers/acpi/*.h include/drivers/acpi/acpi.h plan/ws049/tests/*.c plan/ws049/tests/*.h` | 0 findings |

## 制限と残り

- `/dev/acpi` を kernel の上で開く確認は**未実施**（p006 の HAL の差分が無いと `drv_acpi_attach()` は RSDP を得られず、登録の前に ENODEV で戻る）。
  p006 で guest の中で `cat /dev/acpi` を harness の `--dump` と比べる。
- 形（text の read/write か、ioctl か、`/dev/system` への統合か）と device 番号はユーザーの判断（ws.md「人間の判断が要る点」）。
