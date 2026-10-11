<!-- awesome-plan project=zedbsd record=ws197-p007 -->

# ws197-p007: HFP の音（通話の声）— 詳細設計

Parent: [WS197](../ws.md)
Status: planning（q930-i01、P1。設計 2.1（review-2 の X・S2・S5 を反映）、X 節の短い再 review と Q12〜Q14 待ち）
Phase disposition: normal
Queue: q930（[P1 の lane](../../agents/P1/queue.md)）

## 目標と範囲

2026-10-11 ユーザー（P1 の lane の承認）:「HD audioサウンドドライバの録音機能実装、HD audioのマイクとスピーカーを使った実際のHFP通話が目標。」

到達: 5330 で、ペアのスマホの通話（p006 の HFP の制御で受ける・掛ける）の相手の声が 5330 の内蔵 speaker から聞こえ、5330 の内蔵 mic の声が相手に届く。音の経路は Bluetooth の SCO（HCI over USB の isochronous）。

前提: p006（HFP の制御、cleared 待ち・main に統合済み）、WS204（HDA の録音、p002・p003 は実装済み、5330 の確かめは p004）。上位の設計は [p001 §7.4](../phase001/phase.md)（CVSD を先、mSBC は後、LC3-SWB と Intel の音の offload は使わない、echo は AG の EC/NR に任せる、録音の filter は bluetoothd の側）。

## 今の tree の事実（2026-10-11、agent/p1 = main 9e5b93903 以後）

| 所 | 事実 |
| --- | --- |
| USB の core | isochronous の URB は**ある**（`include/drivers/usb/usb.h` `struct drv_usb_iso_packet`、`drv_usb_urb_setup_isochronous`、`src/drivers/usb/usb.c:2308`）。interface の claim・alternate の切り替えもある（`drv_usb_interface_claim`・`drv_usb_interface_set_alternate`） |
| xHCI | endpoint の型は isoch（IN 5・OUT 1）を context に書く（`pci-xhci.c:2332`）が、**Isoch TRB の enqueue と完了の処理が無い**。`xhci_urb_enqueue`（3936 行）は **endpoint ごとに request 1 つ**（`ep->active != NULL` → EBUSY）。isochronous は途切れなく流すのに 2 つ以上の URB を並べる必要がある |
| xHCI の context | `drv_xhci_endpoint_context_encode`（`include/drivers/pci/pci-xhci-control.h:115`）: FS・LS の periodic の interval を interrupt の式（bInterval を frame の数）で出す。**isoch の FS の bInterval は指数**（2^(bInterval−1) frame）なので bInterval > 1 で違う。word4 の **Max ESIT Payload が 0**（isoch・interrupt の帯域の計算に要る）。HS の high-bandwidth の Mult（wMaxPacketSize の bit 12:11）を捨てている |
| EHCI | isoch は ENOTSUP（`pci-ehci.c:1823`）。5330 も Pi 4 も xHCI なので今回は扱わない |
| usb-bt | interface 0 だけ（`src/drivers/usb/usb-bt.c:12-14`）。interface 1（isochronous）は誰も持たない |
| UAPI | `/dev/bluetoothN` は **open が 1 つだけ**、`BT_PACKET_SCO` は予約で EINVAL（`include/uapi/bluetooth.h`） |
| bluetoothd | session は同期の HCI command を最長 2 秒待つ（その間も同じ fd を読む）。SCO の扱いは無い。HF の features に EC/NR は無い（AG の EC/NR が働く、`AT+NREC` を送らない） |
| audiod | 8〜192 kHz の stream を受け、変換は線形補間（anti-alias の filter 無し）。device の rate は HDA で 48 kHz |
| 5330 の BT | Intel AX211（USB 8087:0033、bcdUSB 2.01、class e0/01/01、configuration 1 に interface 2 つ: 2026-10-11 P1 が 5330 の zedBSD の `lsusb -v` と dmesg で読んだ。zedBSD の lsusb は alternate を出さない → p007a2 の最初に usb-bt の attach で interface 1 の alternate と wMaxPacketSize を klog に出して読む）。interface 1 の alternate の実の値は**未確認**（公開の dump では alt 0〜5 で wMaxPacketSize 0・9・17・25・33・49、EP 0x03 OUT・0x83 IN、bInterval 1。alt 6（63 byte、mSBC 用）の有無も未確認）→ p007a の実装の前に 5330 で descriptor を読む |

