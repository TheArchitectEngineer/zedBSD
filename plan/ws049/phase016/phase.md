<!-- awesome-plan project=zedbsd record=ws049p016 -->

# ws049-p016: 橋の下の機能の PCI_Config の region の bus

Phase ID: `ws049-p016`
Parent: [WS049](../ws.md)
Status: in-progress（2026-10-04。実装と host の試験は済み。QEMU の回帰（p007 と同じ image）と実機の確認待ち）
Phase disposition: normal
Queue: q694 / q694-i01（P1 generation15）。Q1 の技術判断（2026-10-04、p008 の発見から）: 「bridge の secondary bus は新しい ws049-p016（q694）として
P1 の優先に入れ、q678 p007 の後・q693 p009 の前に行う。drv_pci_find_device を bridge の先の bus も引けるようにする `src/drivers/pci/` の最小の変更は
Q1 が委任（API の意味・他の利用者の挙動を変える時は止めて Q1 へ）」

## 目的と受け入れ

[p008](../phase008/phase.md) の発見: PCI_Config の region の機能を決めるとき、bus を `_BBN` だけで決め、親の PCI-PCI bridge の secondary bus を
辿っていなかった（ACPICA の `AcpiHwDerivePciId` は辿る）。5330 の `\_SB_.PC00.RPxx.PXSX.PCCX`・`PEGx.PEGP.PCCX`（31 個、RTD3 の判断の
`PRES`・`PNVM`・`PAHC`・`ISGX` が読む）が 0:0.0（host bridge）を読んでいた。加えて kernel の `drv_pci_find_device()` は root の bus の機能しか
探さず、bus を正しくしても橋の下の機能は「無い」扱いになった。

受け入れ:

1. host bridge（`_HID` か `_CID` が PNP0A03・PNP0A08）から region の device までの間の、`_ADR` を持つ device を上から順に機能として読み、
   header type が bridge（1）か CardBus（2）なら、その下を secondary bus とする。bridge でない機能（header 0、無い機能の全ビット 1）は bus を変えない。
2. `drv_pci_find_device()` が bridge の先の bus の機能も返す（root の bus の機能の答えは変わらない）。
3. host の試験、kernel の build（warning 0）。

## 設計と変更

| file | 内容 |
| --- | --- |
| `src/drivers/acpi/aml-field.c` | `region_resolve_pci()`: `_BBN`・`_SEG` を読んだ後、`pci_root_bridge()`（近い方から先祖の device を見て、自分の `_HID`・`_CID`（整数・文字列・`_CID` の package）が PNP0A03・PNP0A08 のもの）があれば `pci_follow_bridges()`。host bridge と device の間の scope（最大 16 段、それより深ければ辿らない）を上から、自分の `_ADR` を持つものだけ、PCI_Config の handler で header type（0x0E）を読み、bridge なら secondary bus（0x19）へ。読めなければそこで止めて今の bus のまま。host bridge が見つからなければ従来どおり `_BBN` の bus |
| `src/drivers/pci/pci.c` | `drv_pci_find_device()`: root の bus ごとに `find_device_in_tree()`（その bus の機能、次に各 bridge の `subordinate` の bus を再帰で）。比べ方は `find_device_on_bus()` と同じ field ごと（以前は padding を含む `kern_memcmp`。呼び出し側は 0 で埋めているので結果は同じ）。他の利用者は `src/drivers/gpu/i915/display/takeover.c`（0:0.0 を引く、root の bus なので答えは同じ） |
| `src/drivers/acpi/acpi-kern.c` | `pci_handler` の注記を新しい探し方に合わせた（code は同じ） |
| `plan/ws049/tests/asl/pcibridge.*` | 新しい ASL の試験（harness だけ）: RP01（0:1c.0）を bus 5 への bridge にしてから下の PXSX を読むと 5:0.0（`--absent-pci 5:0.0` で全ビット 1）、0:0.0 に書いた値は見えない、bridge でない RP02 の下は bus 0 のまま |

ACPICA との違い: ACPICA は自分の `_ADR` が無い scope で辿るのを止めて失敗を返すが、ここは飛ばして続ける（table の実行を止めない方を取る）。

## 検証（2026-10-04、host と kernel の build。QEMU・実機は未実施）

| command | 結果 |
| --- | --- |
| `python3 plan/ws049/tests/run-asl.py` | 18 passed, 0 failed（新しい pcibridge を含む）。pcibridge は変更前の `aml-field.c` では `MAIN returned 0x1`（試験が新しい挙動を区別する） |
| `plan/ws049/tests/check-latitude5330.sh` | PASS（4 項目。host の模擬では root port の config が 0 なので bus は変わらない） |
| `sh plan/ws014/tests/run-pci-service-lifecycle-host.sh`（`pci.c` を host で link） | PASS |
| `make -C plan/ws049/tests kernel-check`・`make ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix -j8` | warning 0、`amd64 vmunix check: PASS` |
| `python3 plan/tools/style-check.py src/drivers/acpi/aml-field.c` | 0 findings |
| `style-check.py src/drivers/pci/pci.c` | 145 件（変更前 146 件、古い `drv_pci_find_device` の分が減り、新しい code の指摘は 0） |

## 残り

- QEMU の回帰: p007 の依頼（T1-086）の `guest-compare.sh`（namespace が host と同じ）がこの変更の後の image でも通ること。q35 の DSDT の
  PCI_Config の region は host bridge の直下だけなので、bus は変わらない見込み。T1-086 の image がこの commit を含まなければ、次の T1 の依頼に足す。
- 実機（p008 の UAT と同じ回）: NVMe など、root port の下に実在する機能（bus 1 以降）について dmesg に `PCI_Config region of absent function` が出ないこと。空の root port の下（何も挿さっていない slot の secondary bus）の分は出てよい（全ビット 1 が正しい答え）。
