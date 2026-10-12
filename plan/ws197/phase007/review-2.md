# ws197-p007 設計 2 の敵対的 review（第 2 回、2026-10-11 design-reviewer、Q1 が写した）

対象: main の `plan/ws197/phase007/phase.md`（設計 2、1039f7432）、`review-1.md`、`plan/ws205/phase001/phase.md`。読むだけで行った。「推測」は未確認。

## 総評

- **p007a1 の実装にはまだ入らない。** X5（cancel）・X3（空の規則）・X6（halt）の 3 つの blocker は DMA の安全か hang に関わる。active の表、iso の packet の API、URB の大きさも p007a1 の設計そのもの。直しは X 節に閉じるので、設計 2.1 で X 節だけを直し、X 節に限った短い再 review でよい。並べて、5330 の descriptor と AX211 の SCO の経路の確かめを p007a1 の前に行う。
- **R を p007a1 より先にする必要はない**（p007a1 は R に依存しない）。ただし R は拍の model・遅れの予算の内訳・権限・endpoint の寿命・A2DP の Suspend との関係・3 OS の backend・Q13 の欠点で設計が足りない。Q12 の決定と R の再設計・review を p007b1（と WS205 の p004・p009）の開始の条件にする。p007a2 の S2・S5（`bt_sco` を handle ごとに、SCO の write を `op_lock` から外す）は ABI・UAPI の契約なので p007a2 の前に直す。

## 指摘

