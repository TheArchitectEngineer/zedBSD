<!-- awesome-plan project=zedbsd record=ws143-design -->

# WS143 設計: Bluetooth（ws143-p001）

版: 2026-10-05 第 1 版（P1 generation18、q752）。design-reviewer の review の前。
目標は [ws.md](ws.md) の単一目標: Settings の Bluetooth の頁を実体にし、5330 の AX211 の Bluetooth（USB 8087:0033）で device を
見つけ、pairing・接続・切断ができる。

## 1. 受け入れ（案。§9 の D1 の答えで確定）

1. 5330 で起動の後に Bluetooth の controller が使える状態になる（firmware を load し、controller の BD_ADDR を読める）。
2. Settings の Bluetooth の頁と system bar で、電源の on/off、周りの device の一覧（名前・種類）、pairing、接続、切断、忘れるができる。
3. 推奨の範囲（D1 の案 A）: Bluetooth のキーボードとマウス（BR/EDR の HID と LE の HOGP）が pairing の後に入力に使え、再起動の後も
   再接続する。
4. CLI `bt`（`wifi` と同じ形の一発の命令）で同じ操作ができる。
5. Linux の Keiland では BlueZ の上で同じ頁が動く。FreeBSD は「未対応」を正しく表示する（§8.4）。
6. 規約の全文との照合と回帰。QEMU の証拠と実機の証拠を分ける（Bluetooth の電波は実機だけ。§10.2）。

## 2. device の事実

| 事実 | 出典 |
| --- | --- |
| 5330 の無線は Intel AX211（CNVi）。Wi-Fi は PCIe、Bluetooth は USB の半分で 8087:0033 | [ws.md](ws.md)、AX211 の Wi-Fi の driver（`src/drivers/wifi/intel-ax211/`） |
| 8087:0033 は Intel の TLV 世代（9260 以降）の boot の手順を使う | FreeBSD `usr.sbin/bluetooth/iwmbtfw/main.c`（main 2026-10-05 取得、BSD-2-Clause）の device 表 `{ 0x8087, 0x0033, IWMBT_DEVICE_9260 }`（85 行）と TLV の分岐（222・632 行） |
| USB の Bluetooth の controller の標準の形: interface 0 は class E0/01/01、endpoint 0 の control で HCI command、interrupt IN で HCI event、bulk IN/OUT で ACL。interface 1 は isochronous（SCO、alternate setting ごとの帯域） | Bluetooth Core 5.4 Vol 4 Part B（USB Transport Layer）§2 |
| 5330 の実の descriptor（interface の数、isochronous の alternate、内部の hub の経路） | **未確認**。UAT の中は 5330 を使えないので、UAT の後に 5330 の Linux の host で `lsusb -v -d 8087:0033` を 1 回取る（p002 の最初） |

kernel の USB の core は control・bulk・interrupt・isochronous の型を持ち（`include/drivers/usb/usb.h:123〜127`）、xHCI は
isochronous の endpoint の型も設定する（`src/drivers/pci/pci-xhci.c:2034`）。isochronous の転送の経路の実績は無い（SCO は §7 で後に回す）。

## 3. firmware

| 項目 | 内容 |
| --- | --- |
| 世代 | Intel「Solar」（AX211 の Bluetooth）。linux-firmware の WHENCE の版 `BT_Solar_REL82122_23.50.26053.82122` |
| file | `intel/ibt-0040-0041.sfi`（firmware）と `intel/ibt-0040-0041.ddc`（device の設定）。別の stepping の `ibt-1040-0041.*`。名前の数字は controller の TLV の版（CNVi・CNVR の id）から決まるので、5330 の値は p003 で読んで確かめ、両方を package に入れる |
| 入手 | AX211 の Wi-Fi と同じ linux-firmware の tag `20260410`、commit `dc85ccedc9c973682fbcf4d628ca61174bcc3120`（`userland/firmware/intelax211/intelax211-firmware.manifest`）。WHENCE の 4283〜4292 行（2026-10-05 取得） |
| license | `LICENCE.ibt_firmware`（Intel、2014）: 変更しない binary の再配布を許す、notice の同梱、reverse engineering の禁止、OSI の OS との組み合わせの特許の許諾。i915 の DMC・AX211 の Wi-Fi と同じ扱い（kernel に入れず、別の optional の package） |
| package | `userland/firmware/intelbt/`（`intelax211/` と同じ形: 既定は off、取得の検証、`/lib/firmware/intel/` へ、license・WHENCE・manifest）。§9 の D6 |

