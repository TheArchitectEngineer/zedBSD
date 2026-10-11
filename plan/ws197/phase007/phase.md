<!-- awesome-plan project=zedbsd record=ws197-p007 -->

# ws197-p007: HFP の音（通話の声）— 詳細設計

Parent: [WS197](../ws.md)
Status: planning（q930-i01、P1。p007a の kernel の詳細設計を書いた。design-reviewer と Q9（UAPI）の判断を待つ）
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
| 5330 の BT | Intel AX211（USB 8087:0033、Full Speed）。interface 1 の alternate の実の値は**未確認**（公開の dump では alt 0〜5 で wMaxPacketSize 0・9・17・25・33・49、EP 0x03 OUT・0x83 IN、bInterval 1。alt 6（63 byte、mSBC 用）の有無も未確認）→ p007a の実装の前に 5330 で descriptor を読む |

## 分け方

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p007a1 | xHCI の isochronous（下の X1〜X8）と context の直し、QEMU の `usb-audio` を相手にした kernel の試験（ktest） | 本設計の design-reviewer |
| p007a2 | usb-bt の interface 1、bt-hci の SCO の口（Q9 で決めた形）、SCO の UAPI | p007a1、Q9 |
| p007b1 | bluetoothd の SCO（CVSD、HCI の Synchronous Connection）、SCO の thread、audiod の 2 つの stream、filter と rate の変換、jitter と clock のずれ | p007a2、p006、WS204 |
| p007b2 | mSBC（codec negotiation `AT+BAC`・`+BCS`、eSCO の T2・T1、transparent の air mode） | p007b1、WS205 の SBC の encoder・decoder |
| p007c | 5330 の実機（T1）と通話の UAT（ユーザー） | p007b1（mSBC は p007b2 の後に追加の UAT） |

## p007a1: xHCI の isochronous

| ID | 決め | 理由 |
| --- | --- | --- |
| X1 | **isoch の endpoint だけ request の queue を持つ**（`struct xhci_endpoint` に isoch の FIFO、上限 `XHCI_ISOCH_QUEUE_MAX` = 4）。bulk・interrupt・control は今の「1 つだけ」のまま | 他の型の回帰を避ける。BT の SCO・USB の音は 2〜3 個の URB を並べて途切れなく流す（Linux の btusb も 2 つ） |
| X2 | 1 つの URB の packet（`iso_packet_count`、上限 `XHCI_ISOCH_PACKETS_MAX` = 32）が 1 packet = 1 TD。TD は Isoch TRB（type 5）1 つ（buffer が 64 KiB の境を跨ぐ時は続きの Normal TRB）。**SIA = 1**（Start Isoch ASAP）を全部の TD に。endpoint が空にならない限り xHC は続きの ESIT に置くので frame の番号の計算（MFINDEX・IST）は要らない | 単純で、queue（X1）が途切れを防ぐ |
| X3 | 完了: TD ごとに Transfer Event（IN の実の長さが packet ごとに要る）。途中の TD は **IOC + BEI**（event は作るが割り込みは起こさない）、最後の TD は IOC だけ。event の残りの長さを `iso_packets[i].actual_length` に、完了の code を `status` に | 1 kHz の割り込みを避けつつ packet ごとの長さを得る |
| X4 | 完了の code: Success・Short Packet は成功。**Missed Service Error（23）**・**Ring Underrun（14、OUT）**・**Ring Overrun（15、IN）** は packet の失敗（`DRV_USB_URB_ERROR` 相当の packet の status、URB 全体は成功で終える）で、endpoint は止まらない（halt でない）。Underrun・Overrun の後の enqueue は doorbell を鳴らすだけで再開。Babble・Transaction Error は packet の失敗。**Stall は isoch に無い**（来たら今の recovery の経路） | isoch は再送が無い。1 つの落ちで流れを止めない |
| X5 | cancel: 今の Stop Endpoint → Set TR Dequeue の経路を isoch の queue 全体に（queue の全部を cancel、TRB を No-op にせず dequeue を最後の後ろへ）。interface の alternate の切り替え（`set_alternate`）は先に全部の URB を cancel してから（usb の core の今の決まり） | 今の recovery の型を流用 |
| X6 | context: FS・LS の isoch の Interval = bInterval − 1 + 3、HS・SS の isoch = bInterval − 1。**Max ESIT Payload** = packet ×（Mult + 1）×（Max Burst + 1）を word0 の上位（Hi）と word4 の上位（Lo）に（interrupt にも同じ直しが要る、帯域の計算）。HS の high-bandwidth の Mult は wMaxPacketSize の bit 12:11 から | 仕様（xHCI 1.2 §6.2.3.6・6.2.3.8）。今の値では Configure Endpoint が Bandwidth Error か、誤った間隔になる |
| X7 | DMA: URB の buffer は今の bounce（staging）を使う。isoch の packet は小さい（BT は 9〜63 byte）ので 1 URB の buffer は 1 つの連続の DMA buffer、64 KiB の境の跨ぎは X2 の Normal TRB。HAL の API の変更は要らない見込み（要ると分かれば止めて Q1 へ） | |
| X8 | 試験: **QEMU の `-device usb-audio`**（USB Audio Class 1、isoch OUT の endpoint）を相手に、kernel の試験の driver（`src/drivers/usb/` の ktest、image の試験の構成だけ）が interface を claim し、alternate 1 にして 1 秒に 1000 packet の URB を 3 つ並べて 10 秒流す: 完了の数、packet の失敗の数、URB の間の途切れ（MFINDEX の差）。cancel・alternate 0 への戻し・抜き差し。既存の USB（keyboard・storage・BT の bulk・interrupt）の回帰は T1 の既存の試験で | qemu-xhci は isoch を emulate する。5330 の BT の isoch は p007a2 で |

