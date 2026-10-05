<!-- awesome-plan project=zedbsd record=ws052p005 -->

# ws052-p005: suspend の無かった driver の「止めて入る」経路（HDA・Wi-Fi・LPSS-I2C）

Phase ID: `ws052-p005`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17。HDA・AX211・LPSS-I2C に suspend・resume を実装、vmunix の link。T1 の QEMU（HDA の往復・UHCI での中止）待ち、AX211・LPSS-I2C は 5330 の UAT）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（§10 の決定: HDA・Wi-Fi の本当の suspend は後、止めれば入れる。HAL に依らない範囲）

## 範囲と考え方

p004 の PCI の口は suspend の無い driver で中止する。5330 の bus 0 には HDA（audiod が stream を回し続ける）、Wi-Fi（AX211）、LPSS-I2C（touch pad の
I2C-HID）があり、p004 だけでは必ず中止になる。ここでは各 driver に「止めて入る」最小の suspend・resume を足す:

| driver | suspend | resume | 中止する時 |
| --- | --- | --- | --- |
| HDA（`pci-hda.c`） | 動いている stream の programming（CTL・CBL・LVI・FMT）を保って止め、interrupt を mask、command ring を止め、controller を reset に保つ。`/dev/dsp` は publish のまま、prepare・start は EBUSY | reset を出て command ring を指し直し、codec の電源・path・音量を書き直し、stream を書き戻し（converter の stream tag と format も）、動いていた stream を start、interrupt を有効に | 無し（audiod の stream は止めて再開する。位置は ring の頭からになり、client には飛びとして見える） |
| Wi-Fi（`intel-ax211.c`） | radio が off（net device が閉じ、recovery の再 open も runtime の DMA も無い）なら、resume まで open を拒む | open を受け付けるだけ（次の open が firmware を起動し直す、いつもの道） | radio が on なら EBUSY（`intel-ax211: the radio is on; it is turned off before the sleep`）。networkd が sleep の前に radio を切るのは p006・p007（sleep の事象を受ける側） |
| LPSS-I2C（`lpss-i2c.c`） | bus を hold（`drv_i2c_bus_hold`: 進行中の転送の終わりを待ち、touch pad の読みは待たせる）して core を off。D3hot で core は reset される | core を reset から出して設定し直し（`lpss_core_start`）、bus を release（待っていた転送が走る） | 無し。core が起きなければ報告し、bus は release（転送は失敗する） |

本当の省電力の suspend（HDA の codec の D3、Wi-Fi の WoWLAN）は後の Phase（§10）。

## 実装（2026-10-05）

- `src/drivers/pci/pci-hda.c`: `hda_suspend`・`hda_resume`、`hda_stream_save`・`hda_stream_restore`・`hda_codec_restore`、command ring の memory の確保と
  指す処理を分けた `hda_command_rings_point`、`hda_interrupt_enable`。`struct hda_stream` に保存の欄、`struct hda_controller` に `suspended`。
- `src/drivers/i2c/i2c.c`・`include/drivers/i2c/i2c.h`: `drv_i2c_bus_hold`・`drv_i2c_bus_release`（bus の転送の mutex を suspend から resume まで持つ）。
- `src/drivers/i2c/lpss-i2c.c`: `lpss_suspend`・`lpss_resume`、`suspended`。
- `src/drivers/wifi/intel-ax211/intel-ax211.c`: `ax211_pci_suspend`・`ax211_pci_resume`、`suspended`（`ax211_net_open` が EBUSY）。
- `plan/ws052/tests/p004-guest.sh`: `hda` の mode（`-device intel-hda -device hda-duplex` で往復が result=0、dmesg に `hda: suspended`・
  `hda: resumed`）、`abort` の mode を suspend の無い UHCI（`-device piix3-usb-uhci`）に変更（result=21、device が uhci）。

## 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| 規約 | `style-check.py`（4 file、新しい指摘 0） | PASS |
| QEMU（T1） | `p004-guest.sh hda` と `abort`（roundtrip は p004 の xHCI の直しと一緒に） | 未実施（依頼する） |
| 実機（5330） | ws159-p005 の 9.（`sleepctl devices`）で中止の device が Wi-Fi（radio が on）になること、radio を切れば先へ進むこと、touch pad と音が戻ること | 未実施 |

host の試験: HDA・LPSS・AX211 の suspend は register と driver の状態だけで、純粋な判断の部分が無いので host の試験は置かない（QEMU の HDA と
実機で見る）。

## 残り

- networkd が sleep の前に radio を切り、後で戻す（p006 の `power.sleep.begin`・`end` の事象を受ける）。それまで 5330 では Wi-Fi が on の時は
  中止の理由が `intel-ax211` になる。
- HDA の codec の電源を D3 に落とす本当の suspend、Wi-Fi の WoWLAN は後。
