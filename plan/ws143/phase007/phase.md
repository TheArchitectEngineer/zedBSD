<!-- awesome-plan project=zedbsd record=ws143-p007 -->

# ws143-p007: Linux の Bluetooth の backend（BlueZ の D-Bus）と FreeBSD の未対応の表示

Parent: [WS143](../ws.md)
Status: planning（2026-10-13 P2 q962: 詳細設計の第 1 版。WS143 §10.1 の規則で design-reviewer を通してから実装する）
Phase disposition: normal
Queue: q962（P2、2026-10-13）。承認: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」、WS143 の design.md §9（D7・D13 ほか、2026-10-05 夕 ユーザー「全部推奨」）と §10 の p007、2026-10-08 ユーザー「Linux・FreeBSD は 10/13 以降」。
依存: p006（desktop の backend の口 `kl_backend_bluetooth_*`、compositor の `bluetooth-shell.c`・`bluetooth-ask.c`・`bluetooth-bar.c`、Settings の頁。test-wait だが口は main にある）。

## 1. 範囲

- Linux の Keiland: `userland/desktop/libkeiland-backend-linux/bluetooth-linux.c`（新）で、`keiland-backend.h` の Bluetooth の口（p006 §2）を BlueZ（`org.bluez`、system bus）の上に作る。今の Linux の build は `unsupported/bluetooth-unsupported.c`（いつも `KL_BACKEND_BT_ABSENT`）を link しているので、それと差し替える。
- そのための `dbus-linux.c` の拡張（§4）: 待たない method call と、その返事・error の名前の受け取り、`v`（variant）の組み立て、`a{sv}` と variant の読み、Introspect の XML から子の node の名前を取る。
- FreeBSD の Keiland: 今のまま `unsupported/`（D7「未対応の表示」）。Settings の頁は state が `KL_BACKEND_BT_ABSENT` の時の文（p006）で未対応を示す。この Phase では FreeBSD の source を変えない（確かめだけ）。
- 範囲の外: zedBSD の電話の機能（`KL_BACKEND_BT_PAIR_PHONE`・`kl_backend_phone_*`、ws197）は Linux では `ENOTSUP`（phone の口は今の unsupported のまま）。A2DP・PAN（D1、別の WS）。文字・数を打って答える pairing（legacy の PIN の入力・`RequestPasskey`、§5.4）。

## 2. 事実（確かめた物）

- Debian 13 の bluez は 5.82（host の `dpkg-query`）。D-Bus の policy（host の `/usr/share/dbus-1/system.d/bluetooth.conf`、2026-10-13 読んだ）: `<policy context="default"><allow send_destination="org.bluez"/></policy>` で、**root でない利用者も org.bluez の全ての method を呼べる**（Agent の登録を含む）。org.bluez（root）からの `org.bluez.Agent1` の呼び出しは root の policy の `allow send_interface="org.bluez.Agent1"` で利用者の connection に届く。design.md §8 の「非 root の人が Agent を登録できるか」は可（[F13] の答え）。polkit は BlueZ の method には使われない。
- 今の `dbus-linux.c`（2120 行）: `dbus_call` は同期（返事まで待つ、期限 `DBUS_TIMEOUT_MS`）で、error の返事を名前を捨てて `EACCES` にする。`dbus_dispatch_messages` は method call と signal を callback に渡し、返事・error は捨てる。`struct dbus_arg` は文字列・数・文字列の配列だけ。読みは `dbus_read_number`・`_string`・`_byte`・`_signature`・`_array`・`dbus_skip_value`。1 つの message は 64 KiB まで（`LINUX_DBUS_MAX`）。
- 口の約束（`keiland-backend.h`）: **何も待たない**（`kl_backend_bluetooth_update` は届いた物を読むだけ）、見ている間 2 秒・他は 30 秒ごとに state を読み直す、request は一度に 1 つ（`EBUSY`）。BlueZ の `Pair()` は数十秒かかりうるので、同期の `dbus_call` は使えない（compositor の main loop を止める）。

## 3. 構成

- `bluetooth-linux.c` が持つ物: system bus の自分の connection（他の backend の connection と分ける: Agent を export し、signal の match を足すため）、選んだ adapter の path と state、device の表（`KL_BACKEND_BT_DEVICES_MAX` 32）、送った call の serial と種類の表（8 個）、出した question、request の id の番号。
- 1 つの adapter: `org.bluez` の object の中で最初の `org.bluez.Adapter1`（path の文字の順、普通は `/org/bluez/hci0`）。adapter が消えたら次を探す。
- compositor の tick ごとに `kl_backend_bluetooth_update` → `dbus_dispatch_messages`（signal・Agent の call・返事）→ 期限の来た再読み → 変わった bit。fd は poll に足す（他の Linux の backend と同じ形、`kl_backend_bluetooth` の fd を返す口が要るなら p006 の口に合わせる: 今の zedBSD の backend の fd の扱いを実装の時に写す）。