見積もり: p007a1 約 8 LW（xHCI の queue・TRB・event・cancel、context の直し、ktest）。design-reviewer の対象: X1・X4・X5（今の recovery の不変条件との整合）。

## p007a2: usb-bt の interface 1 と SCO の口（Q9）

| ID | 決め | 理由 |
| --- | --- | --- |
| S1 | usb-bt は attach で interface 1（同じ class、isoch の EP だけ）を `drv_usb_interface_claim` で持つ（alternate 0、流さない）。無い controller は SCO 無し（`bt_info.flags` に `BT_INFO_SCO` を立てない） | |
| S2 | alternate は Linux の btusb と同じ規則: CVSD 16 bit（Voice Setting の bit 5）なら SCO の本数 1・2・3 で alt 2・4・5、8 bit なら本数 = alt。transparent（mSBC）は alt 6 があれば alt 6、無ければ alt 1。**kernel は HCI を解析しないので、bluetoothd が本数と Voice Setting を ioctl で言う**（`BT_IOC_SET_SCO`）。kernel は descriptor から alt を選び、その wMaxPacketSize を返す | zedBSD は host stack が userland（WS143 D2） |
| S3 | 流し方: IN・OUT とも URB 3 つ × 8 packet（8 ms）を並べる（X1）。IN は届いた bytes を SCO の packet（3 byte の header ＋ 長さ）に組み立て直して class の SCO の ring へ。ring が満ちたら**古い packet を捨てる**（isoch は NAK で待たせられない、数える）。OUT は class の SCO の書きの queue から packet を切って wMaxPacketSize ずつ、無ければ長さ 0 の packet | |
| S4 | **SCO の口の形（Q9、ユーザーに尋ねる）**: 推しは **(A) 別の node `/dev/bluetoothN.sco`**（open は 1 つ、bluetoothd だけ。read は SCO の packet 1 つ（H4 の 0x03 を先頭に）、write は SCO の packet 1 つ、poll、ioctl `BT_IOC_SET_SCO {count, voice_setting}` → `{alternate, packet_bytes}`、0 本で止める）。bluetoothd は SCO の thread がこの fd を持ち、session の 2 秒の同期の待ちと無関係に 7.5 ms ごとに読み書きする。案 (B): 今の node に SCO を混ぜる（`BT_PACKET_SCO` を有効に）。session の同期の待ちの間に来た SCO を別の thread へ渡す仕組みが bluetoothd に要り、読みの ring（32 KB）が SCO で埋まる危険 | (A) は既存の node の規則（1 open・順序）を変えない。SCO は順序の要らない別の流れ |
| S5 | 統計: `bt_stats` の reserved から `sco_in`・`sco_out`・`sco_dropped`（UAPI の構造の大きさは同じ） | |