追加の事実（review-1 から、確かめた）: xHCI の event と request の照合は `endpoint->active` だけ（`xhci_event_request_locked`・`transfer_claim`・`xhci_request_publish_locked`）、`ring_push` に空きの計算が無い（「Queue depth is one per endpoint」）、cancel は dci ごとに 1 つ（`xhci_device_quiesce`）、isoch の URB は transfer reservation を持てない（usb.c）、完了の callback は hard IRQ（`xhci_irq`→`xhci_completion_drain`）、IN の bounce は連続の写しだけ（`xhci_completion_finish`）、`drv_usb_device_reset` は全 interface の URB 0 を求める、SET_INTERFACE と HCI command は EP0 の門を取り合う、bluetoothd は単一 thread（main.c の poll）。

## 決まったこと（2026-10-11 ユーザー、[ws.md](../ws.md) の末尾）

| ID | 決め |
| --- | --- |
| Q9 | 別の node は作らない。**今の `/dev/bluetoothN` で `BT_PACKET_SCO` を有効に**、kernel は SCO だけの小さな ring（あふれたら古い物から捨てる、ACL・event を塞がない）、interface 1 の alternate の ioctl を 1 つ。bluetoothd は fd の読み手の thread を 1 つ置き、session と SCO の thread へ振り分ける |
| Q10 | PC で応答・発信した通話は PC で鳴らし、スマホで応答した通話はスマホのまま。通話の card の button で切り替え |
| Q11 | mSBC は WS205 の SBC の後（WS205 p002 で SBC は実装済み、ここでは CVSD を先） |
| 音の model | 通話の音は app が PCM を中継しない。**audiod に音の endpoint と route** を持たせ、app（Phone・Settings）は route を選ぶだけ。bluetoothd の SCO は audiod の endpoint として登録する（WS205 の A1 の sink を一般化して source も） |

## 設計 2（review-1 と上の決定を反映。設計 1 は git の 4e9a42e93）

### 分け方と見積もり

| Phase | 内容 | 見積 | 依存 |
| --- | --- | --- | --- |
| p007a1 | xHCI の isochronous（X1〜X10）、TD の状態機械の純粋な関数と host 試験、QEMU の usb-audio の ktest（OUT） | 12 LW | 本設計の再 review |
| p007a2 | usb-bt の interface 1・SCO の組み立て直し・`BT_IOC_SET_SCO`・SCO の ring（S1〜S9）、5330 の HCI Local Loopback で IN・OUT を測る | 8 LW | p007a1 |
| R | **audiod の endpoint と route**（R1〜R9）、libkeiland・compositor・backend の route の口、Phone の通話の card の切り替え、Settings の Sound の出力 | 10 LW | — （p007b1 と WS205 の p004・p009 が使う） |
| p007b1 | bluetoothd の読み手の thread、SCO の thread、CVSD の Synchronous Connection、audiod の endpoint の登録（B1〜B8） | 12 LW | p007a2、R、p006 |
| p007b2 | mSBC（`AT+BAC`・`+BCS`、T2・T1、transparent、WS205 の SBC） | 5 LW | p007b1、WS205 p002 |
| p007c | 5330 の実機（T1）と通話の UAT（ユーザー） | — | p007b1 |

計 約 47 LW（設計 1 の 27 から増: review-1 の queue・cancel・reservation・halt・reader thread・route の model）。R は WS197 の外の共通の仕事なので **新しい WS（例「audiod の音の endpoint と route」）に置くのを推す**（WS205 の p004 はその上に乗る形に直す）。

### R: audiod の endpoint と route

