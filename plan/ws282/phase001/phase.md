<!-- awesome-plan project=zedbsd record=ws282-p001 -->

# ws282-p001 camera の枠組みと source の driver の設計

- 親: [ws282](../ws.md)、Queue: Q1 の割当（P3、2026-10-23。版 2 は worktree p3-cam2・branch agent/p3-cam2、基点 main 61832637b。版 3 は worktree p3-cam3・branch agent/p3-cam3、基点 main b2cd7cba5）
- Status: in-progress（設計の版 3 を書いた。Q1 の判定と版 3 の review 待ち）
- 成果物: [design.md](../design.md)（版 3）、[review-20261023.md](review-20261023.md)（版 1 の review）、[review-v2-20261023.md](review-v2-20261023.md)（版 2 の review、Q1 の手配）

## 目的と受け入れの条件

- `/dev/camN` の枠組みの API（format の列挙、mode、buffer の queue と GPU との共有、start/stop、frame の metadata、control、source の分離、node の権限、hotplug）と、最初の source の driver の設計、Phase の分け方と見積もり、試験の計画、危険、ユーザーの判断を design.md に書く。実際の code を読み、仕様と参照の出所を段ごとに示す。コードは書かない。
- 受け入れ: design.md が上の項目を全部持ち、Q1 の手配する review で致命の欠落が無い。
- 範囲の変更（2026-10-23 ユーザー、Q1 経由）:「USB Video classを優先しましょう。CSIは今回はやりません。/dev/camNのAPIはそのまま使います。uvcドライバを実装します。カメラは今から購入します。まずは手元にあるHDMIキャプチャでもデバッグには使えそうですか？」→ 最初の source は **UVC（uvc）**。Pi 5 の CSI（rpicam）は design.md §13 と付録 A。

## ユーザー・Q1 の判断

| # | 答え | 反映 |
| --- | --- | --- |
| D1〜D7 | 2026-10-23 ユーザー「decisionは推しでいいです」「カメラケーブルは手元にあります。」（D1 connector はどちらでも、D2 (a) I2C の core を共有、D3 GPU が確保・camera が取り込む、D4 0600 + seat、D5 IMX219 は後、D6 1296x972 RAW10・AEC/AGC on、D7 16 bit unpacked） | design §6.1。CSI の物は付録 A |
| D9 | Q1 決定: GPU core と bcm2711 の差分は WS282 の担当が書き、Q1 の merge で入れる | design §3.6、§6.1、p008 |
| D10 | Q1 決定: xHCI の isochronous の拡張は WS282 の担当が WS197 p007 と調整して入れる（Q1 が重ならない順を決める）、T1 の回帰（BT の SCO、HID） | design §4.7、§6.1、p005。版 3 で「WS197 は完了」を訂正し、p005 の前提に WS197 p007a2 の L2 の PASS と main への統合を置いた |
| D8 | HDMI capture は使える（段階付き。版 3 で「640x480 で約 13 fps」を撤回: isochronous は device が速さを決めるので P > 1024 の mode は `ENOSPC`。capture の YUYV は p005 の後の見込み、p003 の dump で確定） | design §6.2 |
| D11 | **ユーザーに提示中**。前提: ユーザーの PC の browser が `pattern.html` を全画面で出す（design が HTML を提供する、p006） | design §6.2、§7.4 |
| D12 | **ユーザーに提示中**。勧め: USB 2.0 の UVC 1.1 の webcam で YUYV と MJPEG を両方持ち、小さな YUYV の mode（P ≤ 1024）を持つ物（p004b の受け入れの device） | design §6.2 |

## 記録（2026-10-23 P3、版 3）

