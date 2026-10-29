<!-- awesome-plan project=zedbsd record=ws221-p014 -->
# ws221-p014: xHCI の bulk endpoint に転送を重ねる（opt-in の queue depth）

Status: cleared（2026-10-30 03:35 P3 q1689-i01: 957ddf795・c5d728353・c4b52d61b・989d84fa2。5330 の NCM send 1512 → 2064〜2097、recv 1399 → 1449〜1453 Mbit/s（Linux r8152 2353・2354）、host・QEMU・内蔵 USB の非退行 PASS、全 platform の build warning 0。camcap は未実施（image の build が firmware の license の確認で止まる、Q1 の判断 (c)））
Parent: [WS221](../ws.md)
Queue: q1689 / q1689-i01（P3）
依存: p013（NCM の NTB の複数の datagram・RX の 2 つの buffer）

## 由来

- ユーザー（原文、2026-10-29）:「P3で、2.5GのUSBが遅いことは、ドライバの問題と思いますので、5330を使って修正してほしいです。」
- Q1（2026-10-30、承認）: HCD の変更は HAL でなく driver の変更で、ユーザーの指示は USB の 2.5G を Linux に近づけること。条件: endpoint ごとの opt-in（bulk だけ）、他の全ての device は depth 1 で今と同じ、host 試験（TRB の address による event の照合、完了の順、複数が出ている時の cancel・STALL・reset、ASan・UBSan）、実機 5330（NCM の send・recv、USB storage の非退行（stick があれば read/write）、HID の非退行（USB の HID があれば））、全 platform の build warning 0、実装の前に設計を phase.md に。
- ユーザー（原文、2026-10-30）:「xHCIの改修は行いましょう。たとえ今回効果がなくても、価値がある実装だと考えます。」→ 測った効きが小さくても入れる（価値は効きに依らない）。
- Q1（2026-10-30）: この後ユーザーは Realtek の vendor の mode の driver も望む（WS315 の予定）。その根拠として、この Phase で NCM の到達点を記録する: p014 の後の 5330 の send・recv、Linux の r8152 の値を向きごとに測り直して（send・recv を分けて）並べる。
- p013 の測り: 方向ごとに転送 1 つで、send は 17404 byte の block の 1 回 約 92 µs（bus 約 40 µs）、recv は 32 KB の 1 回 約 185 µs（bus 約 75 µs）。完了から次の submit までの間（割り込み → network worker の poll → 組み立て・parse → submit）が半分ほど。

## 今の xHCI（`src/drivers/pci/pci-xhci.c`、読んだ所）

- `struct xhci_endpoint` は `active`（要求 1 つ）。`xhci_request_publish_locked` は active が埋まっていれば EBUSY、`xhci_request_unlink_locked` は active を空に。
- submit（`xhci_urb_enqueue` の非 isochronous の経路、4150 行付近）: `ep->active != NULL || ep->recovering` なら EBUSY。`recovering` を submit の組み立ての間の排他に使い、TD を ring に積んで doorbell。TD の TRB は `normal_count < XHCI_RING_TRBS - 1` だけを確かめる（1 つの TD が ring に入るか）。
- 転送の event（`xhci_event_request_locked`）: `endpoint->active` を `drv_xhci_transfer_event_matches`（`include/drivers/pci/pci-xhci-lifecycle.h`、純粋な関数）で照合。完了（2850 行付近）は event の時に unlink し、completion の列に積み、`xhci_completion_finish` が後で callback。STALL は `recovering`・`stall_publishing` で admission を閉じ、USB core が halt を latch するまで保つ。
- cancel（`xhci_urb_dequeue` → `xhci_cancel_request`）: Stop Endpoint、Set TR Dequeue を software の producer（enqueue）に（ring を空に）、Running を待って DMA の持ち主を返す。active の 1 つだけを扱う。
- teardown の走査（4882 行の device の drain は dci ごとの `attempted` の bit、5459 行の controller の drain、7161 行の suspend）は endpoint の `active` を見る。
- isochronous は別の FIFO（`pci-xhci-isoch.c`、`xhci_isoch_cancel` は endpoint を丸ごと空に）を持つ。

## 設計

