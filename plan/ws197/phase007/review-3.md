# ws197-p007 設計 2.1 の再 review（第 3 回、X 節・S2・S5・S8・B1 に限る、2026-10-11 design-reviewer、Q1 が要約して写した）

## 総評

p007a1 の実装にはまだ入らない。残る blocker は 1 つ（X3 の Stop が出す Stopped event）。X 節の major が 4 つ。直しは X3・X5・X6・X9・X11 の数文と endpoint の状態表 1 つ（設計 2.1a）、X 節を短く確かめて p007a1 へ。p007a0 の結果（alternate の wMaxPacketSize）は開始の条件のまま。S8・B1 は p007a2・p007b1 の範囲で p007a1 を止めない。B1 の 4096 と LOST の方針は妥当。

## review-2 の解け具合

X5 一部（dequeue_all で再実行の穴は塞がった、core との受け渡し・Stopped event・後の状態が残る）。X3 元の穴は解けたが Stop の Stopped event の扱いが新しい blocker。X6 一部（IRQ の deadlock は回避、thread と直列化が残る）。active の表は解けた（20 関数、release の trap は正しい）。iso の API はほぼ（名前と型）。URB の大きさは数は決まったが後回しと食い違う。SCO の write の非同期は方針は正しく細部が抜け。bt_sco の ABI は解けた（32 byte、amd64・arm64 同形）。

## 指摘