### 3.1 state の写し

| 口の項目 | BlueZ |
| --- | --- |
| `reachable` | `org.bluez` に owner がいる（`org.freedesktop.DBus.GetNameOwner`、`NameOwnerChanged` で追う） |
| `state` | owner 無し → `ABSENT`。adapter 無し → `NONE`。`Adapter1.PowerState`（5.66 以降）が `off-blocked`（rfkill）→ `OFF`、`off`・`off-enabling` → `OFF`、`on-disabling` → `ON`、`on` → `ON`。PowerState が無い版は `Powered` で ON・OFF |
| `scanning` | `Adapter1.Discovering` |
| `pairing` | この backend の Pair の call が返事を待つ間 |
| `features` | `CAN_POWER \| CAN_CONNECT` |
| `power` | `Powered`（利用者の switch。rfkill の block の時も Powered は false） |
| `agent` | 自分の Agent の `RegisterAgent` が成功し、`RequestDefaultAgent` も成功したら 1（GNOME など別の default agent がいれば `RequestDefaultAgent` が後勝ちになる、§5.4） |
| `address`・`name` | `Adapter1.Address`・`Alias` |

### 3.2 device の写し

| 口の項目 | `org.bluez.Device1`（と `org.bluez.Battery1`） |
| --- | --- |
| `address`・`type` | `Address`・`AddressType`（`public` → BR/EDR か LE public: `Device1` は BR/EDR と LE を同じ object にまとめるので、`AddressType` が `random` なら `LE_RANDOM`、`public` で `Bearer` 情報が無ければ BR/EDR とみなす。p006 の zedBSD の扱いと同じ見え方にする） |
| `name` | `Alias`（無ければ `Name`、それも無ければ address） |
| `kind` | `Icon`: `input-keyboard` → KEYBOARD、`input-mouse`・`input-tablet` → MOUSE、`audio-*` → AUDIO、`phone` → PHONE、`computer` → COMPUTER、他 OTHER |
| `paired`・`legacy`・`connected` | `Paired`（か `Bonded`）・`LegacyPairing`・`Connected` |
| `battery` | `Battery1.Percentage`（無ければ -1） |
| `rssi` | `RSSI`（int16、無ければ 0） |

並びは paired が先、同じ中は名前の順（zedBSD の backend と同じ）。32 を越える発見の device は RSSI の強い順に残す。

### 3.3 読み方（大きな message を避ける）

- `GetManagedObjects` は使わない: 発見の多い所では device ごとの UUID の一覧で 64 KiB を越える。代わりに、
  1. 起動と adapter の変化の時: `org.freedesktop.DBus.Introspectable.Introspect` を adapter の path に（子の `<node name="dev_…"/>` だけを拾う。1 つ 40 byte ほどで 100 個でも数 KB）、
  2. adapter と各 device に `org.freedesktop.DBus.Properties.GetAll`（1 つ数 KB）を待たない call で順に送る（同時に 4 つまで）、
  3. 以後は signal で追う: `org.freedesktop.DBus.ObjectManager.InterfacesAdded`・`InterfacesRemoved`（sender `org.bluez`、path `/`）と `org.freedesktop.DBus.Properties.PropertiesChanged`（sender `org.bluez`、path_namespace `/org/bluez`）の match を足す。PropertiesChanged の `a{sv}` を表に当てる。
- 周期の再読み（見ている間 2 秒・他 30 秒）は adapter の `GetAll` だけ（device は signal で十分。signal を取りこぼした印（`dbus_dispatch_messages` の EMSGSIZE など）があれば 1 から読み直す）。
- 64 KiB を越える message は、今の `dbus-linux.c` では `EOVERFLOW` で bus を閉じる（2026-10-13 の別の世代の調べ）。Bluetooth の connection が閉じたら `reachable` を一時 0 にし、次の tick で開き直して 1 から読み直す（他の backend の connection には響かない、§3 の専用の connection）。

## 4. `dbus-linux.c` の拡張