| ID | 決め | 理由 |
| --- | --- | --- |
| R1 | **endpoint**: 音の出入り口。種類は sink（audiod が書く: speaker、headset の A2DP、headset の SCO の送り、phone の SCO の送り）と source（audiod が読む: mic、headset の SCO の受け、phone の SCO の受け）。HDA の speaker・mic は audiod の中の endpoint、外の物は daemon（bluetoothd、将来の modem の daemon）が登録する | ユーザーの model。modem も同じ形 |
| R2 | 登録: `AUDIOD_ENDPOINT_ADD {id, kind(sink/source), class(speaker/mic/headset/phone-call/…), rate, channels, format, name}` → 共有 memory の ring（stream と同じ形、`audiod_shm_header`）。sink は audiod が書き daemon が読む、source は daemon が書き audiod が読む。`AUDIOD_ENDPOINT_REMOVE`。登録できるのは決めた uid（bluetoothd の child の uid、将来の modem の uid）だけ（audiod は peer の uid を socket から得る） | socket は 0666 なので登録の権限を絞る |
| R3 | **route**: 名前のある配線。`media`（app の再生の mix → 出力の endpoint 1 つ: 既定は HDA の speaker、A2DP の headset に切り替え）と `call`（通話の endpoint の組 ↔ 利用者の endpoint の組: phone の SCO の受け → 出力（speaker か headset の SCO の送り）、入力（mic か headset の SCO の受け）→ phone の SCO の送り）。`AUDIOD_ROUTE_SET {route, output, input}`、`AUDIOD_ROUTES` の event（subscribe） | app は PCM に触らない |
| R4 | route を変えられるのは session の利用者（compositor が代わりに）と bluetoothd（通話の始まり・終わりで call を張る・外す）。app は libkeiland の `kl_system_audio_route_*` で compositor に頼む（Phone・Settings だけ）。compositor の backend が audiod に言う | Guardrail（app は daemon に直接触らない） |
| R5 | 流し方: audiod の中で route の各つなぎ（source → sink）に **適応の rate 変換**（多相の FIR、8/16 kHz ↔ device の rate、PI 制御で ±0.1% まで、1 sample の足し引きはしない: click になる）と jitter の水位（目標 40 ms）。call が張られている間、media は call の出力に −12 dB で重ねる（着信の時も音楽が鳴り続けないように） | review-1 の B3・B4・MINOR（click）。変換を bluetoothd から audiod に移す |
| R6 | 拍: 各 endpoint は自分の clock を持つ（HDA は device の period、外の endpoint は daemon が ring を進める速さ）。つなぎは source の書き位置と sink の読み位置で水位を見て、変換の比を寄せる | 2 つの clock の間 |
| R7 | 片道の遅れの予算 150 ms 以下（SCO の受け 3〜7.5 ms ＋ jitter 40 ms ＋ 変換 ＋ HDA の period と buffer） | review-1 MINOR |
| R8 | endpoint が消えたら（headset が切れた）route は既定（speaker・mic）へ戻る。call の endpoint が消えたら call を外す | |
| R9 | 試験: host で audiod の route の仕組み（偽の endpoint の daemon、偽の device）: 8 kHz の source を 48 kHz の sink へ、±0.1% の clock のずれで 10 分の水位、route の切り替え、endpoint の消滅。FIR の周波数応答（3.4 kHz まで ±0.5 dB、4 kHz 以上で −50 dB） | |

### p007a1: xHCI の isochronous（review-1 の BLOCKER 1〜4・MAJOR を反映）— **設計 2.1 の X 節が置き換える**