1. endpoint に `queue_depth`（既定 1）、要求の列（FIFO、submit の順）と数。`active` は列の頭のまま（depth 1 の全ての経路は今と同じ形で動く）。要求に `queue_next`。
2. opt-in: USB core の endpoint に「重ねてよい要求の数」（例 `drv_usb_endpoint_set_queue_depth(endpoint, n)`、bulk だけ、上限 4）。HCD の op（`drv_usb_hcd` の任意の hook、無い HCD は 1 のまま）。NCM の attach で bulk-in・bulk-out に 4・2。他の driver は呼ばない。HAL の API（`include/hal/hal.h`）は変えない。
3. admission: `ep->active != NULL` の代わりに「列の数 ≥ depth」で EBUSY。ring の空き: 出ている要求の TRB の和 ＋ 新しい TD ＋ link の 1 が `XHCI_RING_TRBS` を超えない事を確かめる（超えれば EBUSY、class driver は後で出す）。`recovering` の submit の排他はそのまま。
4. event の照合: 列の頭から順に `drv_xhci_transfer_event_matches` で照合し、頭でない要求に合った event は「順の外」として diagnose（xHCI は TD を ring の順に終えるので、頭より後ろが先に終わる事は無い）。完了は頭を unlink し、次を `active` に。
5. cancel・STALL・reset（depth > 1 の endpoint）: どれか 1 つの cancel は endpoint の全ての要求を cancel する（isochronous と同じ形）。Stop Endpoint → Set TR Dequeue を enqueue に（今の形で ring は空に）→ 列の全てを submit の順に CANCELLED で完了。STALL は頭が STALL で完了し、残りは IO_ERROR（または CANCELLED）で順に完了してから USB core の endpoint recovery（今の形）。reset・切断・suspend の走査は列の全てを扱う（4882 行の `attempted` は dci の bit でなく「その dci の頭の要求」で）。
6. 純粋な部分を `include/drivers/pci/pci-xhci-queue.h`（列の push・頭の取り外し・TRB による照合・ring の空きの計算）に出し、host 試験（`plan/ws221/tests/xhci-queue-test.c`、ASan・UBSan）: 順の完了、TRB の address の照合（頭・後ろ・ring の wrap）、空きの不足、cancel で全てが順に、STALL の頭と残り。
7. NCM: RX の URB を 4（buffer 4 つ、完了の順に parse、空いた buffer で出し直す）、TX の URB を 2（組み立てた block を順に）。cancel は全ての URB を出した順に（HCD は 1 つの cancel で全てを返す）。

## design-reviewer の指摘と反映（2026-10-30 P3）

review（致命 2・重大 6・中 8・軽微 7）。実装は上の設計を次のとおり直した形:

| 指摘 | 反映 |
| --- | --- |
| C1 submit の組み立ての排他（`recovering`）と STALL の trap | `recovering` を持ち主の数に（submit の組み立てと STALL の公開が重なってよい）。`xhci_recovery_leave_locked` は数を減らすだけ。組み立ての後、他の TD がある時は lock の下で endpoint の状態を読み直し、Running でなければ（`stall_publishing` も）EIO |
| C2 cancel の間の admission | 頭が `cancelling`（1 か 2）なら submit は EBUSY（1 回目と 2 回目の確かめ）。cancel の開始で queue の全てに印を付ける（`xhci_queue_mark_cancelling_locked`） |
| S1 取り消しの契約と順 | `xhci_cancel_request(c, d, r, others)`: r は呼び手が公開（dequeue は core、teardown は DISCONNECTED）、他は `xhci_queue_abort_locked` で completion の列の末尾に（`completions_busy`・`completion_busy` を数える）、command が無ければ cancel の thread が `xhci_completion_drain`。順は「列に先にある物、他を queue の順、頼まれた物は core が後で」。usb.h の `urb_dequeue` の comment に契約を書いた |
| S2 二重の cancel | 開始の時に全てに印。`xhci_urb_dequeue` は queue を探し、無ければ EBUSY、頭が cancelling なら EALREADY。device の drain の `attempted` は dci の bit のまま（1 回で全て退く） |
| S3 頭の Halted | 成功・short 以外で、code が 3・4・6・36 か output context が Halted なら、後ろを頭の直後に IO_ERROR で退ける（`aborted`: 完了の log を出さない）。Reset Endpoint の後は既存の `xhci_endpoint_recover` が Set TR Dequeue（producer へ）を打つ |
| S4 depth の不許可 | `drv_usb_endpoint_set_queue_depth(endpoint, depth, &granted)`（core の endpoint に持つ、HCD は `DRV_USB_HCD_CAP_QUEUED_BULK` で）。NCM は認められた数だけ URB を作る（1 なら今と同じ 1 本と buffer 2 つ） |
| S5 RX の buffer | URB 数 + 1（spare）。完了した block の URB は spare で直ちに出し直し、parse の後にその buffer が spare になる |
| S6 `->active` の全ての利用 | `active` は queue の頭の写し（publish・unlink で更新）。queue 全体が要る所: event の照合、cancel、request の解放の確かめ（`queued` の flag）、`active_count`（要求ごと）。頭で足りる所: endpoint_reset・streams・disable の admission、busy の確かめ、suspend の `xhci_transfer_busy_locked`（同じ endpoint の要求は同じ種類）、controller の drain（頭を繰り返し外す） |
| M1 ring の空き | `drv_xhci_queue_admit`: 1 つ空けて、他の TD の後ろでは更に 32（`DRV_XHCI_QUEUE_SLACK`、short packet の後に controller が辿る退いた TD の残り）。depth 1（列が空）は今と同じ上限 |
| M2 頭より後ろに合う event | 無視して最初の 8 回を log（古い pointer が再利用の slot に合う誤りを避ける。Q1 了承） |
| M3 depth の寿命 | core の `drv_usb_endpoint` に持ち、interface の driver を外す時（claim した sibling も）1 に戻す。stream・control・interrupt は 1 |
| M4 cycle bit の順 | 他の TD の後ろの TD は最初の TRB の cycle bit を最後に渡す（`hold_first`・`ring_release_held`） |
| M5 一時の EBUSY | NCM は submit を endpoint ごとに 1 本に直列にする（RX は rearm の権利、TX は pump）ので組み立ての排他の EBUSY は出ない。ring の空きは NCM の TD（2 TRB 以下）× 4 に十分 |
| M7 QEMU で depth > 1 | 範囲の外（ECM に opt-in を足さない）。5330 の実機で確かめる（Q1 了承） |
| M8 Phase の分割 | 1 つのまま（Q1 2026-10-30） |
| L3 Stopped - Short Packet（28） | Q1 の指示で p014 で直した: suspend の時の「ring に残す」に 28 を加えた（`drv_xhci_completion_stopped`）。他の経路は変えない。host 試験 `test_stopped` |
| L4 TX の sequence | TX は pump 1 本が組み立てと submit を続けて行い、URB ごとに block の buffer |