load の手順（protocol の事実。実装は新規に書き、外部の source を写さない）:

1. HCI の vendor command Intel Read Version（OGF 0x3F、OCF 0x005、parameter 0xFF）で TLV の版を読む。image の型が bootloader なら
   load へ、operational ならもう load 済み。
2. Secure Send（OCF 0x009）で .sfi の先頭の CSS header と公開鍵と署名を送り、続いて本体を command の境目ごとの断片で送る。
3. Intel Reset（OCF 0x001）で boot の address を渡して起動させ、vendor の event（boot の完了）を待つ。
4. DDC の load（Intel Write DDC、OCF 0x08B）で .ddc の各 record を送る。
5. HCI_Reset の後、普通の controller として使う。

出典: FreeBSD iwmbtfw（BSD-2-Clause。手順・定数を読んで確かめる参照。code は写さない）と、Bluetooth Core の vendor command の枠
（Vol 4 Part E §5.4.1、OGF 0x3F）。Linux の btintel.c（GPL-2.0）は読まない（license の境界）。

## 4. 構成の選択

| 案 | kernel | userland | 評価 |
| --- | --- | --- | --- |
| **A（推奨）** | USB の transport だけ（`bt-usb`）。HCI の packet をそのまま運ぶ char device `/dev/btN` | daemon `bluetoothd` が firmware の load・HCI・L2CAP・SMP・SDP・GATT・HID host を持つ | kernel が小さい。暗号（ECDH・AES-CMAC）を userland に置ける。daemon が落ちても kernel は無事。FreeBSD の iwmbtfw・bthidd と同じ分け方 |
| B | HCI・L2CAP を kernel に（Linux の BlueZ の kernel 部、FreeBSD の netgraph の形） | 管理の daemon と profile | kernel が大きくなり、暗号を kernel に要る。socket の family（AF_BLUETOOTH）の UAPI が大きい |

案 A を推奨する。入力の遅れ（HID の report が daemon を通る）は 1 report が数十 byte で、daemon の一回の read と write が足すのは
数十 µs。USB の HID の 8 ms の poll より十分に小さい。

## 5. kernel（案 A）

### 5.1 `bt-usb`（`src/drivers/usb/usb-bt.c`、新規）

- match: interface class E0/01/01（標準の Bluetooth の controller）。Intel の 8087:0033 は firmware の load が要る印を device の情報に持つ。
- 送る: HCI command は endpoint 0 の class の control（bmRequestType 0x20）、ACL は bulk OUT。受ける: interrupt IN の event、bulk IN の ACL。
  受けの URB を常に 1〜2 個出しておく。packet の切れ目は USB の short packet と HCI の header の長さで組み直す。
- `/dev/bt0`…（controller ごと）: 1 回の write は H4 の形の 1 packet（先頭の 1 byte が型: 0x01 command、0x02 ACL）、1 回の read は
  1 packet（0x04 event、0x02 ACL）。`poll` で読める時を知らせる。開けるのは root だけで、同時に 1 つ（bluetoothd）。受けの queue は
  上限付き（溢れたら古い ACL を捨てて数え、event は捨てない）。
- ioctl: controller の情報（bus、vid・pid、firmware が要るか、状態）、USB の reset。
- 取り外し・suspend・resume: open 中の取り外しは read が `ENODEV`。suspend の前に URB を止め、resume の後は daemon に「reset された」と
  知らせ（read が特別な event を返す）、daemon が初期化をやり直す。
- 新しい UAPI: `include/uapi/bluetooth.h`（§9 の D2）。

### 5.2 HID の入力の口（`/dev/hid-host`、新規。§9 の D3）

bluetoothd が受けた HID の report を、kernel の既存の HID の parser（`src/drivers/generic/hid-report.c`、USB HID と I2C-HID が共有）に
渡して `/dev/input/eventN` を作る。Linux の uhid に当たる。

- 最初の write: 作成（report descriptor、vid・pid、名前、bus = `BUS_BLUETOOTH`（`include/uapi/input.h:255`））。kernel は descriptor を
  hid-report.c で解析し、input device を作る。