| ID | 決め |
| --- | --- |
| X1 | isoch の endpoint だけ request の FIFO: `active` は先頭、残りは `isoch_next` の list（上限 `XHCI_ISOCH_QUEUE_MAX` 4）。bulk・interrupt・control は今の 1 つだけのまま |
| X2 | 照合: Transfer Event の TRB pointer で FIFO の各 request の TRB の範囲（`first_trb`・`trb_count`、ring の巻きを含む）を探す。request は残りの TD の数と packet ごとの結果を持ち、**最後の TD の event でだけ completion へ**。unlink は先頭を外して次を active に、`active_count` は request ごと |
| X3 | Missed Service（23）: event が個々に来ない・TRB pointer 0 の時がある → **飛ばしの規則**（ある TD の event が来たら、その前の event の来ていない TD を missed で終える）と**空の規則**（Ring Underrun 14・Overrun 15 の event で queue の残りの TD を全部 missed で終える）。pointer 0 の MSE はこの 2 つの規則だけで扱う（Linux の ep->skip と同じ考え） |
| X4 | ring の空き: producer と consumer（最後に完了した TRB）の位置を数え、足りなければ enqueue は EAGAIN。**isoch の buffer は 64 KiB を跨がせず 1 TD = 1 Isoch TRB**（続きの Normal TRB を作らない）。1 URB 32 packet × 4 URB = 128 TRB ＋ link で 256 の ring に収まる |
| X5 | cancel: 「URB X の cancel は X と後ろ全部を CANCELLED、前は普通に完了」。Stop Endpoint → Set TR Dequeue を X の先頭の TRB へ → `recovering` を持ったまま、後ろの URB を HCD が CANCELLED で完了（その間の再投入は EBUSY）。`xhci_device_quiesce`・`xhci_controller_drain_requests`・`xhci_transfer_busy_locked` は queue の全部を回す |
| X6 | halt: 一部の xHC は isoch でも Babble・Transaction Error で Halted にする → 失敗の event の後に endpoint context を読み、Halted なら Reset Endpoint → Set TR Dequeue（`xhci_endpoint_recover` を流用）。isoch には core の halt の latch が無いので、`xhci_urb_enqueue` の Halted の EIO を isoch では recovery の後に解く |
| X7 | context（isoch だけ、interrupt は変えない: 5330 の HID・hub・BT の Configure Endpoint を変えないため。interrupt の Max ESIT の直しは別 Phase に Future Work）: CErr = 0、FS の Interval = bInterval − 1 + 3、Max ESIT Payload = FS は packet、HS は packet ×（wMaxPacketSize bit 12:11 + 1）で bit 12:11 は Max Burst（word1 15:8）へ、Mult（word0 9:8）は 0（SS isoch だけの物）、SS の isoch は今は EINVAL。対象は FS（5330 の AX211）と HS、Hi（LEC）は 0xFFFF 以下なので使わない |
| X8 | DMA と確保: isoch の URB にも transfer reservation（usb の core と HCD の変更、HAL は変えない）。再投入は usb-bt の worker からだけ（hard IRQ の callback で確保しない）。IN の bounce は packet ごとに offset と実の長さで写す。enqueue で `iso_packets` を検査（offset + length ≤ URB の長さ、IN の length = wMaxPacketSize、PENDING・hcd_owned の URB は EBUSY、`drv_usb_urb_setup_isochronous` にも同じ検査）。TBC・TLBPC・TD Size・ISP は FS・HS の 1 packet で 0、SIA = 1、途中の TD は IOC + BEI、最後は IOC |
| X9 | 止まる時: GATED（suspend の準備）の間の isoch の enqueue は EBUSY、流している間は system の suspend をしない（通話の間、Phone が inhibit を持つ: Phone の範囲）。`command_busy` の間の completion の後回し（`xhci_irq` の drain）で 24 ms を越える危険 → ktest で最長の後回しを測り記録 |
| X10 | 試験: (a) **TD の状態機械を `pci-xhci-control.h` の純粋な関数**（event の照合、飛ばし・空の規則、cancel の切り分け、ring の空き）にして host 試験で全 completion code（Success・Short・MSE・Underrun・Overrun・Babble・Transaction Error・Stopped）と ring の巻き。(b) QEMU の `-device usb-audio`（isoch OUT）の ktest: interface を claim、alternate 1、1 ms の packet の URB を 3 つ並べて 10 秒、完了の数・packet の失敗・URB の間の途切れ、cancel・alternate 0・抜き差し。結果は sysctl か ktest の node を SSH で読む（console の log で判定しない）、合否の閾値: 失敗 0、途切れ 0、cancel の後の残り 0。ktest は vmunix.mk の knob で試験の image だけ。(c) IN・短い packet・Overrun は QEMU で出せないので p007a2 の 5330（HCI Local Loopback）で |

active を見ている所の扱い（review-1 BLOCKER 1）:

| 所 | 今 | isoch での扱い |
| --- | --- | --- |
| `xhci_event_request_locked` | active と event を照合 | FIFO の各 request を TRB の範囲で照合（X2） |
| `xhci_request_publish_locked` | active に置く、失敗で trap | 先頭なら active、他は `isoch_next` の末尾、上限で EAGAIN（trap しない） |
| `transfer_claim` | 最初の event で request を終える | 残りの TD が 0 になった時だけ終える、飛ばし・空の規則 |
| `xhci_streams_configure`・`xhci_endpoint_disable` | active が有れば EBUSY | queue が空でない時も EBUSY |
| `xhci_endpoint_reset` | active・recovering を見る | queue の全部が終わってから |
| `xhci_request_release` | active == request なら外す | queue から外す（先頭なら次を active に） |
| `xhci_urb_enqueue` | active != NULL で EBUSY、Halted で EIO | isoch は上限まで積む、Halted は X6 |
| `xhci_cancel_request`・`xhci_urb_dequeue` | 1 つだけ | X5 |
| `xhci_device_quiesce`・`xhci_controller_drain_requests`・`xhci_transfer_busy_locked` | dci ごとに active 1 つ | queue の全部 |
| `xhci_submission_quiesce`・`xhci_release_resources`・`xhci_start` | `active_count` | request ごとに数える（変えない） |

### p007a2: usb-bt の interface 1 と SCO（Q9 の形）— **S2・S5・S8 は設計 2.1 が置き換える**