見積もり: p007a2 約 6 LW。

## p007b1: bluetoothd の SCO と audiod（CVSD）

| ID | 決め | 理由 |
| --- | --- | --- |
| B1 | HCI: 起動時に Read Buffer Size の SCO の値（packet の長さと数）と Read Voice Setting。AG の Connection Request（link type SCO 0x00・eSCO 0x02、相手が phone の link）に **Enhanced Accept Synchronous Connection Request**（無ければ Accept Synchronous、Voice Setting 0x0060 = CVSD・16 bit・2 の補数・linear）。eSCO の組は HFP 1.8 の S4→S3→S1→D1。Synchronous Connection Complete（0x2C）で handle を記録し `BT_IOC_SET_SCO {1, 0x0060}`、Disconnection Complete で `{0}` | 仕様（HFP 1.8 §5.7） |
| B2 | 音の始まり: AG が開けた時（着信の応答・発信の後）。HF から開ける時は `AT+BCC`（codec negotiation が無い CVSD だけの今は AG に任せ、開かなければ通話の音は phone 側）。PHONE HF の行に `audio=0|1` を足し、Phone の通話の card に「音: このコンピュータ／スマホ」を出す。「スマホで話す」の button（HF が SCO を切る、AG は音を自分へ戻す） | 通話の音の行き先を人が選べる |
| B3 | SCO の thread: `/dev/bluetoothN.sco` と audiod の接続を持つ。受け: SCO の packet（CVSD の 8 kHz の 16 bit の PCM、payload は 48・60 byte など）→ jitter の buffer（60 ms、40 ms 溜まってから鳴らし始める）→ 8 kHz → 48 kHz の多相の FIR（6 倍）→ audiod の再生の stream（48 kHz・mono）。送り: audiod の録音の stream（48 kHz・mono）→ FIR の低域の filter と 1/6 の間引き → 8 kHz → 受けた packet 1 つにつき同じ長さを 1 つ送る（**受けで送りの拍を取る**: controller の clock に合う） | audiod の線形補間を使わない（p001 R20・S21）。送りの拍が controller の clock と一致する |
| B4 | clock のずれ: 再生側は jitter の buffer の水位（目標 40 ms）で、録音側は audiod の ring の水位で、1 sample を足す・間引く（最大 0.1%）。受けが 100 ms 途切れたら無音を鳴らし、水位をやり直す | |
| B5 | mic の gain: WS204 の固定の boost のまま（U1 の決め）。録音の stream の音量は audiod の unity。相手に小さすぎる時は p007c の UAT で値を見直す | |
| B6 | echo: 作らない。AG の EC/NR に任せる（`AT+NREC` を送らない）。Phone の通話の card に「ヘッドセットを勧める」 | p001 §7.4 |
| B7 | 試験: host で FIR（周波数応答: 3.4 kHz 以下の平坦 ±0.5 dB、4 kHz 以上の減衰 ≥ 50 dB）、jitter と clock のずれ（±0.1% の clock で 10 分の水位が保たれる）、HCI の台本（偽の controller、WS143 の道具）で Connection Request → Accept → Complete → SET_SCO → Disconnection。実機は p007c | |

見積もり: p007b1 約 8 LW。p007b2（mSBC）約 5 LW（SBC は WS205 の encoder・decoder を使う。H2 の header、60 byte の frame、PLC は無音）。

## 判断（Q1 経由でユーザーへ、推し付き）

| ID | 問い | 推し |
| --- | --- | --- |
| Q9 | SCO の UAPI の形 | **(A) 別の node `/dev/bluetoothN.sco` と `BT_IOC_SET_SCO`**（S4） |
| Q10 | 通話の音の既定 | **通話を 5330 で受けた（Phone の Answer）・掛けた時はこのコンピュータで鳴らす**。スマホで受けた通話はスマホのまま（AG が決める）。card の button で切り替え |
| Q11 | mSBC（p007b2）の順 | **WS205（SBC の encoder・decoder）の後**。それまでは CVSD（狭帯域）で通話できる |

## 進み

- 2026-10-11: 本設計（P1）。次は design-reviewer（Q1 の判断で起こす）と Q9〜Q11 の回答、5330 の interface 1 の descriptor の読み（5330 を使う前に Q1 に知らせる）。