| 重さ | 箇所 | 問題 | 推しの直し |
| --- | --- | --- | --- |
| blocker | X5（cancel） | 「Set TR Dequeue を X の先頭へ」の穴: (a) HC が X より前の URB W の途中で止まると W の残りの TD が飛ばされ、W は完了せず quiesce が終わらない。(b) X から後ろの TRB は今の lap の cycle bit のまま ring に残り、次の doorbell で cancel 済みの TD を実行、返した buffer に DMA が走る。今の回復の `xhci_endpoint_dequeue_all`（pci-xhci.c:3244）とも違う | isoch の cancel は endpoint 全体を止める: Stop Endpoint → `xhci_endpoint_dequeue_all`（dequeue＝enqueue と producer の cycle）→ その完了の後に FIFO の全 URB（W も）を CANCELLED（完了済みの packet の結果は残す）。「前は普通に完了」を残すなら X 以降を No-Op にして dequeue を動かさない。host 試験に停止点が X の前・中・後の 3 例 |
| blocker | X3（空の規則） | Underrun 14・Overrun 15 の event から IRQ が処理するまでに worker が新しい URB を enqueue・doorbell しうる。その TD を「残り全部 missed」で完了すると HC が使用中の buffer を返す。純粋な関数の host 試験では見つからない | Underrun・Overrun では完了させず「skip 待ち」の印だけ。残りは次の TD の event（飛ばしの規則）か thread からの Stop Endpoint で片付け、Stop の後に endpoint context の TR Dequeue Pointer で前は missed・後ろは続行と分ける |
| blocker | X6（halt の回復） | `xhci_endpoint_recover`（:3319）は `command_ex` で完了を待つので、`transfer_claim`（event_lock と active_lock の下、hard IRQ）から呼ぶと deadlock。中で `xhci_endpoint_dequeue_all` を使い ring の TD を全部捨てるので FIFO の後ろの URB の event が来ず hang | event の側は「Halted を見た」の記録だけ、回復は thread（port_worker か回復の worker）。回復の後は FIFO の残りを全部 missed／IO_ERROR か次の TD へ Set TR Dequeue（どちらか明記）。回復の間の enqueue は EBUSY |
| major | active の表 | 抜け: `xhci_request_unlink_locked`（:1771・:1776、FIFO の pop の中心）、`xhci_device_request_busy_locked`（:1831、`xhci_device_release` の trap の判定）、header の `drv_xhci_endpoint_reset_admit(active, …)`。`xhci_request_release`（:3642・:3666）を「queue から外す」とするのは誤り（今は「公開中なら trap」の安全の不変条件） | release は「FIFO のどこかに居れば trap」のまま、外すのは unlink（claim と cancel）だけ。表に 3 行を足す。不変条件「active==NULL なら queue は空」と debug の assert |
| major | X2・X8（usb の core の API） | `struct drv_usb_urb` は usb.c の内部で、iso の packet の結果（actual_length・status）を HCD が書く口も class が読む口も無い。iso の API は alloc と setup だけ。packet の一部が missed の時の URB 全体の status も未定 | p007a1 に `drv_usb_urb_iso_packet_*` の getter・setter（HCD 用と class 用）と URB の status の対応表（cancel・切断以外は COMPLETE、packet ごとに結果） |
| major | X8 と S8 の矛盾 | 再投入は worker からだけ、だが worker は `usb_bt_finish` の `drv_usb_urb_drain(…, 1000)`（usb-bt.c:741）や reset の quiesce で 1〜2 秒止まる | usb-bt に SCO 専用の thread（完了は wake だけ、再投入と組み立て直しはその thread）。でなければ SCO の間は worker が sleep しないことを保証 |
| major | S5・B1（SCO の write） | `bt_hci_write` は `op_lock`（mutex）を持ったまま `ops->send` を同期で呼ぶ（bt-hci.c:609-618）。ACL 最長 2 秒、command 1 秒、BT_IOC_RESET・SET_SCO も同じ mutex。UAPI の「write は controller が受け取ってから返る」と S5 の「queue に積んで EAGAIN」が矛盾 | SCO の write は `op_lock` を取らず spinlock の下で SCO の送りの queue に積むだけ（非同期）。withdraw は別の counter。`bluetooth.h` 冒頭の契約（SCO の write・read・poll・EAGAIN・ENOTCONN、O_NONBLOCK の共有）を書き直す |
| major | X4・S5・R7（URB の大きさと遅れ） | 「1 URB 32 packet × 4」は容量だけ。IN は 32 ms ごと、OUT は HC に 64〜128 ms、S5 の 40 ms が加わる。R7 の 150 ms に USB の束ね・kernel の queue・HDA の lead（4096 byte × 2 ≒ 21〜43 ms、audio.c:37・:67）・capture の 1 period（約 21 ms）が無く、向きも分かれていない | IN 8〜10 packet、OUT は SCO packet 1〜2 つ分（3〜6 TD）、queue 2〜3 本。R7 を「相手→speaker」「マイク→相手」の 2 列で各段の最大と目標の表に |
| major | B1（読み手の thread） | read は全 ring 通しの 1 本の流れなので SCO の消費が ACL・event とつながる。main への queue に上限があると読み手が止まり SCO も止まる、無いと「読まなければ controller に留め置く」流量制御が失われる。読み手の終わり方（LOST・p013 の reopen・close、read で止まった thread の起こし方）と、audiod の shm を外す時の受け渡し（ack が無いと SEGV）が無い | queue の上限と越えた時の方針（例: 越えたら kernel の SCO だけの read で SCO を読み続ける。Q9 の範囲の kernel の口が要るので Q1 に確かめる）。thread の始め・終わりと shm の受け渡しの状態の表 |
| major | S2（`struct bt_sco` の ABI） | voice_setting・packet_length が 1 つずつで、WS205 H3（phone CVSD、headset mSBC）の混在を表せない。S3 に混在の alternate の規則が無い。S6 の組み立て直しも handle ごとの長さが要る | `voice_setting[3]`・`packet_length[3]`、16 bit 揃えで reserved で 32 byte を保つ。混在は EINVAL か alt の規則を S3 に |
| major | R5・R6（拍の model） | audiod は 1 thread、mix の拍は HDA の period（無い時は timer）。「各 endpoint が自分の clock」が曖昧、外の endpoint 同士（H3）や media→A2DP の拍が無い。PI ±0.1% では始まり・underrun の後の数十 ms のずれに数十秒。1 sample の足し引きも禁じている | 「master は HDA（または timer）、外の各 endpoint に ASRC」と明記。初めの充填と XRUN の後の詰め直し（無音の挿入・block の破棄）。R9 は ±0.05% のずれと段の応答を試す |
| major | R8 × WS205 H3・A1 | 通話の間 A2DP を Suspend → sink が消えたと扱うと media の route が speaker へ戻り、headset で通話中に音楽が speaker で鳴る。call の出力を headset に選んだ時に誰が headset の SCO を開けるかが無い（鶏と卵） | endpoint に「一時停止中」を設け「消えた」と分ける。route の要求で bluetoothd が headset の SCO を張る流れを R に |
| major | B5・Q10（状態機械） | in-band ringing、スマホで発信した時に AG が開ける SCO、音声認識（BVRA）の SCO が無い。in-band の SCO を受けたままスマホで応答すると Q10 に反する。誰が応答したかを Connection Request の時点で知る手段が無い | 着信中・発信中・応答（PC／スマホ）・音声認識 × SCO の要求の表。スマホで応答した時に SCO が開いていれば HF が切る |
| major | R4（OS 境界と権限） | `kl_system_audio_route_*` に libkeiland-backend の Linux・FreeBSD の実装（少なくとも ENOTSUP の stub）が要る、無いと WS108・WS109 の build と boundary の checker が壊れる。「Phone・Settings だけ」は強制の仕組みが無い（app_id は自己申告）。audiod の ROUTE_SET の許可（socket は 0666）が未定 | R に 3 OS の backend と checker。ROUTE_SET は root・`_bluetooth`・登録された compositor の uid に限る。「Phone・Settings だけ」は約束であり強制ではないと書く |
| major | Q13（−12 dB で重ねる） | 出力が speaker の時、音楽が speaker→mic で相手に届く。HF に EC は無く（B7）、AG の EC は自分の信号しか消せない。in-band の着信音と Phone の着信音の二重 | ユーザーに欠点を示す。推しを「出力が speaker の時は media を止める、headset の時だけ −12 dB」に |
| major | Phase の順 | AX211 の interface 1 の descriptor、SCO が HCI に来るか（Read Local Supported Codecs・Intel の経路）、HCI Local Loopback の対応を p007a2 の最初に調べる計画。だめなら p007a1 の 12 LW が無駄。今の kernel と bluetoothd で読める（推測） | 5330 の確かめ（Q1 の許可）を p007a1 の前の小さな調査に |
| major | 見積もりと WS の構造 | R の 10 LW は少ない（protocol・endpoint・route・FIR・PI・権限・compositor・libkeiland・3 OS backend・Phone の card・Settings・host 試験）。新 WS なら規約の Phase も要る。p007b1 は Q12 未決の R に依存。ws.md の p006 の行が planned で phase.md と食い違い | R は 15〜20 LW。Q12 の決定を p007b1 の開始の条件に。ws.md の p006 の行を直す |
| minor | X7・X8（HS） | X8 の「TLBPC・TD Size は 0、IN の length = wMaxPacketSize」は 1 interval 1 transaction の時だけ。IN の ISP=0 は IOC=1 なら event が出る見込み（推測）だが Linux は ISP を立てる | FS と HS の mult=0 だけ、他は EINVAL。IN は ISP=1 |
| minor | X8（BEI） | 推測: Linux は Intel の xHC に `XHCI_AVOID_BEI`。event ring 256 が IRQ の遅れで isoch 2 本で埋まりうる | BEI の連続を N（≤8）に、event の数と ring の大きさの計算を X4 に |
| minor | X9 | `xhci_transfer_busy_locked`（:6941）は isoch を転送中と見なし、流す間の suspend は 2 秒待って失敗（結果は正しいが未記載、lid の方針も無い） | 明記し ktest で |
| minor | R2（audiod の protocol） | `AUDIOD_MESSAGE_MAX` 64 byte、ENDPOINT_ADD で name に 24 byte、ROUTES の一覧が入らない。backend の `STREAM_PENDING` がこの値から計算 | message の最大を上げ `AUDIOD_VERSION` 2、name の長さを固定 |
| minor | R2・R8（endpoint の寿命） | 登録した接続が切れたら消える、daemon が書く位置の検査、ずれた時の PI の reset が無い | 明記 |
| minor | B4・S5 | 送りの拍に kernel の SCO の OUT の queue の水位が見えない（POLLOUT は「controller が居る」だけ） | `bt_stats` か別の ioctl で queued の値 |
| minor | B1・S5 | Disconnection の後の閉じた handle の SCO、SCO の thread からの snoop の trace | 捨てて数える、trace は main |
| minor | p006 の BRSF | 62→574 は正しいが `plan/ws197/tests/bt-hfp-host-test.c:308`・`:509` が `AT+BRSF=62` を固定で確かめている。S4 に HFP 1.7 以上の SDP の版が要る（推測） | p007b1 で試験を直し SDP の版を確かめる、p006 への comment も |
| minor | X10（QEMU） | QEMU の xHCI の isoch と `-device usb-audio` が動くか未確認（推測）。MSE・underrun の時間の振る舞いは再現しない | T1 に早めに小さな動作確認、合否の意味を phase.md に |
| minor | S1・S7 | core が interface 1 を probe 中なら claim は EBUSY。SET_INTERFACE の timeout は `device_quarantine_selection`（usb.c:3233 付近）で device 全体を隔離 | claim の失敗は SCO 無しとして扱う。隔離の時の bluetoothd の回復（LOST → reset）を S8 に |

## 第 2 版で正しかった点

X7 の式（FS の Interval＝bInterval+2、CErr 0、Max ESIT、bit 12:11 → Max Burst、Mult 0）、ring の容量（128 TRB＋link は 255 に収まる）、`struct bt_sco` の layout（32 byte）と `_IOWR('b', 5)` が空いていること、`getpeereid` があり bluetoothd の child の uid が `_bluetooth` で R2 の判定に合うこと、MINOR の多くの行き先（PSF、Accept、FIR の仕様）。