- 続く write: input report（report ID 付き）。read: output report（キーボードの LED）を daemon へ返す。
- close で device を消す。root だけ。`/dev/input-inject`（試験だけの注入）とは別の、製品の口。

## 6. userland の `bluetoothd`（§9 の D4）

root の daemon。`audiod` と同じ service の形（`userland/base/audiod/audiod.service`）。部品:

| 部品 | 内容 |
| --- | --- |
| transport | `/dev/bt0` の読み書き、command の流れの制御（Num_HCI_Command_Packets）、ACL の buffer の数の管理 |
| firmware | §3 の Intel の load（firmware の file が無ければ controller を「firmware が要る」状態で示し、Settings が理由を出す） |
| HCI core | reset、BD_ADDR、BR/EDR の inquiry と LE の scan、接続と切断、名前の読み、暗号化の開始 |
| L2CAP | BR/EDR の basic mode（HID の PSM 0x11・0x13、SDP の 0x01）、LE の固定の channel（ATT 4、SMP 6） |
| 鍵 | BR/EDR の Secure Simple Pairing（Secure Connections を優先）、LE の SMP（LE Secure Connections、Just Works と passkey）、link key・LTK・IRK の保存（`/var/db/bluetooth/<controller>/<peer>`、0600 root）、resolvable private address の解決 |
| SDP | client（HID の service record から report descriptor と PSM） |
| GATT | client（HOGP: HID service 0x1812、Report Map、Report の CCC の有効化、Battery service） |
| HID host | BR/EDR の HID と HOGP の report を §5.2 へ。再接続（ペアの device が来たら受ける） |
| 口 | Unix socket `/var/run/bluetoothd.sock`。networkd と同じ 1 frame 往復の request と、SUBSCRIBE の監視（`userland/desktop/libkeiland-backend-zedbsd/network-zedbsd.c` の形） |
| CLI | `bt`（`userland/base/wifi` と同じ一発の命令）: `bt show`・`bt scan`・`bt pair ADDR`・`bt connect`・`bt disconnect`・`bt forget` |

暗号（§9 の D5）: AES-128（LE の e）、AES-CMAC（f4・f5・f6・g2）、HMAC-SHA-256（BR/EDR の Secure Connections の f1・g・f2・f3）、
ECDH P-256。案 a: 既存の package の OpenSSL（`userland/packages/security/openssl`）の libcrypto を使う（推奨: 実績のある定数時間の
P-256）。案 b: 自前の小さな実装（AES・SHA-256 は小さいが、P-256 の定数時間の実装は危険が大きい）。

## 7. 音声（A2DP）と後に回す物

A2DP（ヘッドホン）は AVDTP、SBC の encoder、audiod の出力の振り分けが要り、HID より大きい（目安で HID の 2 倍）。SBC の encoder は
既存の package の libavcodec（`userland/packages/multimedia/libavcodec`、LGPL）か自前。SCO（通話の音声）は isochronous の転送が要る。
D1 で範囲を決める。LE Audio、file の転送（OBEX）、tethering（PAN）は範囲の外。

## 8. desktop

### 8.1 backend の口

`kl_backend_bluetooth`（`userland/desktop/libkeiland-backend/keiland-backend.h` に、`kl_backend_network` と同じ形で）:
状態（controller の有無、電源、firmware が要る、scan 中）、device の一覧（address、名前、種類の icon（class of device・appearance）、
ペア済み、接続中、電池）、request（電源、scan の開始と停止（Wi-Fi の scan と同じ lease）、pair、接続、切断、忘れる）、pairing の
確認（数字の一致の確認、passkey の入力・表示）の event と返事。

### 8.2 OS ごと

| OS | 実体 |
| --- | --- |
| zedBSD | `libkeiland-backend-zedbsd/bluetooth-zedbsd.c`: bluetoothd の socket |
| Linux | `libkeiland-backend-linux/bluetooth-linux.c`: BlueZ の D-Bus（`org.bluez` の Adapter1・Device1・Agent1。既存の `dbus-linux.c` を使う） |
| FreeBSD | 最初は `unsupported/`（FreeBSD の Bluetooth の stack には BlueZ に当たる管理の daemon が無い。hccontrol・bthidd を包むのは後） |

### 8.3 compositor と Settings