- 読んだ物（版 2 の物に加えて）: `include/drivers/usb/usb.h`（:79 の「one active URB per endpoint」、:85 の予約の上限 64 KiB、`drv_usb_urb_reserve_transfer`、`drv_usb_urb_setup` の timeout、isochronous の契約 :980-990、`drv_usb_urb_drain`・`drv_usb_urb_retired`）、`src/drivers/usb/usb.c`（`drv_usb_urb_setup`、timeout 0 = 無期限）、`src/drivers/pci/pci-xhci.c`（`xhci_request_alloc` :3651、`xhci_urb_reserve` :3925、`xhci_urb_enqueue` :4163 の EBUSY、isochronous の enqueue :8160-8240、suspend の拒否 :6675）、`src/drivers/usb/usb-bt.c`（SCO の completion と thread の bit の集め :1833-1920、`drv_usb_urb_retired` :2117）、`include/kern/waitq.h`・`pmem.h`・`src/kern/pmem.c`・`src/drivers/audio/audio.c:2059`・`include/uapi/system.h`・`userland/desktop/sessiond/seat.c:70`・`include/uapi/io-stats.h`・`userland/base/sysctl/main.c`・`include/kern/thread.h`・`plan/ws197/ws.md`・`plan/ws197/phase007/phase.md`。Linux の `uvc_video.c`・`uvc_driver.c`（reviewer の scratchpad の写し、読むだけ）は組み立ての規則と PROBE の補正と descriptor の offset の照合に使った（code は写さない。出典の書き方は design §8）。`plan/guardrail.md` の「MIT・GPL の出典のデータの引用」。
- 実行した command: `git worktree add /home/awe/zedBSD-worktrees/p3-cam3 -b agent/p3-cam3 main`（b2cd7cba5）、`grep`/`sed` で source を読んだ。build・QEMU・実機は無し（設計の Phase）。rm は使っていない。
- 結論: design.md 版 3 = 版 2 の review の H1〜H4・M1〜M10・L1〜L8 を全部反映（下の表、design §12）。Phase は p004 を p004a（xHCI の HS isoch IN の実地）と p004b（streaming）に分け、p005 を HS の high-bandwidth だけにし、SS の isoch を p012（後）に。
- 確かめた事実（版 3 で新たに）: (1) xHCI は isochronous 以外の endpoint に URB を 1 本しか受けない（usb.h:79、pci-xhci.c:4163）→ bulk は 1 本。(2) 予約は 64 KiB まで（usb.h:85）で `xhci_urb_reserve` は 2 の冪に揃える → 32 KiB の URB は予約でき 64 KiB をまたがない。予約の無い URB は投入ごとに coherent を取る（`xhci_request_alloc`）。(3) `kern_pmem_alloc` は 0 で埋めない、audio.c は確保の直後に `kern_memset`。(4) `/dev/system` の event の class は 0x01〜0x80・mask 0xff、seat は INPUT | USB を購読 → camera は新しい class。(5) WS197 p007a1 の受け入れは host 試験と build、isoch の実地は p007a2 の L2 で「5330 は新しい image の後」（未実施）→ HS の isoch IN は未実地。(6) isochronous の URB は device が沈黙しても 32 packet で終わる（pci-xhci-isoch.h の MISSED の規則）→ 「URB の完了が無い」は信号無しの検出にならない。(7) `waitq_sleep` に `WAITQ_INTERRUPTIBLE`。(8) Linux の `uvc_video_start_transfer` は帯域の足りる alternate が無ければ `-EIO` で始めない、`uvc_video_decode_start` は `last_fid = -1`・同期外れの捨て・NO_FID の反転・空の buffer を返さない、`uvc_video_validate_buffer` は非圧縮の短い frame を error、`uvc_video_decode_bulk` は payload が URB をまたぐ（照合に使っただけ）。
- HAL の API（hal.h）の変更: 不要（版 2 と同じ。reviewer も同意）。
- 他の WS の source に頼む変更（Q1 へ、design §11・§6.2 Q-1）: p003 `src/kern/devfs.c`（`cam` の 0600）、p005 `include/drivers/pci/pci-xhci-control.h`・`src/drivers/pci/pci-xhci.c`（HS の high-bandwidth、**WS197 p007a2 の後、D10**）、p008 `src/drivers/gpu/gpu.c`・`include/drivers/gpu/{gpu.h,gpu-share.h,gpu-scanout.h}`・`src/drivers/gpu/bcm2711/{share.c,render-device.c}`（D9）、p009 `include/uapi/system.h`（`KERN_SYSTEM_EVENT_CAMERA`）・`userland/desktop/sessiond/seat.c`。
- **計画に無い依存（Q1 へ）**: p005 は **WS197 p007a2 の L2（5330 の HCI Local Loopback）が PASS して main にある事**が前提。WS197 は incomplete（p007 はベータ3）なので、p005 の時期は WS197 の進みに依る。p004a で xHCI の isoch の不具合が出た時の直しの担当（WS197 か WS282 か）は D10 の範囲で Q1 が決める。
- usb-bt の SCO の thread も URB の完了を bit で集めて番号の順に処理する型なので、同じ順の入れ替え（H4）があり得る（組み立て直しの resync で吸収し、数えていない）。WS197 へ報告する価値がある（Q1 の判断）。
- 未実施: 版 3 の review（Q1 が手配）、実機の確認（p003 以降）、HDMI capture の descriptor の dump（p003 で実体が決まる）、仕様の PDF での節の番号の確定（p003）。
- 敵対的に読み直した点（版 3）: (1) `ENOSPC` の判定を TRY/SET/start の 3 か所に置くと、SET の後に host の状態が変わる事は無い（候補は descriptor と encoder の条件で固定）ので start の判定は冗長だが、p005 の後に候補が増えた時の防御として残した。(2) `drv_cam_frame_begin` を take と分けたので、buffer が無い frame でも sequence が進み、落ちは飛びで数えられる。take に sequence を渡す形にして slot の metadata に入れる。(3) halt の順（cancel → drain → join）は worker が drain の間も走る事を前提にする: worker は `stopping` を見て再投入せず、retired な URB を処理せずに抜ける。drain の timeout の時も join は進む（worker は `stopping` だけで抜ける）。(4) 所有者が IDLE（format 設定済み）の間に他の open の TRY が device の PROBE の状態を変えても、所有者の `start` が COMMIT を送り直すので影響しない事を確かめた上で IDLE の TRY を許した。(5) bulk の 1 本では完了から再投入までの間に device の FIFO が溢れ得る。device が無いので p006 の「あれば」の項目に留め、受け入れに入れない。(6) `host_payload_max` は isochronous の候補の最大の容量なので、bulk では 0（制限なし）にし app が `payload_bytes` と比べる時に 0 を「制限なし」と読む規則を §3.2 に書いた。