## 確かめ

- host: 上の xhci-queue の試験、`plan/ws197/tests/run-xhci-isoch-test.sh`（isochronous の非退行）、NCM の codec の試験。
- build: amd64・pcat・rpi4・rpi5・zybo・VF2・5330・RTL8127 の config の vmunix の warning 0。
- QEMU: usb-net（CDC ECM、depth 1 の経路）・usb-storage の回帰（qemu-tcp と USB storage の起動）。
- 実機 5330: NCM の send・recv（30 s、`NO_VF2=1 5330-ncm.sh`）、USB storage（stick があれば読み書き）、USB HID（あれば反応）。

## 危険

- xHCI は全ての USB 機器が使う。depth 1 の経路を変えない事を host 試験と QEMU と実機で確かめる。cancel・STALL の複数の要求は NCM だけが使う。

## 結果（2026-10-30 P3 q1689-i01）

実装（commit）: 957ddf795（xHCI の queue・core の opt-in・NCM の RX 4・TX 2・`pci-xhci-queue.h` と host 試験）、c5d728353（Stopped - Short Packet を suspend の時の Stopped に、L3、Q1 の指示）、c4b52d61b（試験の script）、989d84fa2（規約の短い見直し、動作は変えない）。

| 場所 | 変えた事 |
| --- | --- |
| `include/drivers/pci/pci-xhci-queue.h`（新規） | 純粋な判断: 要求の FIFO（最大 4、途中の取り出し）、ring の使用量と admission（1 つ空き、他の後ろは 32 の余白）、TRB の address による照合（頭・後ろ・無し）、Halted にする完了（3・4・6・36 か context の Halted）、Stopped の 3 つの code |
| `src/drivers/pci/pci-xhci.c` | endpoint の queue と `active`（頭の写し）、要求の `queued`・`aborted`、`recovering` の数、submit の admission（depth・cancel 中・組み立ての後の状態の読み直し）、最初の TRB の cycle bit を最後に、event の照合を queue 全体で（後ろに合う event は無視して最初の 8 回を log）、頭が Halted の時の後ろの退け、cancel・teardown で queue 全体（他は completion の列で CANCELLED・DISCONNECTED）、`DRV_USB_HCD_CAP_QUEUED_BULK` |
| `include/drivers/usb/usb.h`・`src/drivers/usb/usb.c` | `drv_usb_endpoint_set_queue_depth`（bulk、1〜4、認められた値を返す）・`drv_usb_endpoint_queue_depth`、driver を外す時に 1 に戻す、`urb_dequeue` の契約の comment。HAL（`include/hal/hal.h`）は変えていない |
| `src/drivers/usb/usb-cdc-ncm-net.c` | RX は認められた数（4）の URB と buffer 5（spare）、submit の順の列で完了を順に取る、rearm は全ての空いた URB。TX は認められた数（2）の URB と block、pump 1 本が組み立てと submit（sequence の順）。attach の行に `urbs=RX/TX` |

