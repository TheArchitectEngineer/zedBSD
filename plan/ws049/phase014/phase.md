<!-- awesome-plan project=zedbsd record=ws049p014 -->

# ws049-p014: FACS の Global Lock を firmware と取り合う

Phase ID: `ws049-p014`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27。kernel の上での実行は p006・p007 の後で、未実施）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と受け入れ

p005・p011 の残り（`\_GL_` を OS の中の AML mutex としてだけ扱っていた）を埋める。Alder Lake の laptop の EC は `_GLK` で
Global Lock を求めることが多く、SMM の firmware と EC を取り合うので、ACPI 6.5 §5.2.10.1 の手順が要る。受け入れ:

- `Acquire(\_GL_)`・Lock rule の field・EC の `_GLK` が FACS の lock の dword を lock 付きの交換で取り、firmware が持っていれば
  pending を立てて待つ（timeout を守る）。解放で pending が立っていれば PM1_CNT の GBL_RLS で firmware に知らせる。
- firmware が解放のときに立てる GBL_STS（固定 event bit 5）を有効にし、SCI で消す。
- host の疑似の firmware で、取る・firmware が求める・GBL_RLS・timeout・待って取る・GBL_STS の一連を試験する。

## 設計と変更

| file | 内容 |
| --- | --- |
| `include/drivers/acpi/acpi.h` | `int drv_acpi_global_lock_attach(volatile uint32_t *word)`（driver 自身の header。HAL の API ではない） |
| `src/drivers/acpi/aml-sync.c` | `hardware_acquire`（owned を立て、owned だったら pending も立てる。取れたのは pending が立たなかったとき）、`hardware_release`（両方を消し、pending だったかを返す）。`drv_acpi_mutex_acquire` は owner が無く、`\_GL_` なら hardware も取れたときに取る（取れなければ 1 ms ずつ眠って timeout まで再試行）。最後の Release で hardware を解放し、firmware が待っていれば `drv_acpi_events_global_release()` |
| `src/drivers/acpi/acpi-event.c` | `drv_acpi_events_init` で GBL_EN を有効にする。`drv_acpi_events_global_release`（PM1a/b_CNT に GBL_RLS。SLP_EN は落として書く。event lock の下） |
| `src/drivers/acpi/acpi-kern.c` | `start_global_lock`: events が始まった後、FADT の FACS の address の page を `hal_space_map_device` で写し（amd64 では uncached。lock 付きの交換はそのまま効く）、signature と長さを確かめて attach。失敗は log して続ける |
| `plan/ws049/tests/aml-host-hardware.[ch]`・`aml-host.c` | `--global-lock`: 疑似の FACS の dword と、眠るたびに動く疑似の firmware（OS が持つ lock を一度求める、持つ lock を離し pending なら GBL_STS、GBL_RLS で取る）。MAIN の後に lock の値と SCI を出す |
| `plan/ws049/tests/asl/glock.*` | 新しい試験（harness だけ。acpiexec にこの firmware は無い） |

待ちは GBL_STS の割り込みで起こさず、1 ms ごとの再試行にした（SCI を取り逃しても止まらない。AML の mutex の待ちと同じ方式）。

## 検証（2026-09-27、host）

| command | 結果 |
| --- | --- |
| `python3 plan/ws049/tests/run-asl.py glock` | passed。出力: `FIRMWARE waits` → `FIRMWARE GBL_RLS` → `FIRMWARE took` → `FIRMWARE released …, GBL_STS` → `MAIN passed` → `GLOBAL-LOCK 0x0` → `SCI pending` → `SCI none` |
| `build/ws049/host/aml-host --events --main build/ws049/asl/glock.aml`（lock を attach しない対照） | `MAIN returned 0x3`（firmware が持つ lock の timeout を確かめる check 3 が落ちる。試験が hardware の手順に依ることの確認） |
| `python3 plan/ws049/tests/run-asl.py` | 15 passed, 0 failed |
| PRIMERGY の DSDT・SSDT 3 つを `--reg --init --events --global-lock --methods` と `--reg --init --methods` で | 両方 rc 0、method の結果（2024 件）は同じ |
| `compare-namespace.sh q35`・`compare-devices.py --methods --init q35`・`compare-firmware.sh --rsdt q35 …` | same（244 nodes・38 evaluations・244 nodes） |
| `fuzz.py --iterations 300 --seed 4`（q35 DSDT、glock・sync・events の aml） | 失敗 0 |
| `make -C plan/ws049/tests kernel-check`・`all`・`release`・`stack` | warning 0。`aml-host-Os --stack --events --global-lock --main glock.aml` は 1632 B |
| `python3 plan/tools/style-check.py src/drivers/acpi/*.[ch] include/drivers/acpi/acpi.h plan/ws049/tests/*.[ch]` | 0 findings |

## 制限と残り

- kernel の上（QEMU の FACS）での確認は未実施（p006 の HAL の差分の承認の後、p007 で）。実機は未実施（p008）。
- lock を attach するのは event を始めた後（`_REG`・`_INI` の後）。それまでの `_INI` の中の Lock の field は OS の中の mutex だけで守る。
  ACPICA は ACPI mode と Global Lock を `_INI` の前に用意するので、p007 で順序を見直す（`_PRW` の評価が EC の `_REG` の前に来ないようにする必要がある）。
- 64 bit の FACS の lock（`X_FIRMWARE_CTRL` で 4 GiB より上）も同じ経路で写す。FACS の version・flags（S4BIOS など）は読まない。
