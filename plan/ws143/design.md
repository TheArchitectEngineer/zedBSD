<!-- awesome-plan project=zedbsd record=ws143-design -->

# WS143 設計: Bluetooth（ws143-p001）

版: 2026-10-05 第 2 版（P1 generation18、q752）。第 1 版への design-reviewer の指摘 F1〜F25（[phase001/review-1.md](phase001/review-1.md)）を
反映した。各節の末尾の `[Fn]` は、その節が答える指摘。

目標は [ws.md](ws.md) の単一目標: Settings の Bluetooth の頁を実体にし、5330 の AX211 の Bluetooth（USB 8087:0033）で device を見つけ、
pairing・接続・切断ができる。

参照の扱い: Linux の btusb・btintel（GPL-2.0）は読まない。FreeBSD の iwmbtfw（BSD-2-Clause、`usr.sbin/bluetooth/iwmbtfw/`
の main.c・iwmbt_hw.c・iwmbt_fw.c、freebsd-src main 2026-10-05 取得）は手順と定数を確かめる参照で、code は写さない（新規に書く）。

## 1. 受け入れ（案。§9 の答えで確定）

1. 5330 で起動の後、Bluetooth の controller が使える（firmware を load し、BD_ADDR を読める）。firmware の package が無い時は
   Settings が理由（「firmware が要る」）を出す。
2. Settings の Bluetooth の頁と system bar で、電源の on/off、周りの device の一覧、pairing（確認・passkey）、接続、切断、忘れるが
   できる。「接続」は対応する profile のある device だけに出す（無い device は「この種類は未対応」）。
3. D1 の範囲（推奨 A）: Bluetooth のキーボードとマウス（BR/EDR の HID と LE の HOGP）が pairing の後に入力に使え、再起動・電源の
   入れ直し・電波の届かない所から戻った時に再接続する。接続が切れた時に押されたままの key が残らない。
4. CLI `bt`（`wifi` と同じ一発の命令）で同じ操作ができ、pairing の確認にも答えられる（D8 の許す人だけ）。
5. Linux の Keiland では BlueZ の上で同じ頁が動く。FreeBSD は「未対応」を表示する（D7）。
6. 規約の全文との照合と回帰。QEMU と実機の証拠を分ける（電波は実機だけ。§10）。

## 2. device の事実

| 事実 | 出典 |
| --- | --- |
| 5330 の Bluetooth は USB 8087:0033（AX211 の CNVi の Bluetooth の半分） | [plan/ws075/phase011/phase.md](../ws075/phase011/phase.md) 53 行、[BUG-158](../bugs/BUG-158.md) 233 行 [F10] |
| 8087:0033 は Intel の TLV 形式の版の応答（Read Version の parameter 0xFF）を返す世代。FreeBSD は enum 名 `IWMBT_DEVICE_9260` の組で扱う（名前は 9260 だが、本物の 9260 の 0x0025 は別の組） | FreeBSD main.c 72〜87 行の表 [F10] |
| USB の Bluetooth の controller の標準の形: interface 0（class E0/01/01）の endpoint 0 の class の control で HCI command、interrupt IN（0x81）で HCI event、bulk IN（0x82）・OUT（0x02）で ACL。interface 1 は isochronous（SCO） | Bluetooth Core 5.4 Vol 4 Part B §2、FreeBSD iwmbt_hw.h 79〜82 行 |
| Intel の bootloader の時だけ、Secure Send（0xFC09）の断片を **bulk OUT** で送り、各断片の応答は **bulk IN** に来る。download の完了の vendor event（0xFF、1 byte 目 0x06）は interrupt IN | FreeBSD iwmbt_hw.c 53〜95 行・380〜404 行 [F1] |
| 5330 の Linux の host では btusb・btintel が動き、起動の時に operational の firmware を load する | [BUG-158](../bugs/BUG-158.md) 233 行 [F11] |
| 5330 の実の descriptor（interface の数、isochronous の alternate、内部の hub の経路）、CNVi・CNVR の id | **未確認**。p002 の最初に T1 経由（lock の下）で 5330 の Linux の host の `lsusb -v -d 8087:0033` と、Linux の btmon か dmesg の版の行を 1 回取る [F24] |
| AX211 の Wi-Fi の driver は BT_CONFIG を mode 3 で送る（Wi-Fi と Bluetooth の共存の設定） | `src/drivers/wifi/intel-ax211/intel-ax211-runtime.c:205-210`。mode 3 の意味は未確認。UAT の前に確かめる（Wi-Fi が Bluetooth を飢えさせないか）[F23] |