| 追加 | 内容 |
| --- | --- |
| `dbus_send_call(bus, destination, path, interface, member, signature, args, count, uint32_t *serial)` | method call を送るだけ（送りの期限は今の `bus_send` の期限、受けは待たない）。返事は `dbus_dispatch_messages` の callback に来る |
| `dbus_dispatch_messages` | 今は返事・error を捨てる → callback に渡す（既存の利用者（notify-linux.c）は type を見て method call と signal だけを扱っているか確かめ、返事を無視する形にする。既存の動作は変えない） |
| 遅らせた返事 | `dbus_dispatch_messages` は callback の後に message を解放するので、Agent の call（`RequestConfirmation` など）に答えを待って返すには、呼び手の `sender` と `serial` を写して持つ。`dbus_send_return_to(bus, sender, serial, …)`・`dbus_send_error_to(bus, sender, serial, name, text)` を足す（今の `dbus_send_return` はその薄い包みに） |
| `struct dbus_reply` の `error` | 既にある（error の名前）。待たない call の error の名前（`org.bluez.Error.AuthenticationFailed` ほか）を backend が §5.3 の errno に写す |
| `struct dbus_arg` の variant | `variant`（内の signature、例 `"b"`）を足し、`v` の時は内の 1 つの値（数か文字列）を組む。`Properties.Set`（`ssv`）と `SetDiscoveryFilter`（`a{sv}`、§5.2）に使う |
| `a{sv}` の書き | `SetDiscoveryFilter` の `{"Transport": <"auto">}` の 1 項目だけ（固定の bytes の組み立てで足りるなら `dbus_arg` の `dict` は足さない、実装で小さい方） |
| 読み | `dbus_read_variant(reply, const char **signature)`（`g` を読み、中の型を返す）と `dbus_read_int16`（`n`）。`a{sv}` は既存の `dbus_read_array` と 8 の揃えで回し、知らない型は `dbus_skip_value` |
| Introspect の XML | `bluetooth-linux.c` の中の小さな走査（`<node name="` の後の `dev_` で始まる名前だけ、長さと文字を検査） |

既存の `dbus_call`（同期）は今の利用者のために残す。

## 5. request・question

### 5.1 request

| 口 | BlueZ（全て `dbus_send_call`） | 終わり |
| --- | --- | --- |
| `POWER_ON`・`POWER_OFF` | `Properties.Set(org.bluez.Adapter1, Powered, <b>)` | 返事で result 0 |
| `PAIR` | 対の device の path（address から `dev_AA_BB_…`）に `Device1.Pair()`。成功したら `Properties.Set(Device1, Trusted, true)`（HID の再接続に要る、bluetoothctl の trust と同じ）を送り、result 0 | 長い（BlueZ の期限は約 60 秒）。この間 `pairing` 1 |
| `FORGET` | `Adapter1.RemoveDevice(o)` | |
| `CONNECT`・`DISCONNECT` | `Device1.Connect()`・`Disconnect()` | |
| `PAIR_PHONE` | `ENOTSUP`（Linux に電話の機能は無い） | |

- 一度に 1 つ（`EBUSY`）。device の path は表に無い address なら `EINVAL`。
- 期限: backend の側でも 90 秒で `ETIMEDOUT` の result を出し、遅れて来た返事は捨てる（`Pair` は `Device1.CancelPairing` も送る）。

### 5.2 scan

- `set_scanning(1)`: `Adapter1.SetDiscoveryFilter({"Transport": "auto"})` → `StartDiscovery`。`set_scanning(0)`・close: `StopDiscovery`。BlueZ は connection が切れると、その client の discovery を自分で止める。
- 電源が切れている時は送らない（`NotReady` を避ける）。

### 5.3 error の写し（result の errno と reason）

| BlueZ の error | errno |
| --- | --- |
| `org.bluez.Error.AuthenticationFailed`・`AuthenticationRejected`・`AuthenticationCanceled` | `EACCES` |
| `InProgress`・`AlreadyExists`・`Busy` | `EBUSY` |
| `AuthenticationTimeout`・（backend の 90 秒） | `ETIMEDOUT` |
| `ConnectionAttemptFailed`・`Failed`（接続の時） | `ECONNREFUSED` |
| `NotReady` | `ENETDOWN` |
| `DoesNotExist`・`InvalidArguments` | `EINVAL` |
| `org.freedesktop.DBus.Error.ServiceUnknown`・`NoReply` | `ENOTCONN`（`reachable` 0 に） |
| 他 | `EIO` |

reason は error の名前の最後の語（`AuthenticationFailed` など、`KL_BACKEND_BT_REASON_MAX` に切る）。

### 5.4 Agent（question）

- 起動時（owner が来た時も）: 自分の connection に `/org/keiland/bluetooth/agent` を export（`dbus_dispatch_messages` の callback で、path と interface `org.bluez.Agent1` の call を受ける）→ `AgentManager1.RegisterAgent(path, "DisplayYesNo")` → `RequestDefaultAgent(path)`。
  - capability は `DisplayYesNo`: 口の question は「数の一致を確かめる」「同意」「device で打つ数を見せる」だけで、**文字・数を打って答える**口が無い（p006 の口）。`KeyboardDisplay` を名乗ると BlueZ が `RequestPasskey` を送ってくるので名乗らない。
- Agent1 の method:

| method | 口 | 返事 |
| --- | --- | --- |
| `RequestConfirmation(o device, u passkey)` | `ASK_CONFIRM`（number = passkey） | 答えまで返事を保留（呼び手の sender と serial を表に写して持つ、§4）。yes → 空の return、no → `org.bluez.Error.Rejected` |
| `RequestAuthorization(o device)` | `ASK_CONSENT` | 同上 |
| `AuthorizeService(o device, s uuid)` | paired で trusted の device なら即 return。他は `ASK_CONSENT` | 同上 |
| `DisplayPasskey(o device, u passkey, q entered)` | `ASK_PASSKEY`（最初の 1 回だけ、entered の更新は無視） | 空の return（method は返事を求める） |
| `DisplayPinCode(o device, s pincode)` | 6 桁の数なら `ASK_PASSKEY`、他は question を出さず log | 空の return |
| `RequestPinCode(o)`・`RequestPasskey(o)` | 出さない（打つ口が無い） | `org.bluez.Error.Rejected`、result の reason に `input-unsupported`（legacy の PIN の device は Linux の Keiland の Settings では pairing できない。制限として記録） |
| `Cancel()` | 今の question の `ASK_END` | 空の return |
| `Release()` | `agent` を 0 に | 空の return |

- question の `user` は compositor の利用者の名前（`getpwuid(getuid())`）、`own` は自分の `Pair` の間なら 1（相手からの pairing は 0）。`answer`: 保留中の call に返事を送る（id が違えば `ENOENT`）。`cancel`: 自分の Pair の間なら `Device1.CancelPairing()`。
- 保留中の Agent の call は BlueZ の側の期限（約 25 秒）で `Cancel` が来る。close の時は保留中の call に `Rejected` を返し、`UnregisterAgent` を送る。

## 6. 試験

| 層 | 試験 |
| --- | --- |
| host | `dbus-linux.c` の新しい読み書きの純粋な部分: 記録した BlueZ の message の bytes（`PropertiesChanged` の `a{sv}`、`InterfacesAdded`、Introspect の XML、error の返事）を解いて表に当てる、64 KiB を越える・壊れた variant の拒否、`Properties.Set` の `ssv` の bytes を `dbus-send` の作る物と比べる。`bluetooth-linux.c` の写しの関数（Icon → kind、error → errno、PowerState → state、並び）を単独で |
| host（bus） | host の system bus（bluez 5.82 が動く。controller は無いので adapter 無し）に繋ぎ、`reachable` 1・`state` `NONE`、bluetoothd を止めた時（できるなら、host の service を止めるのはユーザーの許可が要るので、しないで `org.bluez` の名前の無い private の bus（`dbus-daemon --session` の試験の bus）で `ABSENT`） |
| QEMU（T1、WS131 の Debian の QEMU+KVM、SSH・QMP PNG の例外） | guest の中で `btvirt -l2`（bluez-test-tools、2 つの仮想 controller）か `hci_vhci` を使い、guest の bluetoothd の上で Keiland の Settings の Bluetooth の頁: 電源の on・off、scan で相手の仮想 controller が見える、pairing（相手は `bluetoothctl` の agent で数の一致に yes）で `ASK_CONFIRM` の窓、Forget、Connect・Disconnect、system bar の表示。PNG |
| FreeBSD（T1、WS109） | Settings の Bluetooth の頁が未対応の文（`ABSENT`）を出す PNG |

## 7. 判断の点（design-reviewer と Q1 へ）

| ID | 点 | 案 |
| --- | --- | --- |
| L1 | Agent の capability | `DisplayYesNo`（打つ口が無い）。legacy の PIN・passkey の入力の device は pairing できない制限を受け入れる（口を足すのは新しい機能になる） |
| L2 | 既に default agent がいる（GNOME などと共に動く）時 | `RequestDefaultAgent` を送って後勝ち。Keiland の session の中では Keiland が出すのが自然。`agent` は自分が default かどうかではなく登録できたか（BlueZ は default の変化を知らせないので、確かめられない） |
| L3 | 読み方 | `GetManagedObjects` を使わず Introspect ＋ `GetAll`（64 KiB の上限のため）。代わりに上限を上げる案は buffer が message ごとに固定の大きさなので採らない |
| L4 | 待たない call の足し方 | `dbus-linux.c` に `dbus_send_call` を足し、`dbus_dispatch_messages` が返事も渡す。既存の利用者の動作は変えない |
| L5 | 自分の connection | Bluetooth は専用の system bus の connection（Agent の export と match のため）。fd が 1 つ増える |

## 8. 見積もり

`bluetooth-linux.c` 約 1200 行、`dbus-linux.c` の拡張 約 250 行、host 試験。約 3〜4 LW（design.md の見積もりの BlueZ の部分）。