## review の指摘の反映（版 2 の review → 版 3。design §12 と同じ）

| 指摘 | 変更 | design の節 |
| --- | --- | --- |
| H1 HS の isoch IN が未実地、p005 の基準が無い | p004a の新設（2〜3 LW）、R5 の撤回、D10 の訂正、p005 の前提に WS197 p007a2 の L2 と main への統合、回帰は L2 の道具の前後比較と HID | §4.7、§6.1、§7.3、§9、§11 |
| H2 帯域の足りない alternate、D8 の 13 fps | `ENOSPC`（TRY/SET/start）、`host_payload_max`・`payload_bytes`、D8 の訂正、p004b は P ≤ 1024 の mode、capture の YUYV は p006（p005 の後）、D12 の webcam を p004b の device に | §3.2、§4.3、§6.2、§7.3、§11 |
| H3 bulk の 4 本 | 1 本、64 KiB の予約、payload は URB をまたぐ、timeout 0、未実地と明記 | §4.4、§9 R11、§11 |
| H4 URB の順 | 投入の順の ring、host の試験に偽の URB の層 | §4.3、§4.5 |
| M1 128 KiB の URB | 32 KiB で予約、p005 は 21 × 3072、再投入の誤りの扱い | §4.3 |
| M2 組み立ての穴 1〜8 | §4.5 を書き直し（10 の規則）、INCOMPLETE/ERROR の意味、`drv_cam_frame_begin`、host の試験の case | §2.1、§3.5、§4.5、§7.1 |
| M3 COMMIT と他の open の TRY | start で COMMIT を送り直す、所有者が BUFFERS・STREAMING の間の TRY は EBUSY | §3.1、§4.2、§4.3 |
| M4 停止の二重、close の admission | 停止は枠組みだけ、uvc の detach は unregister だけ、stop は冪等、quiesce は halt + fault、close も admission | §1、§2.1、§2.3、§5、§7.1 |
| M5 watchdog、信号無し | counters（no_signal）、URB の完了が 2 s 無い時だけ fault、DEQUEUE の timeout と EINTR、bulk の timeout 0 | §3.2、§4.3、§4.4、§5 |
| M6 0 埋め | REQUEST_BUFFERS で memset、host の試験 | §3.4、§7.1 |
| M7 試験の計画 1〜7 | counter の規則、matrix と range の推定、MJPEG の pattern、camcommon の DHT、抜去はユーザーの一覧、io_stats の leak、host の試験の case、bulk は未実地 | §7.3、§7.4 |
| M8 p005 の内容 | HS の high-bandwidth だけ（TBC/TLBPC）、4〜5 LW、SS は p012、R3 は定数の変更（WS197） | §4.7、§9、§11 |
| M9 MJPEG の descriptor、GUID、出典 | 11 byte の形、27 byte の offset、GUID の byte 列、出典を payload の仕様に、節は p003 で確定 | §3.3、§4.1、§8 |
| M10 依存、suspend | p008 ← p007、依存の図、suspend の影響を §5 と docs、止める口は Future Work | §5、§9、§11 |
| L1 PROBE の細部 | bcdUVC で長さ、GET_LEN 無し、補正（出典付き） | §4.2、§4.2.1、§8 |
| L2 build | arm64 の filter、5 か所の登録、CONFIG_DRIVER_CAMERA | §4.8 |
| L3 event の class | KERN_SYSTEM_EVENT_CAMERA、p009 で Q1 に依頼 | §3.9、§11 |
| L4 UAPI | layout と _Static_assert、ENUM_INTERVAL、poll・QUERY_BUFFER・REQUEST_BUFFERS の規則、DEQUEUE の close を削除 | §3.1、§3.2、§7.1 |
| L5 alternate の順 | encoder と同じ条件で候補を絞る | §4.1、§4.3 |
| L6 thread の後始末 | halt の順（cancel → drain → join → NULL） | §4.3 |
| L7 IRQ の複写 | p007 で測る | §7.3、§11 |
| L8 板の quirk | p007 の最初に Linux の quirk の表 | §4.7、§11 |

## 見積もりの変更

- 版 1: p002〜p009 で 25〜34 LW（CSI）。版 2: p002〜p011 で 29〜40 LW（p010 を除けば 27〜37）。**版 3: p002〜p011（p010 を除く）で 32〜43 LW**（p010 を足せば 34〜46、p012 は未定）。増えた理由: p004a（xHCI の HS isoch IN の実地、2〜3 LW）、p005（TBC/TLBPC の読みと試験、+1）、p006（matrix の推定・leak の測定・quirk、+1）、p007（Linux の quirk の表、+0.5）。
- 依存: p005 は WS197 p007a2 の L2（PASS と main への統合、Q1 の順の調整）、p008 は WS279 の V3D と p007（Pi 5）と GPU core の差分（D9）、p003/p009 は devfs/system.h/seat（Q1）。p006 の HDMI capture の YUYV は p005 の後。