| 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- |
| blocker | X3 の Stop（X5・X6 も） | 自分の Stop Endpoint で動いていた TD k に Stopped 26／Length Invalid 27 が来る。`transfer_claim` が吸収するのは `request->cancelling`（pci-xhci.c:2623-2645）と suspend（2613-2620）だけ。X2・X3 のままだと TD k を終えて URB を返すが、TR Dequeue は TD k を指したままで doorbell で再実行、返した buffer に DMA | endpoint の印（`isoch_stopping`）を Stop の前に active_lock の下で立て、間の 26・27 は記録だけ（TD を終えず飛ばしの規則も動かさない）。Stop の後は TR Dequeue で手前の結果の無い TD を MISSED、その位置と後ろは残す。host 試験に「TD k の途中で Stop」 |
| major | X5 と USB core | `urb_cancel_to`（usb.c:8114-8131）は HCD の dequeue が 0 なら X を自分で publish し `urb_hcd_put` → HCD は X を `drv_usb_hcd_complete` しない（参照 2 回落ち）。X の前の W の Stopped を `transfer_claim` が IO_ERROR で完了。X の cancel 中の dequeue(Y) の返り値が無い（`xhci_urb_dequeue` は active だけ、4530-4540） | (1) X は core が publish、HCD は X 以外を CANCELLED で (2) endpoint 全体の cancel の間は FIFO の全 request の event を記録だけ (3) 間の dequeue(Y) は EALREADY (4) 完了の順を class に約束 |
| major | X5・X3 の後の状態 | `xhci_urb_enqueue` は EP0 以外で Running でないと EIO（4143-4145）。X5・X3 の後 Stopped のままで再開できない。`xhci_endpoint_restart_empty`（3274-3315）は空の isoch ring ですぐ Underrun → 印 → timer → Stop の繰り返しの恐れ | isoch の enqueue が Stopped を受けて doorbell で起こすか、X5・X3 の後に restart_empty。どちらでも「FIFO が空なら skip 待ちの印・timer を立てない」 |
| major | thread と直列化 | X3 の thread に名前が無い。X6 の port_worker（3076-3097）は列挙・command の 5 秒 timeout で遅れる、印の受け渡しも無い。enqueue も `ep->recovering`（4003-4024）を使い 4 者が同じ印を取り合う。port_worker が BT の detach で interface 1 を cancel すると EBUSY で drain の timeout まで止まる（推測） | controller ごとに isoch 専用の worker、または class（usb-bt の SCO の thread）から呼ぶ HCD の口。endpoint の状態表（NORMAL・SKIP_WAIT・STOPPING・CANCELLING・HALT_PENDING・RECOVERING）× event・enqueue・dequeue・quiesce・suspend。cancel は skip・halt を引き取る。lock は event_lock → active_lock、active_lock を持って `command_ex` を呼ばない |
| major | X9・X11（遅れ） | command の間 IRQ は completion を後回し（3053-3061）、`command_ex` は後で配る（1504-1511）。他の device の command・自分の Stop・SET_INTERFACE の間 isoch の完了が遅れる。OUT 3 本×4 ms＝12 ms は最悪 24 ms より短く Underrun | OUT の queue を後回しの最悪より長く（8 ms × 3〜4 本）か、wake だけの isoch の callback は command_busy の間も配る（command を出さない証明が要る）。R7 の表に、ktest で後回しの最大と OUT の Underrun の数を合否に |
| major | S8（隔離） | SET_INTERFACE の timeout で `device_quarantine_selection`（usb.c:3231-3235）、submit の門が閉じる（5385-5386）、`drv_usb_device_reset` は隔離に ENODEV（1297-1300）、usb-bt は detach されず read も ENODEV にならない → 「LOST → p013 の reset」が働かず再起動まで使えない | 隔離に気づいたら usb-bt が withdraw して bluetoothd に ENODEV。隔離の device を回復する道（port reset と列挙のやり直し）は USB core の仕事で計画に無い依存 → Q1。SET_INTERFACE の前に interface 1 の URB が全部 drain の前提を書く |
| major | B1（読み手の止め方） | 最後の参照で cdev の close（file.c:2420-2441）。読み手が `bt_hci_read` で眠っている間（bt-hci.c:548-566）は main が close しても `bt_hci_close` が呼ばれず `opened` は 1、reopen が EBUSY（446-447） | 読み手を `pthread_kill` で EINTR か [fd, 止める pipe] を poll。止めて join してから close・reopen。状態表を B1 に |
| minor | B1 の上限 | 数だけで ACL 最大 4101 byte × 4096 ≒ 16.8 MB。main は write でも止まる（ACL 2 秒・command 1 秒、op_lock、usb-bt.c:57-58、bt-hci.c:609-618）。通話中の LOST で音がスマホに移る（Q10 と違う）。SCO の queue に上限が無い | byte の上限（例 1 MiB）、高い水位で log、SCO の queue は古い物から捨てて数える、通話中の LOST は音がスマホに移ると明記して Phone の card に出す |
| minor | S5 の細部 | POLLOUT は controller が居る間いつも立つ（bt-hci.c:744）。blocking の fd で満ちた時が無い。transport の SCO の口と withdraw の counter の名前が無い。reset・SET_SCO の切替で queue を捨てるのが無い。`bt_stats.reserved[4]` を使い切る | POLLOUT の文を消し水位だけ。EAGAIN は O_NONBLOCK に関係なく返すか決める。口の名前と counter。捨てる時。counter の割り当て |
| minor | S2 | 入力の reserved が 0 の確かめ、handle の範囲（12 bit）と重なり、8 bit と 16 bit の CVSD の混在、`mtu` 1 つで IN と OUT の wMaxPacketSize が同じの仮定 | それぞれ EINVAL。`_Static_assert(sizeof == 32)` と offset の host 試験 |
| minor | X8 の名前と型 | `drv_usb_iso_packet.status` は `enum drv_usb_urb_status`（usb.h:223-228）で OK・SHORT・MISSED・ERROR が無い。NODEV は無く `DRV_USB_URB_DISCONNECTED`（usb.h:137）。reservation は isoch を断る（usb.c:1969・2026） | packet 用の新しい enum と field の型の変更、URB の actual_length（合計か）、reservation の 2 か所を直す |
| minor | X4（consumer） | dequeue_all・片付けの後に consumer を新しい dequeue に合わせない → 満ちて見え EAGAIN が続く | 明記と host 試験 |
| minor | X6（Halted の見つけ方） | どの completion code で context を読むか、Halted でない Transaction Error は packet の ERROR だけ、EBUSY を受けた class の待ち方が無い（`xhci_endpoint_state` 4233-4252 は IRQ で読める） | X6・S8 に明記 |
| minor | S8（thread） | SCO の thread の優先度が無い（既定 8、quantum 10 ms、sched.h:22-31）。reset・close の後に送りの queue を捨てる | 既定より高い優先度、捨てる時を明記 |
| minor | p007a0 | Read Local Supported Codecs を送らないので AX211 の SCO が HCI を通るか（Intel の offload でないか）は分からない（推測: btusb は AX211 で既定で HCI の SCO） | a0 で Read Local Supported Codecs も送るか、残る危険を p007a1 の開始の条件に |

正しかった点: X7 の式、X1 の上限 4 と X11 の 3 本、ring 256 と 1 TD = 1 TRB、X9 の event ring の計算、X10 の suspend、HAL の変更は不要。