kernel の USB の core は control・bulk・interrupt・isochronous の型を持つ（`include/drivers/usb/usb.h:123〜127`）。xHCI は
isochronous の endpoint の型を設定するが Isoch TRB の経路は無い（`src/drivers/pci/pci-xhci.c:2034`）。SCO は範囲の外（§7）[F17]。

## 3. firmware

| 項目 | 内容 |
| --- | --- |
| 世代 | Intel「Solar」。linux-firmware の WHENCE の版 `BT_Solar_REL82122_23.50.26053.82122` |
| file | WHENCE の 4281〜4295 行の Solar の block 全体: `intel/ibt-0040-0041.{sfi,ddc}`、`ibt-0041-0041.{sfi,ddc}`、`ibt-1040-0041.{sfi,ddc}` と symlink `ibt-{0040,1040}-{4150,1050}.{sfi,ddc}`。名前の数字は controller の TLV の CNVi・CNVR の top の id（FreeBSD iwmbt_fw.c 178〜192 行の形 `ibt-%04x-%04x`）。5330 の値は p002 で読み、package は block 全体を入れる（symlink は package の中の link） [F8, F19] |
| 入手 | AX211 の Wi-Fi と同じ linux-firmware の tag `20260410`、commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`（`userland/firmware/intelax211/intelax211-firmware.manifest`）。WHENCE の sha256 は manifest と同じ c282239a…（reviewer が確認） |
| license | `LICENCE.ibt_firmware`（Intel 2014）: 変更しない binary の再配布、notice の同梱、reverse engineering・decompilation・disassembly の禁止、OSI の OS との組み合わせの特許の許諾。kernel に入れず、別の optional の package。.sfi の解析は、file に並ぶ HCI command の枠（opcode・長さ）を読んで断片に分けることと、その中の 0xFC0E の boot の parameter を読むことに限る（firmware の中身は解釈しない）[F8] |
| package | `userland/firmware/intelbt/`（`intelax211/` と同じ形、既定は off、取得の検証、`/lib/firmware/intel/`、license・WHENCE・manifest）。5330 の既定の image に入れるかは D14 |

load の手順（bluetoothd の firmware の部品。全て HCI の vendor command、OGF 0x3F）[F8]:

1. Intel Read Version（0xFC05、parameter 0xFF）で TLV を読む。image の型が operational なら load 済み（6 へ）。bootloader でも
   operational でもなければ失敗。
2. `limited_cce` が 0 でなければ、断片が Command Complete で応えられる別の方式で、対応しない（失敗として表示）。`sbe_type` が 0（RSA）か
   1（ECDSA）でなければ対応しない。
3. bt-usb を bootloader の経路にする（§5.1）。.sfi の先頭の header を Secure Send で送る: RSA は CSS 128 byte・公開鍵 256 byte（2 断片）・
   4 byte を飛ばして署名 256 byte（2 断片）、ECDSA は offset 644 から CSS 128・公開鍵 96・署名 96（FreeBSD iwmbt_hw.c 321〜351 行）。
4. 本体を、file の HCI command の境目でまとめた 4 byte の倍数・最大 252 byte の断片（型 0x01）で送る。途中の 0xFC0E の command の
   parameter（4 byte）が boot の parameter（iwmbt_hw.c 353〜378 行）。送り終えたら interrupt IN の vendor event 0xFF/0x06 を 5 秒まで待つ。
5. Intel Reset（0xFC01、boot の parameter 付き）を送り、vendor event 0xFF/0x02（boot の完了）を待つ。bt-usb は普通の経路に戻す。
6. 版を読み直し、`.ddc` があれば Intel Write DDC（0xFC8B）で record ごとに送る（無ければ飛ばして記録）。Intel Set Event Mask（0xFC52）。
   HCI_Reset の後、普通の controller として初期化する。

失敗の経路: 各 command に 2 秒、download の完了に 5 秒の timeout（FreeBSD と同じ値）。失敗したら USB の reset（bt-usb の ioctl）で
bootloader に戻して 1 回だけやり直し、だめなら controller を「firmware の load に失敗」の状態にして Settings と `bt show` に出す。
firmware の file が無ければ「firmware が要る」の状態。

## 4. 構成の選択

| 案 | kernel | userland | 評価 |
| --- | --- | --- | --- |
| **A（推奨）** | USB の transport だけ（`bt-usb`）。HCI の packet をそのまま運ぶ char device `/dev/btN` と、HID の入力の口 `/dev/hid-host` | daemon `bluetoothd` が firmware の load・HCI・L2CAP・SMP・SDP・GATT・HID host を持つ | kernel が小さい。電波の相手の解析が kernel の外。暗号は §6.6。daemon が落ちても kernel は無事。先例は Android の userspace の stack と BTstack（HCI の transport の上の userland の host stack） [F9] |
| B | HCI・L2CAP を kernel に（Linux の BlueZ の kernel 部、FreeBSD の netgraph） | 管理の daemon と profile | 電波の相手の解析と暗号が kernel に入る。AF_BLUETOOTH の socket の UAPI が大きい |

案 A を推奨する。ws.md の範囲（25 行: firmware の load・HCI の core を kernel に）から変わるので、その承認は D15。入力の遅れ:
HID の report が daemon を通る分は数十 µs で、Bluetooth の遅れは sniff の間隔（数〜十数 ms）が支配する [F24]。

## 5. kernel（案 A）

### 5.1 `bt-usb`（`src/drivers/usb/usb-bt.c`、新規）と `/dev/btN`（D2）

- match: interface class E0/01/01。Intel 8087:0033 は「firmware が要る」印を device の情報に持つ。他の vendor（Realtek・MediaTek）の
  controller も match するが、bluetoothd は vendor の firmware を知らない controller を「この controller は未対応」と示す [F20]。
- 普通の経路: HCI command は endpoint 0 の class の control（bmRequestType 0x20）、ACL は bulk OUT。受け: interrupt IN の event、bulk IN の ACL。
- **bootloader の経路**（ioctl `BT_IOC_SET_BOOTLOADER` で入り、同じ ioctl で出る。daemon が Read Version の答えで決める）: 型 0x01 の packet の
  うち opcode 0xFC09 は bulk OUT へ、bulk IN の受けは型 0x04 の event として渡す。他の command は普通の経路 [F1]。
- packet の形: 1 回の write は H4 の形の 1 packet（先頭 1 byte が型: 0x01 command、0x02 ACL）。1 回の read は 1 packet（0x04 event、0x02 ACL）。
  0x03（SCO）・0x05（ISO）は予約（今は EINVAL）。kernel だけの通知は型 0x80 以上（0xFF は HCI の vendor event の code なので使わない）:
  0x80「controller が reset された」[F20]。
- 境界: command は header 3 byte と parameter 255 byte まで、event は 2+255 byte まで、ACL は controller の ACL の大きさ
  （HCI_Read_Buffer_Size を daemon が ioctl で教える、教わる前は 1021 byte）まで。組み直しは header の長さで行い、長さが合わない USB の
  受けは捨てて数える（悪い device が kernel の buffer を越えさせない）。read の buffer が 1 packet より短いと EMSGSIZE（切らない）[F5, F20]。
- 流れの制御: 受けの queue が満ちたら IN の URB を出し直さない（controller の側に NAK で留まる、backpressure）。packet は捨てない
  （L2CAP の basic mode は再送しないので、ACL を捨てると HID・ATT の data が壊れる）[F15]。
- 開ける: root だけ、同時に 1 つ（bluetoothd）。bluetoothd は開けた後に特権を落とす（§6.5）。
- 寿命と並行: driver の状態は参照数付き。取り外し（detach）は新しい操作を断り、URB を取り消して完了を待ち、read・poll で寝ている者を
  ENODEV で起こし、最後の close で解放する。取り外しの後の write・ioctl は ENODEV。URB の完了の文脈と read・write・poll は 1 つの spin lock
  と waitq で守る [F5]。
- reset の ioctl（`BT_IOC_RESET`）は USB の port の reset を頼む。re-enumerate になれば node は消え、daemon は新しい `/dev/btN` を待つ（§5.3）。
- UAPI の header: `include/uapi/bluetooth.h`。D2 で承認するのは形（packet の型・ioctl の種類・境界）で、struct の配置は p002 の詳細設計で
  review する [F18]。

### 5.2 HID の入力の口 `/dev/hid-host`（D3）

bluetoothd が受けた HID の report を kernel の HID の層に渡し、`/dev/input/eventN` を作る（Linux の uhid に当たる製品の口。
`/dev/input-inject` は試験だけの口なので使わない）。

- 最初の write: 作成（report descriptor 4096 byte まで（hid-report.h の parser の上限）、vid・pid、名前、bus = `BUS_BLUETOOTH`
  （`include/uapi/input.h:255`）、unique_id = 相手の BD_ADDR（再接続でも同じ id、device ごとの設定が続く）、physical path）[F6, F25]。
- 続く write: input report。HOGP の notification には report ID が無いので、daemon が Report Reference の ID を先に付ける（§6.3）[F7]。
- read: output report（キーボードの LED）。**今の kernel に出力の経路は無い**（hid-report.h に encoder 無し、`include/kern/input-device.h`
  の 64〜83 行に出力の callback 無し、usb-hid も送らない）。LED は範囲の外とし、read は今は何も返さない（後の Phase で evdev の EV_LED から作る）[F6]。
- close・相手の切断: device を消す前に押されている全ての key と button を離した report を流す（または device を消して evdev が離す）[F25]。
- **kernel の作業**: report から input device を作る glue は今 `usb-hid.c`（128〜139、970〜1117、1424〜1435、1556〜1588 行）にあり、
  i2c-hid にも写しがある。これを USB・I2C・hid-host が共有する module（`src/drivers/generic/hid-input.c`）に分ける refactor が要る
  （全ての USB のキーボード・マウスの回帰の危険）。p005 の前半に置く [F6]。
- 攻撃面: report descriptor が電波から来る。`hid-report.c` の host の fuzz 試験（ランダムと変異の descriptor・report）を p005 に置く [F6]。
- 開ける: root だけ。誰がキーボードを作れるかは、bluetoothd が開けた後に特権を落として口を持ち続けることで絞る（§6.5）。

### 5.3 suspend・resume と USB の取り外し

USB の core は driver の suspend・resume を呼ばない（usb.c・usb-hid.c に呼び出しが無い）。sleep は `kern/sleep.c` →
`drv_pci_suspend_all` → `xhci_suspend`（`pci-xhci.c:6040-6090`）で、resume は xHCI が状態を戻して転送が ring に残るか、reset と
re-enumerate で device が detach・attach し直すか（6087〜6125 行）。設計はこの 2 つに合わせる [F4]:

- 状態が戻る時: bt-usb には何も見えない。相手は supervision timeout で link を切るので、bluetoothd は切断の event を受け、HID の
  device を消し（key を離す）、再接続を待つ（§6.4）。
- re-enumerate の時: 古い `/dev/btN` は取り外し（ENODEV）、新しい node が現れる。bluetoothd は `/dev/system` の
  `KERN_SYSTEM_EVENT_USB`（`include/uapi/system.h:307`）で現れたことを知り、新しい controller として firmware を load し直し、bond 済みの
  device の再接続を始める。
- 架空の suspend の hook（第 1 版の「URB を止めて reset の event を返す」）は作らない。USB の core に suspend の通知を足すのは別の仕事。

## 6. userland の `bluetoothd`（D4・D16）

### 6.1 部品

| 部品 | 内容 |
| --- | --- |
| transport | `/dev/btN` の読み書き、Num_HCI_Command_Packets による command の流れ、ACL の buffer の数の管理、0x80 の reset の通知で初期化のやり直し |
| firmware | §3 |
| HCI core | reset、BD_ADDR、Read Local Supported Commands（controller の暗号の command の有無、§6.6）、BR/EDR の inquiry と page scan、LE の scan、接続と切断、名前、暗号化の開始、filter accept list と resolving list |
| L2CAP | BR/EDR の signalling（CID 0x0001、Information Request、basic mode の channel、HID の PSM 0x11・0x13、SDP の 0x01）、LE の固定の channel（ATT 4、signalling 5: Connection Parameter Update Request に答える、SMP 6） [F7] |
| SMP・SSP | §6.2 |
| SDP | client（HID の service record から report descriptor と PSM） |
| GATT | client（HOGP: HID service 0x1812、Report Map の長い読み（Read Blob）、各 Report の Report Reference（ID と型）、CCC の有効化、Protocol Mode（report mode）、Battery service）。HID の characteristic を読む前に暗号化（security level）を満たす [F7] |
| HID host | BR/EDR の HID と HOGP の report を `/dev/hid-host` へ。§6.4 の再接続 |
| 鍵の保存 | §6.3 |
| 口 | §6.5 |
| CLI | `bt show`・`bt scan`・`bt pair ADDR`・`bt connect`・`bt disconnect`・`bt forget`。`bt pair` は対話の agent として確認・passkey に答える（D8 の許す人だけ）[F21] |

### 6.2 pairing（D10）

- BR/EDR の Secure Simple Pairing は **controller が行う**（f1・g・f2・f3 と ECDH は LMP の中）。host は HCI の event に答えるだけ:
  IO Capability Request・Response、User Confirmation Request、User Passkey Request・Notification、Link Key Notification [F2]。
- LE は host の SMP: LE Secure Connections（Just Works・numeric comparison・passkey）。D10 の答えによって LE の legacy pairing（c1・s1）と
  BR/EDR の legacy の PIN の pairing を受けるか断る。安いキーボード・マウスの中には legacy だけの物がある [F7]。
- 製品では相手の debug の P-256 の鍵（仕様の公開の鍵の組）を断る（LE の SMP と、BR/EDR の Link Key Notification の key type）[F3]。

### 6.3 鍵の保存

- 置き場所: `/var/db/bluetooth/<controller の BD_ADDR>/<相手の identity address>`、持ち主は bluetoothd の専用の account、0600 [F16, F22]。
- 書き方: 一時 file・fsync・rename（電源が切れても半端な file を残さない）[F22]。
- 自分の IRK と privacy: controller ごとに IRK を作って保存し、LE は resolvable private address を使う（D11 で既定を決める）[F22]。
- 忘れる: 鍵の削除、HID の Virtual Cable Unplug（BR/EDR）、filter accept list と resolving list から消す [F22]。
- bond は system 共有か人ごとか: D9（system 共有なら greeter と console でもキーボードが使える）[F18]。

### 6.4 再接続と切断

- BR/EDR: bond 済みの HID の device は相手から接続してくる。page scan を有効にし、bond 済みで暗号化した link の HID の PSM だけ受ける [F3, F7]。
- LE: host が始める。bond 済みの HOGP の device を filter accept list と resolving list に入れ、背景の scan（または auto-connect）で接続する [F7]。
- 切断（supervision timeout を含む）: `/dev/hid-host` の device を消して key を離す。作り直す時も unique_id は同じ [F25]。

### 6.5 口と特権の分離（D4・D8・D16）[F3, F16]

- Unix socket `/var/run/bluetoothd.sock`。networkd と同じ 1 frame 往復の request と SUBSCRIBE の監視
  （`userland/desktop/libkeiland-backend-zedbsd/network-zedbsd.c` の形）。
- 利用者の区別は networkd と同じ SO_PEERCRED（`userland/base/networkd/main.c:3256-3330`・`6762-6790`）: root、許された group
  （仮に `bluetooth`）の人、それ以外。状態の読みは誰でも、電源・scan・接続・切断は許された人、pairing・忘れるは D8 の答えの人。
- active な session の特定: sessiond の今の seat の持ち主（compositor が自分の session を名乗って agent になる）。agent（pairing の確認に
  答える者）は同時に 1 つで、compositor の agent を優先し、CLI の `bt pair` は compositor が無い時（console・SSH）だけ D8 の許す人が agent になる。
- 相手から始まる pairing は、pairing の mode（Settings で「device を足す」を開いている間、または `bt pair` の間）でなければ断る。Just Works
  でも人の同意を取る（確認の窓）。
- 特権の分離: bluetoothd は root で起動し、`/dev/btN` と `/dev/hid-host` と socket を開いた後に専用の account に落ちる。全ての parser
  （HCI・L2CAP・SDP・ATT・SMP）は長さを検査し上限を持つ。

### 6.6 暗号（D5）[F2]

host が要るのは LE の SMP だけ: AES-128（e、ah）と AES-CMAC（f4・f5・f6・g2）、ECDH P-256。legacy pairing を受けるなら c1・s1
（AES-128）。BR/EDR は controller が行う。

| 案 | 内容 |
| --- | --- |
| **b1（推奨）** | 自前の AES-128・AES-CMAC（zedBSD には kernel の Wi-Fi の AES-128・SHA-1・HMAC がある、`src/kern/net/wifi/wlan-crypto.h:45-83`。userland 用に同じ形で書く）と、controller の HCI_LE_Read_Local_P-256_Public_Key・HCI_LE_Generate_DHKey（Core Vol 4 Part E §7.8.36・§7.8.37）。AX211 の対応は Read Local Supported Commands で p003 に確かめる（無ければ b2） |
| b2 | 自前の AES・CMAC と自前の P-256（定数時間の実装と試験が要り、危険が大きい） |
| a | package の OpenSSL の libcrypto。**base の daemon が外部の実装に依存するので、master-design-policy.md の §2.1（20〜23 行、base は全て再実装）の例外で、§9 の見直しの境界（262 行）**。OpenSSL の package は optional で `no-asm` の build |

## 7. 音声（A2DP）と範囲の外 [F17]

A2DP（ヘッドホン）は AVDTP、SBC の encoder、audiod の出力の振り分けが要る。SBC の encoder は今の package の libavcodec に無い
（`--disable-encoders`、WS122 の監査済みの構成）ので自前か構成の変更、audiod（base）が LGPL の package に依存するなら §6.6 の a と同じ
方針の例外になる。SCO（通話）は xHCI の Isoch TRB の経路から要る。どれも HID とは別の大きな仕事なので、A2DP は **別の WS** にする案を
D1 で尋ねる。LE Audio、OBEX、PAN は範囲の外。

## 8. desktop

### 8.1 backend の口と API

- `kl_backend_bluetooth`（`userland/desktop/libkeiland-backend/keiland-backend.h`、`kl_backend_network` と同じ形）: 状態（controller の有無、
  電源、firmware が要る・load に失敗・未対応の controller、scan 中）、device の一覧（address、名前、種類の icon、ペア済み、接続中、電池、
  対応する profile の有無）、request（電源、scan の lease、pair、接続、切断、忘れる）、pairing の確認の event と返事。
- 「同時に 1 request」（keiland-backend.h:157-161）の例外: pairing の確認の返事は、pair の request が出ている間に送れる [F14]。
- Settings（app）が使う公開 API `kl_system_bluetooth_*`（`keiland.h`、interface の版を 31 から上げる、keiland.h:48、libkeiland/system/、
  guardrail.md:36）、`kl_system_manager_v1` の版 9 と `kl_system_bluetooth_v1`（since 9 の定数と KL_SYSTEM_CAPABILITY の bit、
  `kl-system-protocol.h:189-217`）、OS の境界の検査（check.sh）の更新 [F14]。

### 8.2 OS ごと

| OS | 実体 |
| --- | --- |
| zedBSD | `libkeiland-backend-zedbsd/bluetooth-zedbsd.c`: bluetoothd の socket |
| Linux | `libkeiland-backend-linux/bluetooth-linux.c`: BlueZ の D-Bus（Adapter1・Device1・AgentManager1・Agent1）。**今の `dbus-linux.c` では足りない**: 引数は s/o/g/u/b だけ（786〜792 行）、数か文字列だけを読み、返事と signal だけを扱う（248〜263・951〜957 行）。variant・dict（GetManagedObjects の `a{oa{sa{sv}}}`、PropertiesChanged）の解析と、Agent1 の export（METHOD_CALL を受ける）を足す。Debian の org.bluez の D-Bus の policy（非 root の人が Agent を登録できるか）を p007 で確かめる [F13] |
| FreeBSD | 最初は `unsupported/`（D7） |

### 8.3 compositor と Settings

- compositor が pairing の確認の窓（数字の一致、passkey の入力・表示、Just Works の同意）を出し、§6.5 の agent になる。
- Settings の Bluetooth の頁: 今の stub（`userland/desktop/settings/pages.c:28` の `se_soon_draw`）を置き換える。
- system bar: Bluetooth の icon と menu（Wi-Fi の menu と同じ形）。

## 9. ユーザーの判断が要る点 [F18]

| # | 判断 | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 最初の profile の範囲 | A: キーボード・マウス（BR/EDR HID と LE HOGP）だけ。B: A とヘッドホン（A2DP）をこの WS で | **A**。A2DP は別の WS（§7） |
| D2 | UAPI `/dev/btN`（HCI の packet の char device、bootloader の経路の ioctl、reset の ioctl、`include/uapi/bluetooth.h`） | 形を承認し struct は p002 で review / 案 B（kernel に HCI・L2CAP と AF_BLUETOOTH） | 形を承認 |
| D3 | UAPI `/dev/hid-host`（userland の HID の report を kernel の HID の層で input device にする製品の口）と、usb-hid の glue の共有の module への refactor | 形を承認し struct は p005 で review / 作らない（案 B） | 形を承認 |
| D4 | root で起動する daemon `bluetoothd` と socket の口、CLI `bt` | §6.5 の形 | §6.5 の形 |
| D5 | 暗号 | b1 / b2 / a（方針の例外） | **b1** |
| D6 | firmware の package `intelbt`（`LICENCE.ibt_firmware`、Solar の block 全体） | AX211 の Wi-Fi・i915 と同じ optional の package | 作る |
| D7 | FreeBSD の Keiland | 最初は未対応の表示 / hccontrol・bthidd を包む | 未対応の表示 |
| D8 | 誰が pairing・接続・確認できるか | wheel だけ / `bluetooth` group / active な session の人なら誰でも。GUI の session が無い時（console・SSH）は許された group の人だけ | active な session の人（確認は compositor）、console・SSH は wheel |
| D9 | bond は system 共有か人ごとか | system 共有（greeter・console でもキーボードが使える）/ 人ごと（Wi-Fi の鍵と同じ、guardrail.md:36） | system 共有（入力の device なので） |
| D10 | pairing の安全の方針 | Secure Connections だけ / legacy（LE の c1・s1、BR/EDR の PIN）も受ける / 受けるが警告を出す | legacy も受け、Settings に「古い方式」と出す |
| D11 | 電源の既定 | 起動時 on/off、前の状態を記憶、discoverable（既定 off）、機内 mode の扱い。Dell の BIOS の Bluetooth の switch が on であること | 前の状態を記憶（初回 on）、discoverable は pairing の mode の間だけ、LE は private address |
| D12 | UAT の device | ユーザーの手元の BR/EDR と LE のキーボード・マウス（受け入れ 3 に両方が要る） | 機種を教えてほしい |
| D13 | 5330 の試験の host の変更（§10.3） | btusb・btintel を blacklist して電源を入れ直す（bootloader の経路を試せる）/ guest が Intel reset で bootloader に戻す（試験 host は変えない） | まず後者、だめなら前者 |
| D14 | `intelbt` を 5330 の既定の image に入れるか | 入れる / 入れない（firmware の package は既定 off） | 入れる |
| D15 | ws.md の範囲の変更（firmware の load と HCI core を kernel から userland へ） | 承認 / 第 1 版の ws.md の通り kernel に | 承認 |
| D16 | bluetoothd の特権の分離 | 開けた後に専用の account に落ちる / root のまま | 落ちる |

## 10. Phase と試験

### 10.1 Phase（案。§9 の答えの後に確定し、各 Phase は着手の前に詳細設計と design-reviewer）[F19]

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | 5330 の descriptor・版（CNVi・CNVR の id）を T1 で取る。kernel の `bt-usb`（普通と bootloader の経路、寿命、境界）と `/dev/btN`（D2）。小さな試験の道具（`/dev/btN` で Read Version と HCI_Reset だけ）。host の試験（組み直しと境界、悪い device、取り外し） | D2 |
| p003 | firmware の package `intelbt`（Solar の block 全体）。bluetoothd の transport・firmware の load（§3、失敗の経路）・HCI core・scan、CLI `bt show`・`bt scan`、Read Local Supported Commands の記録。T1 の passthrough で load と scan | p002、D4・D6・D13・D14 |
| p004 | L2CAP（BR/EDR・LE の signalling）、SSP の event の処理、LE の SMP（D10）、暗号（D5）、鍵の保存、特権の分離、socket の口の権限（D8・D16）。host の試験（仕様の sample data、偽の controller） | p003、D5・D8・D10・D16 |
| p005 | usb-hid の glue の共有の module への refactor と USB の回帰、`/dev/hid-host`（D3）、hid-report.c の fuzz、SDP・GATT client、HID host（BR/EDR と HOGP）、再接続、切断で key を離す | p004、D3 |
| p006 | desktop: backend の口、zedBSD の backend、API と protocol の版、Settings の頁、system bar、pairing の確認の窓 | p005 |
| p007 | Linux の backend（D-Bus の拡張、BlueZ）、FreeBSD の未対応の表示 | p006 |
| p008 | UAT（5330、ユーザーの device: pairing・入力・再接続・suspend の後・忘れる）。Wi-Fi との共存（F23）の確認 | p006、D12 |
| p009 | 規約の全文との照合と回帰 | p002〜p008 |

見積もり（目安、A の範囲）: 25〜35 日（userland の BR/EDR+LE の host stack、hid の refactor、desktop、BlueZ の backend の D-Bus の拡張、
偽の controller を含む）。第 1 版の 12〜15 日は少なすぎた。

### 10.2 試験の方法 [F12]

| 層 | 試験 |
| --- | --- |
| 暗号 | AES-CMAC を RFC 4493 の vector、SMP の f4・f5・f6・g2・ah・c1・s1 を Bluetooth Core Vol 3 Part H Appendix D の sample data |
| bt-usb | host の試験: 組み直し（短い・長い・壊れた packet、悪い device の長さ）、queue の満ち（backpressure で捨てない）、bootloader の経路の振り分け、取り外しの間の read・poll（ENODEV で起きる）、EMSGSIZE |
| hid | hid-report.c の fuzz（ランダムと変異）、`/dev/hid-host` の誤用（作成の前の report、壊れた descriptor、作成と破棄の繰り返し、切断で key が離れる）、refactor の後の USB HID の回帰（今の host の試験） |
| 状態機械 | 偽の controller（`/dev/btN` の代わりの socketpair で HCI の event を返す試験の program）で HCI・L2CAP・SMP・GATT・SDP。相手の debug の鍵を断る。2 つの bluetoothd を偽の電波でつなぐ試験は LMP・SSP をする偽の controller が要り大きいので、HCI の event の台本で相手を演じる形にする |
| Linux の backend | WS131 の Debian の QEMU+KVM の guest で、仮想の controller（hci_vhci と btvirt、guest の Linux の中）に BlueZ を動かし、backend が一覧・pairing の台本を通るか |
| FreeBSD | WS109 の FreeBSD の guest で「未対応」の表示 |
| QEMU（zedBSD） | QEMU に Bluetooth の controller の emulation は無い（版は未確認）。boot test と、controller が無い時に daemon と Settings が「無い」と正しく言うこと |
| 実機 | §10.3 と UAT（p008） |

### 10.3 実機の試験 [F11]

- 5330 の passthrough の QEMU に `-device usb-host,vendorid=0x8087,productid=0x0033` で controller を渡す（T1、lock の下）。CNVi の Wi-Fi の
  vfio（00:14.3）とは別の run で試す（相互作用は未確認）。
- Linux の host が先に operational の firmware を load するので、そのままでは bootloader の経路を試せない。D13 の答えで、guest が Intel reset で
  bootloader に戻すか、host の btusb・btintel を blacklist して電源を入れ直す（試験の host の変更、ユーザーと T1 の承認）。
- QMP の `device_del` で usb-host を抜く取り外しの試験、suspend・resume の 2 つの結果（§5.3）は T1 で。
- 電波の相手が要る確かめは UAT（p008）。