確かめ:

- host: `plan/ws221/tests/run-xhci-queue-test.sh` PASS（-O2 と ASan・UBSan。FIFO・途中の取り出し・ring の空きと余白・照合（Link TRB を跨ぐ・後ろ・遅れた event）・Halted の code・Stopped の 3 つの code（suspend の code 28 を含む `test_stopped`）・20 万手の乱数の model）、`plan/ws197/tests/run-xhci-isoch-test.sh` all PASS、`run-ncm-ntb-frames-test.sh` PASS。
- build: amd64・pcat・rpi4・rpi5・zybo・VF2（config-visionfive2）・5330（config-5330-jumbo）・RTL8127（plan/tools/rtl8127/config-amd64-rtl8127.mk）の vmunix の warning 0（規約の見直しの後に amd64・5330・zybo を build し直して warning 0）。`git diff --check` 無し。
- QEMU（KVM、image は `plan/ws221/tests/config-amd64-usb.mk`、c4b52d61b の src。989d84fa2 の書き方の変更の前の image）:
  - `qemu-usb-xhci.sh`（新規）PASS: `-name p3-ws221`、usb-storage から起動、lsusb で hub・stick・ECM・HID 3 つ（kbd・hub の下の mouse・tablet）、16 MiB の乱数の file を ECM で入れ、stick の上で copy と cmp、ECM で出して host で一致。
  - `qemu-tcp.sh` inet PASS（send 47.1・recv 48.6 Mbit/s、ECM、depth 1 の経路）。
  - `qemu-usb-idle.sh` PASS（hub と mouse、CPU の busy 1 % 未満）。
  - cache=unsafe は `guest.py` に口が無く付けていない。port は `guest.py` が空きを選ぶ。
- 実機 5330（`MODE=--nvme-kernel NO_VF2=1 5330-ncm.sh`、userland は NVMe の物、相手はこの host 10.0.10.2、30 s。下の表）: 5330-run PASS、ping 1472 が通る、ue0 の errors 0。
- 内蔵の USB の非退行（5330 の kernel log、bff52ee82 と p014 を比べた）: Broadcom 58200 は usb-ccid、AX211 は usb-bt、RTL8156 は usb-cdc-ncm で同じ。webcam は p014 の kernel では uvc が付き cam0 ができた（bff52ee82 の kernel には UVC の driver が無かった。main の merge で入った物で、p014 とは関係が無い）。AX211 の `xhci: transfer completion=4 … slot=3 endpoint=5 port=10`（の後の usb-bt の clear halt）は p014 の前の run（i02・p012・p013）にもあり、p014 に固有でない。
- 未実施: camcap による camera の stream（5330 の NVMe の userland に camcap が無い。image の build は amd-navi-firmware・i915-firmware の license の確認で止まり、承諾はユーザーの判断なので作らない、Q1 の判断 (c)）。USB の stick と USB の HID は 5330 に無いので、storage と HID は QEMU だけ。depth > 1 の cancel・STALL・抜き差し・suspend の経路は確かめていない（5330-run の終わりの再起動で NCM の stop の cancel が走るはずだが、その結果は観察していない。STALL・suspend は起こしていない）。

### NCM の到達点と Linux（WS315 の vendor の mode の根拠）

| kernel | send Mbit/s | recv Mbit/s | 記録 |
| --- | --- | --- | --- |
| bff52ee82（p013 の最後、URB 1 つ） | 1512.1 | 1398.8 | build/ws221-5330/p014-base-bff52 |
| p013 の実験の版（RX 64 KB、NVMe にあった物） | 1516.4 | 1309.6 | build/ws221-5330/p014-base-p013 |
| p014 c5d728353（RX 4・TX 2） | 2064.0 | 1453.4 | build/ws221-5330/p014-k1 |
| p014 989d84fa2（同じ、規約の見直しの後） | 2097.0 | 1449.0 | build/ws221-5330/p014-k2 |
| Debian の r8152（6.19.13、2500、同じ tcp-perf を Linux に build） | 2353.4 | 2353.6 | build/ws221-5330/p014-linux-1（`plan/ws221/tests/5330-linux-r8152.sh`） |

送りは Linux の 89 %、受けは 62 %。受けは URB を 4 本にしても 1399 → 1453 で、xHCI の要求 1 つは受けの天井ではなかった。残りの候補（測っていない）: RX の block の parse と packet への copy・poll の往復の CPU、NCM の device 側の NTB の詰め方（Linux の r8152 は NCM でなく vendor の mode）。