| ID | 決め |
| --- | --- |
| S1 | usb-bt は attach で interface 1 を claim（alternate 0、流さない）。無い controller は `bt_info.flags` の `BT_INFO_SCO` を立てない |
| S2 | UAPI（`include/uapi/bluetooth.h`）: `BT_PACKET_SCO` を有効に（write は SCO の packet 1 つ、長さは header と一致、`BT_SCO_DATA_MAX` 以下）。新しい ioctl 1 つ: `BT_IOC_SET_SCO` = `_IOWR('b', 5, struct bt_sco)`、`struct bt_sco { uint16_t handle[3]; uint16_t count; uint16_t voice_setting; uint16_t packet_length; uint16_t alternate; uint16_t mtu; uint32_t reserved[4]; }`（固定長。入力: 本数 0〜3、handle、Voice Setting、SCO の packet の長さ（Read Buffer Size か eSCO の値）。出力: 選んだ alternate と wMaxPacketSize）。0 本で流すのを止め alternate 0。新しい node・dev_t は作らない |
| S3 | alternate の選び: Linux の btusb と同じ（CVSD 16 bit は本数 1・2・3 で alt 2・4・5、8 bit は本数、transparent は alt 6 が有れば 6、無ければ 1） |
| S4 | 権限: 既に開いている fd への ioctl なので、privsep の child（uid を落とした）でも使える。open の決まり（1 つだけ）は変えない |
| S5 | ring: SCO の受けは ACL・event と別の ring（約 200 ms 分、あふれたら古い物を捨てて数える、ACL・event を塞がない）。read は届いた順（両方の ring に通しの番号）。SCO の送りの queue は約 40 ms まで（満ちたら EAGAIN、流していない時は ENOTCONN） |
| S6 | 組み立て直し: IN の isoch の bytes から SCO の packet を組む。handle と packet の長さを S2 で知っているので、packet が落ちて header がずれたら組み立て中を捨て、handle と長さが合う所で同期し直す（数える） |
| S7 | EP0 の門: SET_INTERFACE は HCI command と EP0 を取り合う → alternate の設定は session の thread（command を書く thread）から、kernel も EBUSY の時は短く（最長 50 ms）待ち直す |
| S8 | reset・detach・close・bootloader: reset（BT_IOC_RESET、p013 の reopen・reset）と detach は interface 0 の前に interface 1 の URB を cancel・drain、reset の後は SCO 0 本（bluetoothd は Synchronous Connection を張り直す）。close で 0 本。bootloader の間の `BT_IOC_SET_SCO` は EBUSY。usb-bt の worker の drain の最長 1 秒の待ちは SCO の URB には使わない（SCO は別の完了の通路で、worker を止めない） |
| S9 | 統計: `bt_stats` の reserved から `sco_in`・`sco_out`・`sco_dropped`（大きさは変えない）。5330 の確かめ: interface 1 の descriptor（alt と wMaxPacketSize）、AX211 の SCO が HCI に来るか（Read Local Supported Codecs・Intel の SCO の経路の設定）を **p007a2 の最初に**（5330 を使う前に Q1 に知らせる）。HCI Local Loopback で IN・OUT の packet の数・落ち・遅れを T1 が測る |

### p007b1: bluetoothd（review-1 の B・MINOR を反映）

| ID | 決め |
| --- | --- |
| B1 | **読み手の thread**: `/dev/bluetoothN` を読むのは 1 つの thread だけ。ACL・event は main の loop への queue（pipe で起こす）へ、SCO は SCO の thread の queue へ。session の同期の command の待ちは fd を直に読まず、この queue を待つ（2 秒の待ちの間も SCO は流れる）。共有するのは queue だけ（可変の状態を共有しない）。書きは main（command・ACL）と SCO の thread（SCO）が別々に write（kernel は packet ごと） |
| B2 | HCI: Read Buffer Size の SCO の値、Read Voice Setting。AG の Connection Request（SCO 0x00・eSCO 0x02、相手が phone の link）に **Accept Synchronous Connection Request**（eSCO の値つき、Voice Setting 0x0060）。eSCO の組は HF features の bit 9（eSCO S4）を **p006 の AT+BRSF に足す**（62 → 574、p006 と WS に記録）上で S4 → S3 → S1 → D1。HF から開ける時（codec negotiation が無い今）は Setup Synchronous Connection。Synchronous Connection Complete で handle、`BT_IOC_SET_SCO` は session の thread から（S7） |
| B3 | SCO の thread: 受けた SCO の packet（CVSD の 8 kHz・16 bit の PCM）を audiod の **phone-call の source の endpoint** の ring へ、phone-call の sink の endpoint の ring から読んだ PCM を SCO の packet にして送る。Packet Status Flag が data 無しなら無音を書く。rate 変換・jitter・clock のずれは audiod（R5・R6）、bluetoothd はしない |
| B4 | 送りの拍: 周期は alternate と packet の長さから（CVSD alt 2 の 48 byte は 3 ms）、単調時計で送る。受けが来ている間は受けの数に合わせて寄せ、受けが N ms（例 50 ms）来なければ時計だけで送る |
| B5 | 通話の始まり: Synchronous Connection Complete で audiod に endpoint の組を登録し、route `call` を張る（Q10: PC で応答・発信した通話はこのコンピュータの speaker・mic、スマホで応答した通話は bluetoothd が SCO を受けない＝スマホのまま）。終わり（Disconnection）で外す |
| B6 | UI（review-1 B2、R4 の上）: Phone の通話の card に「音: このコンピュータ／スマホ」（headset が繋がっていれば「ヘッドセット」も）。「スマホ」は HF が SCO を切る（AG が音を自分へ戻す）。libkeiland・compositor・backend の phone の口に `KL_PHONE_ASK_AUDIO`（"computer"・"phone"）、route の選びは R の `kl_system_audio_route_*` |
| B7 | echo は作らない（AG の EC/NR に任せ `AT+NREC` を送らない）、Phone の card に「ヘッドセットを勧める」。流している間は suspend しない（Phone が inhibit） |
| B8 | 試験: host で読み手の thread の振り分け（偽の fd、2 秒の同期の待ちの間も SCO が流れる）、HCI の台本（偽の controller、WS143 の道具）で Connection Request → Accept → Complete → SET_SCO → Disconnection、送りの拍（受けが止まっても送る）、endpoint の登録と route の張り外し（偽の audiod）。実機は p007c |