- `kl_system_manager_v1` に `get_bluetooth(new_id kl_system_bluetooth_v1)` を足す（`kl_system_network_v1` と同じ形、
  `userland/desktop/keiland/kl-system-protocol.h`）。pairing の確認は compositor が窓を出して答える（daemon が知らない人に答えさせない:
  確認の要る request は active な session の人の操作だけ）。
- Settings の Bluetooth の頁: 今の stub（`userland/desktop/settings/pages.c:28` の `se_soon_draw`）を置き換える。
- system bar: Bluetooth の icon（電源・接続の状態）と menu（Wi-Fi の menu と同じ形）。

## 9. ユーザーの判断が要る点

| # | 判断 | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 最初の profile の範囲（ws.md「p001 でユーザーと決める」） | A: キーボード・マウス（BR/EDR HID と LE HOGP）。B: A とヘッドホン（A2DP）。C: A の後に B を別の段で | **A**（ベータ2）、A2DP は C の後の段 |
| D2 | UAPI: `/dev/btN`（HCI の packet をそのまま運ぶ char device）と `include/uapi/bluetooth.h` | 案 A（§4）の形 / 案 B（kernel に HCI・L2CAP と AF_BLUETOOTH の socket） | 案 A の `/dev/btN` |
| D3 | UAPI: `/dev/hid-host`（userland の HID の report を kernel の HID の parser で input device にする、製品の口） | 作る / daemon が `/dev/input-inject` を使う（試験だけの口なので不可） / kernel に HID host を置く（案 B） | 作る |
| D4 | root の daemon `bluetoothd` とその socket の口、CLI `bt` | §6 の形 | §6 の形 |
| D5 | 暗号の実装 | a: package の OpenSSL の libcrypto（base の daemon が package に依存する） / b: 自前 | a |
| D6 | firmware の package `intelbt`（`LICENCE.ibt_firmware`） | AX211 の Wi-Fi・i915 と同じ optional の package | 作る |
| D7 | FreeBSD の Keiland | 最初は未対応の表示 / 最初から hccontrol・bthidd を包む | 未対応の表示 |

## 10. Phase と試験

### 10.1 Phase（案。D1〜D7 の答えの後に確定し、各 Phase は着手の前に詳細設計と design-reviewer）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | firmware の package `intelbt`、kernel の `bt-usb` と `/dev/btN`（D2）、5330 の descriptor の確認 | D2・D6 |
| p003 | bluetoothd の transport・firmware の load・HCI core・scan、CLI `bt show`・`bt scan` | p002、D4 |
| p004 | L2CAP・SMP・SSP の pairing、鍵の保存、暗号（D5）。host の試験（仕様の sample data） | p003、D5 |
| p005 | kernel の `/dev/hid-host`（D3）、SDP・GATT client、HID host（BR/EDR と HOGP）、再接続 | p004、D3 |
| p006 | desktop: backend の口、zedBSD の backend、`kl_system_bluetooth_v1`、Settings の頁、system bar、pairing の確認の窓 | p005 |
| p007 | Linux の backend（BlueZ）、FreeBSD の未対応の表示 | p006 |
| p008 | 規約の全文との照合と回帰 | p002〜p007 |

D1 が B・C なら A2DP の Phase（AVDTP・SBC・audiod の出力）を p006 の後に足す。見積もり（目安）: A の範囲で 12〜15 日。

### 10.2 試験の方法

- host: SMP・SSP の関数を Bluetooth Core の sample data（Vol 3 Part H Appendix D、Vol 2 Part G）、AES-CMAC を RFC 4493 の vector、
  P-256 の ECDH を仕様の debug key の組で照合する。HCI・L2CAP・SMP・GATT の状態機械は、host の上の偽の controller（試験の
  program が `/dev/btN` の代わりの socketpair で HCI の event を返す）で動かす。2 つの bluetoothd を偽の電波でつなぐ試験も作る。
- QEMU: QEMU には Bluetooth の controller の emulation が無い（QEMU 5.0 で削除）。QEMU の証拠は boot test と、daemon が controller
  無しで「無い」と正しく言うことだけ。
- 実機: 5330 の passthrough の QEMU に `-device usb-host,vendorid=0x8087,productid=0x0033` で controller を渡す（T1 経由、lock の下）。
  電波の相手（キーボード・マウス）を要する確かめは UAT（ユーザーの手元の device で pairing・入力・再接続）。
