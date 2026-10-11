<!-- awesome-plan project=zedbsd record=ws204-p002 -->

# ws204-p002: pci-hda の入力の選び直し・mic bias・boost・log

Parent: [WS204](../ws.md)
Status: in-progress（実装・host 試験済み、実機は p004）
Phase disposition: normal
Queue: q929（[P1 の lane](../../agents/P1/queue.md)）

## 実装（設計は [p001](../phase001/phase.md) の R1〜R4）

`src/drivers/pci/pci-hda.c`:
- `hda_input_choose`（capture の prepare のたび）: attach で決めた ADC から、(1) presence detect を持ち sense が「挿してある」jack、(2) 内蔵（connectivity internal）で device が Mic In の pin、(3) 接続のある入力の pin、の順に最初に届く pin を選び、その道を program して `hda: capture from pin 0x.. (plugged jack|internal mic|connected pin) config 0x........ through N widget(s)` を log に。
- `hda_route`: ADC から特定の pin への深さ優先の道。
- `hda_pin_input_control`: Mic In・Line In で digital・optical でない pin に VRef 80%（無ければ 50%、100%）。
- `hda_mic_boost`: pin の入力 amp を 0 dB（offset）から +20 dB に最も近い段（段の数の中）。
- `hda_inputs_log`: publish の時に入力の pin ごとに `hda: input pin 0x.. config .. pin caps .. amp ..`。

## 確認

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws204/tests/run-hda-fixture-test.sh`（ws035 の fixture を写して F09 の sense と capture の試験を足した。ws035 の volume の期待（50% = 0x4a の半分）は WS100 の curve（59）に追従） | plain・ASan/UBSan とも all PASS: QEMU duplex（pin 5、接続のある pin）、laptop の再生、**jack が空なら内蔵の digital mic 0x12（selector 0、bias 無し）、挿すと jack 0x19（selector 1、VRef 80% = pin control 0x24、boost 2 段 = +20 dB）** |
| `make … BUILD=build/ws204-k ZEDBSD_CONFIG=config/current-uat.mk build/ws204-k/vmunix` | rc 0、warning 0 |
| `plan/tools/style-check.py src/drivers/pci/pci-hda.c` | 67（base と同じ、新規 0） |

未実施: QEMU（T1、p003 と一緒）、5330 の実機（p004）。