### review-1 の各項目の行き先

| review-1 | 行き先 |
| --- | --- |
| BLOCKER 1（照合・FIFO・active の所） | X1・X2・active の表 |
| BLOCKER 2（MSE） | X3 |
| BLOCKER 3（ring の空き） | X4 |
| BLOCKER 4（cancel） | X5 |
| MAJOR X6（CErr・ESIT・Mult） | X7 |
| MAJOR interrupt の直し | X7（isoch だけ、interrupt は Future Work） |
| MAJOR X7（reservation・IRQ） | X8 |
| MAJOR IN の bounce・検査 | X8 |
| MAJOR halt | X6 |
| MAJOR 試験（QEMU は OUT だけ・ktest の読み方） | X10、S9 |
| MAJOR S4 の UAPI | S2・S4・S5（Q9 の形で） |
| MAJOR reset と interface 1 | S8 |
| MAJOR EP0 の取り合い | S7 |
| MAJOR 組み立て直し | S6 |
| MAJOR eSCO S4・AT+BCC | B2 |
| MAJOR 送りの拍 | B4 |
| MAJOR UI | B6、R4 |
| MINOR | 単一 thread → B1、suspend・GATED → X9・B7、24 ms → X9、setup の検査 → X8、TBC 等 → X8、worker の 1 秒 → S8、click → R5、遅れの予算 → R7、Accept → B2、PSF → B3、見積もり → 上の表、計画に無い依存 → 下 |

### 計画に無かった依存（Q1 へ）

R（audiod の endpoint と route、新 WS を推す）、p006 の BRSF の bit 9（p007b1 の中で直す）、usb の core の isoch の reservation（p007a1 の中）、vmunix.mk の ktest の knob（p007a1 の中）、5330 の descriptor と AX211 の SCO の経路（p007a2 の最初、Q1 の許可）、HCI Local Loopback の T1 の道具（p007a2）、Phone・libkeiland・compositor・backend の通話の音の口（B6、R の上）。privsep の SCO の口は Q9 の形（既に開いた fd への ioctl）で不要になった。

## 設計 2.1（2026-10-11、[review-2](review-2.md) の X 節・S2・S5 を反映）

### p007a0: 5330 の確かめ（p007a1 の前、読むだけ）

- 2026-10-11 済み: 5330 の zedBSD の `lsusb -v`・dmesg・`bt show`: AX211 は 8087:0033、bcdUSB 2.01、class e0/01/01、interface 2 つ、HCI 版 13（5.4）、manufacturer 2（Intel）。**zedBSD の lsusb は alternate を出さず、bt は HCI の supported commands・codec を出さない**。
- 残り（小さな変更 2 つ、試験の image で T1 が 5330 で読む。controller の状態は変えない）: (a) usb-bt の attach で interface 1 の各 alternate の wMaxPacketSize を klog に 1 行、(b) bluetoothd の起動の時に Read Local Supported Commands の SCO に関わる bit（Setup/Accept Synchronous、Enhanced Setup/Accept、Write Loopback Mode）、Read Local Supported Codecs（v1）、Read Buffer Size の SCO の値を log に 1 行ずつ。(c) HCI Local Loopback の試しは bluetoothd を止める必要があるので p007a2 の中（Q1 に先に知らせる）。だめ（SCO が HCI に来ない）なら p007a1 に入らず Q1 へ。

### X 節（置き換え）

| ID | 決め |
| --- | --- |
| X1 | isoch の endpoint だけ request の FIFO: `active` は先頭、残りは `isoch_next` の list（上限 4）。**不変条件: active == NULL なら list は空**（debug の assert）。FIFO から外すのは `xhci_request_unlink_locked`（完了の claim と cancel）だけ。`xhci_request_release` は今の「公開中なら trap」を「FIFO のどこかに居れば trap」に広げる（外さない） |
| X2 | 照合: Transfer Event の TRB pointer で FIFO の各 request の TRB の範囲（ring の巻きを含む）を探す。request は残りの TD の数と packet ごとの結果（実の長さ・状態）を持つ。最後の TD が終わった request だけ completion へ |
| X3 | **飛ばしの規則**: ある TD の event が来たら、FIFO の順でそれより前の event の無い TD を MISSED で終える。**Missed Service（23）・Ring Underrun（14）・Overrun（15）では何も完了させず「skip 待ち」の印だけ**（pointer 0 の MSE も同じ）。片付けは次の TD の event（飛ばしの規則）か、印から一定時間（URB 2 本分の時間）event が無い時に **thread** が Stop Endpoint を出し、その完了の後に endpoint context の TR Dequeue Pointer を読んで、それより前の TD は MISSED、後ろは続け（doorbell）。IRQ の中で TD を完了させないので、worker が出したばかりの TD の buffer を返すことは無い |
| X4 | ring の空き: producer と consumer（最後に event の来た TRB）の位置を数え、足りなければ enqueue は EAGAIN。**1 TD = 1 Isoch TRB**、packet の buffer は 64 KiB を跨がない（reservation の中で並べる） |
| X5 | **cancel は endpoint 全体**: Stop Endpoint → `xhci_endpoint_dequeue_all`（dequeue を enqueue に、producer の cycle も）→ その完了の後に FIFO の全 URB（X より前も）を CANCELLED で完了（既に結果の出た packet の結果は残す）。その間の enqueue は EBUSY。class（usb-bt）は SCO の流れを止める時にだけ cancel する。host 試験は停止点が X の前・中・後の 3 例 |
| X6 | **halt**: event の側（IRQ）は「Halted を見た」を記録するだけ。回復は thread（xHCI の今の port_worker、無ければ isoch の回復の worker）: Reset Endpoint → `xhci_endpoint_dequeue_all` → FIFO の残りの URB は残りの packet を IO_ERROR にして完了（結果の出た packet は残す）。回復の間の enqueue は EBUSY |
| X7 | context（isoch だけ、interrupt は変えない）: CErr 0、FS の Interval = bInterval + 2、Max ESIT = FS は packet・HS は packet ×（bit 12:11 + 1）、bit 12:11 は Max Burst へ、Mult 0。**対象は FS と HS の mult 0（1 interval 1 transaction）だけ、他は EINVAL**。TBC・TLBPC・TD Size 0、SIA 1、**IN は ISP 1** |
| X8 | usb の core の API: isoch にも transfer reservation（HAL は変えない）、`drv_usb_urb_iso_packet_result_set`（HCD 用: index、実の長さ、状態）と `drv_usb_urb_iso_packet_result`（class 用）。packet の状態は OK・SHORT・MISSED・ERROR・CANCELLED。**URB 全体の状態**: cancel → CANCELLED、切断 → NODEV、X6 の回復 → IO_ERROR、それ以外は COMPLETE（packet ごとの結果を見る）。`drv_usb_urb_setup_isochronous` に PENDING・hcd_owned・offset/length の検査 |
| X9 | event の数: 途中の TD は IOC + BEI、ただし **BEI は 7 つまで続けて、8 つ目ごとに BEI 無しの IOC**。event ring 256 に対し、SCO の IN・OUT の 2 本 × 1 ms に 1 event、IRQ の遅れの最悪（command_busy の後回し、24 ms）で 48 event、余裕あり（ktest で最長の後回しを測る） |
| X10 | suspend: `xhci_transfer_busy_locked` は isoch の流れを転送中と見なすので、流している間の system の suspend は 2 秒待って失敗する（正しい振る舞い、記録）。通話の間は Phone が inhibit を持つ、蓋を閉じても通話中は suspend しない |
| X11 | URB の大きさ: **IN は 8 packet（8 ms）× 3 本**、**OUT は 4 packet（4 ms、SCO の packet 1〜2 つ分）× 3 本**。遅れ: IN は束ねで最大 8 ms、OUT は queue で最大 12 ms（R7 の表に入れる） |
| X12 | 試験: (a) TD の状態機械（照合・飛ばし・skip 待ち・cancel の停止点 3 例・halt・ring の空き・BEI の数え）を `pci-xhci-control.h` の純粋な関数にして host 試験。(b) QEMU の `-device usb-audio`（isoch OUT）の ktest の前に、qemu-xhci の isoch が動くかを T1 に小さく確かめてもらう（動かなければ (b) は無しで、合否は (a) と 5330）。結果は sysctl か node を SSH で。(c) IN・短い packet・Overrun は 5330（p007a2 の HCI Local Loopback） |

active を見ている所の扱い（設計 2 の表に足す・直す）:

| 所 | isoch での扱い |
| --- | --- |
| `xhci_request_unlink_locked` | FIFO の pop の中心: 先頭を外して次を active に（claim と cancel だけが呼ぶ） |
| `xhci_device_request_busy_locked`（`xhci_device_release` の trap の判定） | FIFO の全部を見る |
| `drv_xhci_endpoint_reset_admit(active, …)` | active に加えて list が空でない時も busy |
| `xhci_request_release` | **外さない**: FIFO のどこかに居れば trap（今の不変条件を広げる） |

### S2・S5・S8 の置き換え

| ID | 決め |
| --- | --- |
| S2 | `BT_IOC_SET_SCO` = `_IOWR('b', 5, struct bt_sco)`、`struct bt_sco { uint16_t handle[3]; uint16_t voice_setting[3]; uint16_t packet_length[3]; uint16_t count; uint16_t alternate; uint16_t mtu; uint32_t reserved[2]; }`（32 byte、handle ごとに Voice Setting と packet の長さ）。alternate の規則: CVSD だけなら 16 bit の本数 1・2・3 で alt 2・4・5（8 bit は本数）、transparent（mSBC）1 本だけなら alt 6 が有れば 6・無ければ 1、**CVSD と transparent の混在・transparent 2 本は EINVAL**（WS205 H3 の phone CVSD・headset mSBC は、headset を CVSD に落とすか後の Phase で決める） |
| S5 | **SCO の write は `op_lock` を取らない**: spinlock の下で SCO の送りの queue（約 40 ms）に積むだけで返る（満ちたら EAGAIN、流していない時 ENOTCONN）。withdraw は別の counter。`include/uapi/bluetooth.h` の冒頭の契約を書き直す: command・ACL の write は今のまま（controller が受けてから返る）、SCO の write は queue に積んで返る、read は届いた順（ACL・event・SCO を通しの番号で）、SCO の受けの ring は約 200 ms で古い物から捨てる、poll の POLLOUT は SCO の queue に空きのある時も、O_NONBLOCK は fd に共通。送りの queue の水位は `bt_stats` の reserved の `sco_out_queued`（B4 が見る） |
| S8 | usb-bt に **SCO 専用の thread**: isoch の完了は wake だけ、再投入・組み立て直し・送りの queue からの詰めはその thread。今の worker の drain（最長 1 秒）や reset の quiesce は SCO の thread を止めない。reset・detach は interface 0 の前に SCO の thread を止め interface 1 の URB を cancel・drain、reset の後は SCO 0 本。interface 1 の claim の失敗（core が probe 中）は「SCO 無し」。SET_INTERFACE の timeout で device が隔離されたら bluetoothd は LOST → p013 の reset |

### B1 に関わる確認（Q1 へ）

読み手の thread の main への queue の上限: **4096 packet** を推し、越えたら LOST として reopen（p013 の回復）。main は同期の待ちもこの queue を読むので、越えるのは main が解析で止まった時だけ。kernel に SCO だけを読む口を足す案（review-2）は Q9 の範囲（ioctl 1 つ）を越えるので推さない。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| Q12 | R（audiod の endpoint と route）をどこに置くか | **新しい WS**（WS197 p007b1 と WS205 p004・p009 と将来の modem が使う共通の仕事）。WS205 の A1 はその上に直す |
| Q13 | 通話の間の音楽 | **call の出力に −12 dB で重ねる**（止めない。音楽の app の一時停止は後） |
| Q14 | 「スマホで話す」の切り替えの場所 | **Phone の通話の card だけ**（Settings には置かない） |

## 進み

- 2026-10-11: 設計 1（4e9a42e93）→ design-reviewer の [review-1](review-1.md)（BLOCKER 4）。
- 2026-10-11: 設計 2（P1）→ [review-2](review-2.md)（blocker 3: X5・X3・X6）。
- 2026-10-11: 設計 2.1（X 節・S2・S5・S8、P1）。次は X 節の短い再 review、p007a0 の残り（T1）、Q12〜Q14。R の再設計（15〜20 LW）は Q12〜Q14 の後で p007b1 の条件。
