<!-- awesome-plan project=zedbsd record=ws143-p007 -->

# ws143-p007: Linux の Bluetooth の backend（BlueZ の D-Bus）と FreeBSD の未対応の表示

Parent: [WS143](../ws.md)
Status: cleared（2026-10-14 Q1: T1-593・T1-604・T1-605（Debian 13 の QEMU+KVM、btvirt）で ON・adapter の名・検出・番号の照合・pair・Forget・電源・Connect の失敗の文・古い bond での拒否・Cancel（約 1 秒で閉じ「The pairing was refused.」）・失敗の後の窓の閉じが PASS。制限: 仮想の相手への Connect が約 20 秒の Timeout で終わる間は Remove が効かない（1 度に 1 つの request の作り、記録だけ）、FreeBSD の頁は未実施（T1-587）、実機は未実施）
Phase disposition: normal
Queue: q962（P2、2026-10-13）。承認: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」、WS143 の design.md §9（D7・D13 ほか、2026-10-05 夕 ユーザー「全部推奨」）と §10 の p007、2026-10-08 ユーザー「Linux・FreeBSD は 10/13 以降」。
Queue（実装）: q967（P3、2026-10-14）。review: [review-1](review-1.md)（design-reviewer、判定「直せば実装してよい」、重大 4 と中 M1・M3・M5・M7・M8 を入れれば再 review なし、Q1）。
依存: p006（desktop の backend の口 `kl_backend_bluetooth_*`、compositor の `bluetooth-shell.c`・`bluetooth-ask.c`・`bluetooth-bar.c`、Settings の頁。p006 は 2026-10-08 に cleared）。

**§3〜§6 の第 1 版の文と §9（第 2 版）が違う所は §9 が優先する。**

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

## 9. 第 2 版（review-1 の後、2026-10-14 P3 q967）

review-1 の重大 R1〜R4、中 M1〜M13、軽微 m1〜m9 を入れた。Q1 の決め（2026-10-14）: R1〜R4・M1〜M9・M11・軽微は review の案どおり。ユーザーの決定（2026-10-14、Q1 経由）: L1・M10・M12・M13（下）。

### 9.1 不変条件と接続の状態機械（R1・R2・L4・L5）

- **同期の呼び出しは開く時だけ**: Bluetooth の connection で待つのは `dbus_open_address()` の connect・EXTERNAL の認証・`Hello` だけ（最初の待たない call と Agent の export より前）。他は全部 `dbus_send_call()`（AddMatch・GetNameOwner・RegisterAgent・RequestDefaultAgent・UnregisterAgent・Introspect・GetAll・Set・Pair ほか）。`dbus_call()` と `dbus_add_match()`（同期）は使わない。AddMatch も待たない call で送る: bus は 1 つの connection の message を順に処理するので、後の GetNameOwner より前に match が効く（R3）。
- 状態: **閉じている**（fd < 0）→ 期限（backoff）が来たら開く。開くのに失敗したら、または開いた後に閉じた（受け取りの誤り・64 KiB 超・送りの失敗）ら、次の試みまで `BT_RETRY_MS` 1 秒から倍ずつ 30 秒まで延ばす。開いてから 60 秒続いたら backoff を 1 秒に戻す（64 KiB 超の返事が繰り返しても 30 秒に 1 回までしか開き直さない）。
- 接続を失った時、および `NameOwnerChanged` で org.bluez の owner が変わった・居なくなった時: 出ている request に result `ENOTCONN`（reason `lost`）、出ている question に `ASK_END`、保留の Agent の call は捨てる（返事は送れない、または新しい owner には無意味）、送った call の表・adapter・device の表を空に、`agent` 0、`discovery_sent` 0。owner が居ない間は `reachable` 0・state `ABSENT`。
- 開いた直後: AddMatch 4 つ（`type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.bluez'`、`type='signal',sender='org.bluez',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'`、同 `InterfacesRemoved`、`type='signal',sender='org.bluez',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path_namespace='/org/bluez'`）→ `GetNameOwner("org.bluez")`。
- owner が来た時（GetNameOwner の答え、NameOwnerChanged の new）: `RegisterAgent(path, "DisplayYesNo")` → 成功（か `AlreadyExists`）で `RequestDefaultAgent(path)` → 成功で `agent` 1。失敗は log し 30 秒後にやり直す。並んで adapter を探す（§9.2）。scan を求めている間は adapter が ON になったら `StartDiscovery` を送り直す（BlueZ は client の切断・電源の入り直しで discovery を止める）。
- **M1 auto-start**: org.bluez への call は全部 header の flags に `NO_AUTO_START`（0x2）。owner が居ない間は org.bluez に何も送らない。
- 接続は専用の 1 本（L5）。compositor の poll には足さない（m1: tick ごとに `kl_backend_bluetooth_update` が読む、口は変えない）。

### 9.2 読み方（R3・M3・M4・M5・m8）

- adapter を探す: `Introspect(/org/bluez)` の子の node（`hciN`）の名前の順で最初 → その path に `GetAll(org.bluez.Adapter1)` と `Introspect(path)`（子の `dev_…` の node）。Introspect の XML は interface の記述も含めて数 KB〜十数 KB で 64 KiB に収まる見込み（超えれば接続を閉じて backoff）。
- device ごとに `GetAll(org.bluez.Device1)` と `GetAll(org.bluez.Battery1)`（Battery1 が無ければ error、無視して battery -1）。送り中の call は表（`BT_CALLS_MAX` 16）に持ち、読みは同時に 4 つまで。表が満ちたら送りを次の tick に延ばす（device の「読みが要る」印で持つ）。
- signal: `InterfacesAdded(o, a{sa{sv}})` と `GetAll` の答えは同じ upsert。`InterfacesRemoved(o, as)` は interface ごと: Device1 → device を消す、Battery1 → battery -1、Adapter1（選んだ adapter）→ 表を空にして adapter を探し直す。`GetAll` を待つ間に device が消えたら答えは捨てる（表の項目は path で引く）。`GetAll` が `UnknownObject`・`UnknownMethod`・`InvalidArgs`（Device1 の時）なら device を消す。
- `PropertiesChanged(s, a{sv}, as)`: 変わった物を当て、invalidated の `RSSI` は 0 に（M4）。表に無い device の path（選んだ adapter の下）から来たら `GetAll` を送る（M5）。
- variant の読みは `y`・`b`・`n`・`q`・`u`・`i`・`s`・`o`（他は `dbus_skip_value`）。`Percentage` は `y`、`RSSI` は `n`（M3）。
- 内部の表は `BT_TABLE_MAX` 128。満ちた時の新しい device は、paired でない device のうち RSSI の一番弱い物と入れ替える（paired はいつも残す）。口に出すのは 32 まで: paired を先に、残りは RSSI の強い順で選び、出す並びは paired を先に名前の順（M5）。
- 周期の再読みは adapter の `GetAll` だけ（見ている間 2 秒、他 30 秒、result の後すぐ）。
- **M6**: RSSI だけの変化は `CHANGED_DEVICES` を 2 秒に 1 回までにまとめる（口に出す一覧を前回と比べ、RSSI 以外が違えばすぐ、RSSI だけなら 2 秒ごと）。`SetDiscoveryFilter` は送らない（既定と同じ、RSSI の閾値の既定を保つ）。§4 の `a{sv}` の書きは要らない。

### 9.3 写し（M2・m4・m5）

- state: owner 無し → `ABSENT`、adapter 無し → `NONE`。`PowerState` が在れば（experimental、Debian の既定では無い見込み）`on`・`on-disabling` → `ON`、`off`・`off-blocked` → `OFF`、`off-enabling` → `STARTING`。無ければ `Powered` で `ON`・`OFF`（普通の経路）。rfkill の block は区別しない。
- device の type: `AddressType` が `random` → `LE_RANDOM`。`public` は `Class` が在れば `BREDR`、`Appearance` だけなら `LE_PUBLIC`、どちらも無ければ `BREDR`（m4）。
- 名前: `Name` が在れば `Alias`（利用者の付けた名、無ければ Name と同じ）、無ければ address。63 byte に UTF-8 の文字の境界で切る（m5）。

### 9.4 request（M7・M8・M12・M13・m3）

- device の path は表に持った物を使う（address から作らない、m3）。表に無い address は `EINVAL`。adapter が無い時の POWER は `EINVAL`。
- **PAIR（ユーザーの決定 M12・M13、zedBSD と同じ）**: `Device1.Pair()` → 成功（か `AlreadyExists`）で `Properties.Set(Device1, Trusted, true)` → `Device1.Connect()`。result は Connect の答えで出す: Pair が成功していれば error 0（Connect の失敗は log と reason に error の最後の語、Trusted の失敗は log だけ）。Pair の失敗はその errno。`pairing` は Pair〜Connect の間 1。
- 期限（M7）: 送った call ごとに期限（内部の読み・Set・Agent の登録 25 秒、Pair 90 秒、Connect 25 秒）。期限が来たら ETIMEDOUT として扱い（Pair なら `CancelPairing` も送る）、遅れて来た答えは表に無いので捨てる。
- `cancel`: 自分の Pair（〜Connect）の間なら `CancelPairing` を送り、result `ECANCELED`（reason `cancelled`）をすぐ出す。
- error の写し（M8、§5.3 を置き換え）:

| error | errno |
| --- | --- |
| Connect の `org.bluez.Error.AlreadyConnected`、Disconnect の `NotConnected`、Pair の `AlreadyExists`（続けて Trusted・Connect） | 0 |
| `AuthenticationFailed`・`AuthenticationRejected`・`AuthenticationCanceled`・`Rejected`・`Canceled`・`NotAuthorized`、`org.freedesktop.DBus.Error.AccessDenied` | `EACCES` |
| `InProgress`・`AlreadyExists`（Pair 以外）・`Busy` | `EBUSY` |
| `AuthenticationTimeout`、`org.freedesktop.DBus.Error.NoReply`・`Timeout`、backend の期限 | `ETIMEDOUT` |
| `ConnectionAttemptFailed`、`Failed`（Connect・Pair の時）、`NotAvailable` | `ECONNREFUSED` |
| `NotReady` | `ENETDOWN` |
| `DoesNotExist`・`InvalidArguments`、`org.freedesktop.DBus.Error.UnknownObject`・`UnknownMethod`・`InvalidArgs` | `EINVAL` |
| `NotSupported` | `ENOTSUP` |
| `org.freedesktop.DBus.Error.ServiceUnknown`・`NameHasNoOwner` | `ENOTCONN` |
| `LimitsExceeded`・他 | `EIO` |

  `reachable` は owner の追跡だけで決め、error では変えない。reason は error の名前の最後の語、`Failed` の時は error の本文（`br-connection-page-timeout` など）を 31 byte に切って使う。自分の Agent が `RequestPinCode`・`RequestPasskey` を断った後の Pair の失敗は reason を `input-unsupported` に（m7）。

### 9.5 Agent（L1・L2・M9・M10・M11・m6・m9）

- 受ける call の検査（M9）: sender が今の org.bluez の owner（unique name）、path が `/org/keiland/bluetooth/agent`、interface が `org.bluez.Agent1`。sender が違えば `org.freedesktop.DBus.Error.AccessDenied`、知らない method・path は `org.freedesktop.DBus.Error.UnknownMethod`。答えを待つ call（`RequestConfirmation`・`RequestAuthorization`・HID の `AuthorizeService`）は 1 つだけ保留し、保留中に 2 つ目が来たら `org.bluez.Error.Rejected`。保留は 30 秒で `Rejected` を返して捨てる（BlueZ の期限の約 25 秒の後の保険）。
- **AuthorizeService（ユーザーの決定 M10）**: device が表に無いか paired でない → `Rejected`。paired で trusted → すぐ return。paired で untrusted → uuid が HID（`00001124-0000-1000-8000-00805f9b34fb`）か HOGP（`00001812-0000-1000-8000-00805f9b34fb`）なら `ASK_CONSENT` で尋ね、他は `Rejected`。
- **M11**: `ASK_PASSKEY` は `Cancel` の他に、自分の Pair の答えが来た時と、その device の `Paired` が true になった時にも `ASK_END`。
- m6: question の `own` は「device の path が自分の Pair の device の path と同じ」。`user` は `getpwuid(getuid())`。
- L2: `RequestDefaultAgent` は後勝ち。利用者の切り替え・別の VT の session では後に登録した方が勝つ（session が active になった時の送り直しはしない、制限として下に記録）。
- close（m9）: 保留の Agent の call に `Rejected`、自分の Pair なら `CancelPairing`、scan を送っていれば `StopDiscovery`、`UnregisterAgent` を送ってから閉じる（どれも待たない。送りの失敗は無視）。

### 9.6 `dbus-linux.c` の追加（§4 を置き換え、L4）

| 追加 | 内容 |
| --- | --- |
| `dbus_open_address(bus, path)` | unix の path の bus に繋ぐ（認証 EXTERNAL、fd の受け渡しは無し、Hello）。`bluetooth-linux.c` は `BT_DBUS_SOCKET`（既定 `/run/dbus/system_bus_socket`、host の試験は build 時の define で私的な bus）を渡す（R4 (b)） |
| `dbus_send_call(bus, flags, destination, path, interface, member, signature, args, count, &serial)` | 待たない method call（flags に `LINUX_DBUS_NO_AUTO_START`） |
| `dbus_dispatch_messages` | 返事・error も callback に渡す（notify-linux.c の callback は METHOD_CALL 以外を見ないので動作は変わらない、review L4） |
| `dbus_send_return_to`・`dbus_send_error_to` | 呼び手の sender と serial を写して後で答える。`dbus_send_return`・`dbus_send_error` はその包み |
| `struct dbus_arg` の `variant` | `v` の時の中の signature（`b`・`s`・`u`）。`Properties.Set`（`ssv`）に使う |
| `dbus_read_variant(reply, &signature)`・`dbus_read_int16(reply, &value)` | variant の中の型（1 つの完全な型）と `n`・`q` の読み |

### 9.7 試験（R4）

| 層 | 試験 |
| --- | --- |
| host（偽の org.bluez） | `plan/ws143/tests/bt-linux-host-test.sh`: 私的な `dbus-daemon`（試験の directory の socket）の上に python3 の dbus-python と GLib で偽の org.bluez（`Adapter1`・`Device1`・`Battery1`・`AgentManager1`、ObjectManager の signal、error を返す台本）を立て、`bluetooth-linux.c`・`dbus-linux.c` を `-DBT_DBUS_SOCKET` で build した C の試験（plain と ASan+UBSan）が口を叩く。確かめ: ABSENT → owner が来て ON・device の一覧（paired が先）、Battery1、RSSI だけの変化のまとめ、PropertiesChanged の invalidated、表に無い device の signal から GetAll、Pair → Agent の RequestConfirmation → answer yes → Trusted・Connect → result 0、Pair の error の写し、AuthorizeService の 3 つの場合、RequestPinCode の拒否と reason、Cancel・Release、owner の入れ替わり（偽の org.bluez を止めて別の名で立て直す）で request が ENOTCONN・question が ASK_END、cancel、POWER の Set、FORGET、Connect の AlreadyConnected が 0、sender の違う Agent の call が AccessDenied、32 を越える device と paired の保持 |
| build | `make keiland-linux`（または target を名指す libkeiland-backend.a と compositor）の warning 0、`plan/tools/keiland-os-boundary/check.sh`（R4 (e)） |
| QEMU（T1、WS131 の Debian の QEMU+KVM、SSH・QMP PNG の例外） | (1) guest で `busctl introspect org.bluez /org/bluez/hci0` で PowerState の有無、`btvirt` が bluez-test-tools に在るか（無ければ `hci_vhci` の module と `btmgmt` の代わり）を確かめる。(2) Keiland を root でない利用者で起動（[F13] の確かめ）。(3) Settings の Bluetooth の頁で電源の on・off、scan で相手の仮想 controller が見える、Pair（相手側の `bluetoothctl` は `agent on` と `default-agent` を Keiland の起動の後に。1 つの bluetoothd が hci0 と hci1 を持つと相手側の pairing も default agent に来るため）で数の一致の窓（仮想 controller が SSP の numeric comparison をしなければ Just Works で窓は出ない: その時は窓無しで pairing が済むことを記録）、Forget、Connect・Disconnect、system bar。PNG |
| FreeBSD（T1、WS109） | Settings の Bluetooth の頁が未対応（ABSENT）の文を出す PNG（source は変えない） |

### 9.8 既知の制限（ユーザーの決定 L1、2026-10-14「制限を受け入れる」）

- Linux の Keiland の Agent は `DisplayYesNo` で、文字・数を打って答える口が無い。pairing できないのは BlueZ が `RequestPinCode`（PIN を打つ必要のある、SSP の無い古い device のうち、キーボードでない物）か `RequestPasskey`（相手が表示だけの device で、こちらが数を打つ場合）を送ってくる device だけ。SSP の無い古いキーボードは BlueZ が `DisplayPinCode` で 6 桁の PIN を示すので `ASK_PASSKEY`（device で打つ数）で pairing できる。BlueZ が一般的な PIN を自分で試すかは未確認（QEMU で見る）。断った時の result の reason は `input-unsupported`。
- `RequestDefaultAgent` は後勝ち（L2）。同じ機械で別の desktop の session（別の VT・利用者の切り替え）が後に Agent を登録すると、相手からの pairing の question はそちらへ行く。自分の Pair の question は自分の connection の Agent に来る。

## 10. 実装の記録（2026-10-14 P3 q967）

### 変えた物

| file | 内容 |
| --- | --- |
| `userland/desktop/libkeiland-backend-linux/bluetooth-linux.c`（新） | §9 の backend。専用の connection（開く時だけ同期）、送った call の表（16、期限つき）、adapter と device の表（128、出すのは 32）、Agent（DisplayYesNo）、request（Pair → Trusted → Connect）、RSSI だけの変化の 2 秒のまとめ、接続を失った時の backoff（1 秒 → 30 秒、60 秒続けば戻す） |
| `userland/desktop/libkeiland-backend-linux/dbus-linux.c`・`.h` | `dbus_open_address`、`dbus_send_call`（flags、`LINUX_DBUS_NO_AUTO_START`）、`dbus_send_return_to`・`dbus_send_error_to`（`dbus_send_return`・`dbus_send_error` はその包み）、`dbus_arg.variant` と `writer_variant`、`dbus_read_variant`・`dbus_read_int16`、`dbus_dispatch_messages` が返事と error も callback に渡す |
| `userland/desktop/libkeiland-backend-linux/Makefile.linux` | Linux の build は `bluetooth-unsupported.c` の代わりに `bluetooth-linux.c` |
| `userland/desktop/libkeiland-backend/unsupported/bluetooth-unsupported.c`・`keiland-backend.h` | comment だけ（unsupported は FreeBSD の物、Linux は BlueZ） |
| `plan/ws143/tests/bt-linux-host-test.sh`・`.c`・`bt-linux-fake-bluez.py`（新） | §9.7 の host 試験 |

FreeBSD の source（`libkeiland-backend-freebsd/Makefile.freebsd` は `bluetooth-unsupported.c` のまま）は変えていない。

### 確かめ（host、2026-10-14）

- `sh plan/ws143/tests/bt-linux-host-test.sh`: **PASS**（plain 54 checks ok、ASan+UBSan 54 checks ok。出力 `build/ws143-bt-linux.run.w8rNrK`）。確かめた物: 起動で ON・adapter の名・Agent の default・device 3 つ（paired が先、Battery1 の 80、LE random、BR/EDR）、RegisterAgent の capability `DisplayYesNo`、InterfacesAdded、RSSI の変化と invalidated、表に無い device の PropertiesChanged から GetAll、Pair の数の一致（question CONFIRM 123456・own 1・pairing 1）→ yes → Paired・Trusted・Connected、no → EACCES、`Failed` の本文が reason（ECONNREFUSED `br-connection-page-timeout`）、RequestPinCode の拒否（Rejected、result EACCES `input-unsupported`）、DisplayPasskey（PASSKEY 654321、Pair の後に ASK_END）、cancel（ECANCELED、CancelPairing が送られた）、BlueZ の Cancel（ASK_END、EACCES）、AuthorizeService の 4 つ（未 paired → Rejected、paired untrusted の HID → CONSENT → yes → ok、HID でない → Rejected、trusted → ok）、RequestPasskey の拒否、owner でない caller の Agent の call → AccessDenied、POWER off・on、Connect の AlreadyConnected → 0、Disconnect、NotConnected → 0、表に無い address → EINVAL、PAIR_PHONE → ENOTSUP、FORGET（device が消える）、scan の StartDiscovery・StopDiscovery、device 45 で 32 を出し paired を残し RSSI の弱い物を落とす、Pair の途中で org.bluez が消える（ENOTCONN・ABSENT・device 0・request は ENOTCONN）→ 戻る（Agent と device を読み直す）、64 KiB を越える signal で接続が閉じ、backoff の後に開き直して読み直す、Release で agent 0、close で UnregisterAgent。
- `sh plan/ws156/tests/run-host-notify-dbus.sh build/tmp/p3-notify-dbus`（dbus-linux.c の dispatch の変更の回帰）: PASS（plain・sanitized 41 checks ok）。
- `make -f userland/desktop/keiland-linux.mk -j8 KEILAND_LINUX_BUILD=build/p3-keiland-linux build/p3-keiland-linux/bin/wayland`: 成功、warning 0（compositor が新しい backend と link）。
- `sh plan/tools/keiland-os-boundary/check.sh`: PASS。
- `git diff --check`: 問題なし。
- zedBSD の amd64 の build: 実施していない。zedBSD が build する file で変わったのは `keiland-backend.h` の comment だけ（`bluetooth-unsupported.c` は host の cc の `-fsyntax-only` で確かめた）。

### 未実施（T1 に依頼）

- QEMU（Debian 13 の QEMU+KVM、WS131 の環境、SSH・QMP PNG の例外）: §9.7 の (1)〜(3)。
- FreeBSD（WS109 の guest）: Settings の Bluetooth の頁の未対応の表示の PNG。

### 残り・制限

- 試験の C（`bt-linux-host-test.c`）は確かめの式を `check()` の引数に書いており、coding-style の「Boolean を式で作らない」に合わない所がある（p009 の規約の全文の見直しで扱う）。
- GetNameOwner が期限切れ（bus daemon が応答しない）の時は、NameOwnerChanged が来るまで owner を知らないまま（再試行しない）。
- §9.8 の既知の制限（L1・L2）。

## 11. T1-593 の直し（2026-10-14 P3、Connect の失敗の文）

### 原因

- 頁が見る errno は backend の errno ではなく、compositor の result を経た物: backend の errno →（`wayland/bluetooth-shell.c` の `bt_applied()`）→ `KL_SYSTEM_RESULT_*` →（`libkeiland/system/system-view.c` の `system_view_error_of()`）→ 頁の errno。写しは ECONNREFUSED → REFUSED → **EACCES**、EACCES → DENIED → EPERM、ETIMEDOUT → UNREACHABLE → ENETUNREACH、ENOTSUP → UNSUPPORTED → ENOTSUP、EIO → FAILED → EIO。
- 頁（`settings/page-bluetooth.c` の `se_bluetooth_result()`）の失敗の文は errno だけで引き、request の種類（pair・connect）を見ない。頁の EACCES は「The pairing was refused.」。
- §9.4 の写しで BlueZ の `NotAvailable` は ECONNREFUSED。profile の無い相手への `Device1.Connect` は BlueZ が `org.bluez.Error.NotAvailable`（`br-connection-profile-unavailable`）を返すので、ECONNREFUSED → 頁の EACCES →「The pairing was refused.」（bt6）。
- 確かめで分かった同じ系統の食い違い: §9.4 は `AuthenticationFailed`・`AuthenticationRejected`・`AuthenticationCanceled`・`Rejected`・`Canceled` を EACCES にしていたが、zedBSD の backend（`bluetooth-zedbsd.c` の `bt_error()`）では EACCES は bluetoothd の `permission`（D8 の許されない利用者）で、pairing の拒否（`rejected`）は ECONNREFUSED。このため Linux の pairing の拒否は頁で「This account may not change Bluetooth.」になる（T1-593 の 2 回目の pair の request 4・5、`error=13 AuthenticationCanceled`。PNG は撮られていない）。`tests/scenarios/apps/settings/bluetooth-pair.md` の「no で `kind=pair errno=<EACCES>`（Linux は 13）と The pairing was refused.」にも合わない。

### 直し（commit eb79dde1c）

| file | 内容 |
| --- | --- |
| `libkeiland-backend-linux/bluetooth-linux.c` の `bt_errno()` | Connect（request の CONNECT、Pair の後の Connect の段）の `NotAvailable` → **ENOTSUP**（この computer が繋ぐ profile が無い）。`Failed`・`ConnectionAttemptFailed` で本文に `timeout`（大小を問わず、`br-connection-page-timeout`・`Page Timeout`）→ **ETIMEDOUT**（相手が応じない）。`AuthenticationFailed`・`AuthenticationRejected`・`AuthenticationCanceled`・`Rejected`・`Canceled` → **ECONNREFUSED**（zedBSD の `rejected` と同じ）。`NotAuthorized`・D-Bus の `AccessDenied` は EACCES のまま（許されない呼び手）。自分の Agent が PIN・passkey の入力を断った後の Pair の失敗（reason `input-unsupported`）は **ENOTSUP**（頁は「Bluetooth cannot do that here yet.」、§9.8 L1 の制限） |
| `settings/page-bluetooth.c` の `se_bluetooth_result()` | `doing` が `connect` の時の文の表を足した（頁の errno で）: EACCES「The device refused the connection.」、ENOTSUP「This computer cannot connect to this kind of device.」、ENETUNREACH「The device did not answer. Check that it is on and nearby.」、EIO「The device could not be connected.」。他（EBUSY・EPERM・ENODEV・EINVAL）は今までの共通の文。pair の文は変えない |
| `libkeiland-backend/keiland-backend.h` | `kl_backend_bluetooth_take_result` の comment に errno の意味（EACCES は許されない呼び手、ECONNREFUSED は相手か利用者の拒否、ETIMEDOUT は相手が応じない、ENOTSUP はこの computer ができない）と ENOTSUP を足した（comment だけ） |

§9.4 の error の写しの表は、上の行で置き換わる（Connect の NotAvailable・timeout の本文・Authentication 系・input-unsupported）。

zedBSD への影響（頁は共通）: zedBSD の Connect の失敗は bluetoothd の HID の理由（`no-hid`・`security`・`protocol` など）が `bt_error()` で EIO、`timeout` が ETIMEDOUT。今までの頁の文は「It did not work.」・「The device did not answer.」で、直しの後は「The device could not be connected.」・「The device did not answer. Check that it is on and nearby.」。zedBSD の backend の写しは変えていない（`no-hid` を ENOTSUP にすれば Linux の NotAvailable と同じ文になる。p007 の範囲の外なので Q1 の判断に残す）。

### Forget と 2 回目の pair（記録だけ）

- Forget は `Adapter1.RemoveDevice(o)` を呼ぶ（`bluetooth-linux.c` の `bt_request_send` の FORGET、host 試験の偽の org.bluez も `RemoveDevice` の call を記録）。T1-593 でも `request 3 kind=4 error=0`。
- Forget の後の 2 回目の pair の `AuthenticationCanceled` は、相手（同じ bluetoothd の hci1）が古い bond を持ったままのため。相手側で device を remove すると窓が出て paired（request 6 error=0）。こちらの側の不具合ではなく、仮想の相手の性質と推測（実の device では相手の側の bond の消し方の問題）。直さない。直しの後は、この場合の頁の文は「The pairing was refused.」（ECONNREFUSED → 頁の EACCES）になる。

### 確かめ（host、2026-10-14 P3）

- `sh plan/ws143/tests/bt-linux-host-test.sh build/p3-bt-linux`: **PASS**（plain 58 checks ok、ASan+UBSan 58 checks ok。出力 `build/p3-bt-linux.run.VMPvLH`）。変えた・足した確かめ: pair-no（answer no → ECONNREFUSED）、pair-failed-text（`Failed` `br-connection-page-timeout` → ETIMEDOUT、reason は本文）、pin-refused（ENOTSUP、`input-unsupported`）、bluez-cancel（ECONNREFUSED）、新しく connect-no-profile（`NotAvailable` → ENOTSUP、reason `NotAvailable`）、connect-no-answer（`Failed` `br-connection-page-timeout` → ETIMEDOUT）、connect-refused（`Failed` `br-connection-refused` → ECONNREFUSED）、connect-not-supported（`NotSupported` → ENOTSUP）。偽の org.bluez の `connect-error` は `名前:本文` を受けるようにした。
- `sh plan/ws143/tests/bt-page-result-host-test.sh build/p3-keiland-linux`（新）: **PASS**。Linux の Settings の object（main.o を除く）と試験の main を link し、`se_bluetooth_result()` に頁の errno を渡して文を確かめる: connect の ENOTSUP・EACCES・ENETUNREACH・EIO・EPERM・0、pair の EACCES「The pairing was refused.」・ENOTSUP・ENETUNREACH・0（10 件 ok）。
- `make -f userland/desktop/keiland-linux.mk -j8 KEILAND_LINUX_BUILD=build/p3-keiland-linux build/p3-keiland-linux/bin/settings build/p3-keiland-linux/bin/wayland`: 成功、warning 0（log `build/p3-p007-build5.log`）。
- `sh plan/tools/keiland-os-boundary/check.sh`: FAIL だが、この変更の無い tree（git stash で戻した）でも同じ FAIL（C4 `userland/desktop/libpdf/font.c:57`・`replace.c:39` の `/usr/share/fonts`、libpdf の別の作業）。この変更で増えた FAIL は無い。
- `git diff --check`: 問題なし。
- zedBSD の amd64 の build: 実施していない（zedBSD が build する file で変わったのは `page-bluetooth.c` と `keiland-backend.h` の comment。`page-bluetooth.c` は Linux の build で warning 0）。

### T1 への再試験の依頼（Q1 が T1 に送る文）

> T1-593 の再試験（ws143-p007 の Connect の失敗の文、P3 eb79dde1c）。WS131 の Debian 13 の QEMU+KVM の guest（SSH・QMP PNG の例外）に main の tree の `make keiland-linux` を install、bluez と bluez-test-tools、`modprobe hci_vhci; btvirt -l2 &`。Keiland を root でない利用者（kei、systemd-run の PAMName=login・tty1、T1-593 と同じ）で起動。相手側（hci1）は bluetoothctl で select・power on・discoverable on・pairable on・agent on・default-agent（Keiland の後）。(1) Settings の Bluetooth で相手を Pair（数の一致の窓で Pair）→ paired。(2) My Devices の相手で Disconnect、続けて Connect（profile の無い仮想の相手なので BlueZ は NotAvailable）: 頁の文が「This computer cannot connect to this kind of device.」で、「The pairing was refused.」ではないこと（PNG）。stderr に `request N kind=5 error=95 reason=NotAvailable`。(3) Forget の後に相手側で `remove 00:AA:01:00:00:00`（hci0 の address）をせずに Pair → 失敗の文が「The pairing was refused.」（「This account may not change Bluetooth.」ではない）で stderr に `kind=3 error=111 reason=AuthenticationCanceled`（PNG）。その後に相手側で remove して Pair が通ること。(4) Pair の窓で Cancel（または No）→ 文が「The pairing was refused.」（PNG）。合否は PNG の文と stderr の行。FreeBSD は T1-587 と同じく GPU の無い guest で compositor が起きないので省く。

## 12. T1-604 の観察の直し（2026-10-14 P3）

T1-604（文は PASS）の観察 3 つ。証拠は T1 の `build/review/t1-604/`（PNG と `keiland-stderr-full.txt`、読み取りで）。

### 12.1 Pair の窓の Cancel が Keiland の側で終わらない（直した）

- 原因: 窓の Cancel（数の一致の CONFIRM・同意の CONSENT）は `kwl_bluetooth_answer(id, 0)` →（Linux）`kl_backend_bluetooth_answer` が保留の `RequestConfirmation` に `org.bluez.Error.Rejected` を返すだけで、request は BlueZ の `Pair()` の答えを待っていた。BlueZ は negative reply を controller に送った後、認証の終わりの event を待つ。btvirt の仮想の相手は相手の側（hci1 の bluetoothctl）の答えまで終えないので、`Pair()` が約 35 秒返らず、その間 request が出たまま（`pairing` 1）で頁の Pair の button が薄いまま（s19〜s22）。相手が no と答えて `AuthenticationFailed`（request 9、s23）。
- zedBSD の同じ操作: 窓の Cancel は同じく answer no → bluetoothd が `PAIR_CONFIRM_NEGATIVE` を送り、認証の終わりで `ERROR rejected` → `bt_error()` で ECONNREFUSED → 頁は「The pairing was refused.」（zedBSD も controller の event を待つ。実機では negative reply の後すぐ失敗が来る見込み）。数を見せる窓（PASSKEY）の Cancel は `kl_backend_bluetooth_cancel` → ECANCELED `cancelled`（zedBSD・Linux とも）→ compositor の `bt_applied()` に ECANCELED の行が無く FAILED → 頁の EIO →「It did not work.」。
- 直し（`bluetooth-linux.c`）: `kl_backend_bluetooth_answer` で、答えが no で question が自分の Pair（request が PAIR、段が Pair、question の device の path が request の path）のものなら、Rejected を返した後に `bt_request_give_up()` で request をすぐ終える: `Device1.CancelPairing` を送り、request の call を「誰にも答えない」に変え（遅れて来る `AuthenticationCanceled` は捨てる）、result **ECONNREFUSED、reason `rejected`**（zedBSD の bluetoothd と同じ errno と reason）。頁の文は zedBSD と同じ「The pairing was refused.」、Pair の button はすぐ戻る。窓の 20 秒の期限（`ASK_MS`、answer no）と、画面の lock の間の no も同じ道を通る。`kl_backend_bluetooth_cancel`（PASSKEY の Cancel）は同じ `bt_request_give_up()` を使う（ECANCELED `cancelled`、変わらず）。
- 頁の文の決め: 取り消しに専用の文（「Pairing was cancelled.」）を出すには、compositor の `bt_applied()` と `KL_SYSTEM_RESULT_*`（client の protocol）に cancelled の値を足す必要があり、zedBSD・Linux の両方と libkeiland に触る。p007 の範囲の外なので、今は zedBSD と同じ文（CONFIRM・CONSENT の Cancel は「The pairing was refused.」、PASSKEY の Cancel は「It did not work.」）に揃えた。専用の文にするかは Q1 の判断に残す。

### 12.2 pair の失敗の後も確認の窓が残る（直した。ただし T1 の場面の原因は確定できない）

- source の確かめ: 直す前から、`Pair()` の error（段 Pair）の答えで question の device が request の device なら `ASK_END` を出していた（窓は END で閉じる）。BlueZ が `Agent1.Cancel` を送る場合も END を出す。従って `Pair()` が失敗を返した時点で窓は閉じるはずで、T1 の (3) の「失敗の後も約 15 秒」は、こちらの BlueZ の `Pair()` がまだ返っていない間（相手の側では失敗が見えていても、こちらの BlueZ は自分の Agent の答えを待つ）か、PNG の取り違え（s14・s15 は時計が 13:21、前後の s13・s16 は 18:21。s14・s15 には窓が無い）と推測。compositor の stdout（`KWL BT ask closed via=…`）が記録に無いので、窓が閉じた理由（`end` か `time` か）は分からない。
- 見つけた不具合（直した）: `Pair()` が失敗して question を END で終えても、BlueZ が `Agent1.Cancel` を送らなければ、保留の Agent の call（`pending`）が 30 秒（`BT_PENDING_MS`）残り、その間の次の Pair の `RequestConfirmation` は「Another question is open」で Rejected になる。
- 直し: `bt_pair_question_end()` を足し、Pair の段の答え（成功・失敗）と、Pair の段の request の終わり（`bt_request_finish`: cancel・give up・期限）で、request の device の question を終える: 保留の call に `org.bluez.Error.Canceled` を返して放し、`ASK_END` を出す（zedBSD の `bt_request_end` が request の終わりで question を終えるのと同じ）。`bt_forget_service`（owner が去った・接続を失った）では保留の call を先に捨ててから request を終える（去った owner に返事を送らない、§9.1 のまま）。

### 12.3 Connected: yes の間 button が Disconnect のまま、押すと kind=5（観察、直さない）

- source の確かめ: 頁の button の文と押した時の要求は同じ `drawn[].flags & KL_BLUETOOTH_CONNECTED`（`page-bluetooth.c` の `bluetooth_list` と `se_bluetooth_press`）、flag は compositor の `KL_SYSTEM_BT_CONNECTED` ← backend の `connected` ← BlueZ の `Device1.Connected`（`bt_device_property`、PropertiesChanged で追う）。誤りは見つからない。
- 記録の確かめ: `keiland-stderr-full.txt` で、Connect の 3 回（request 2〜4）の各々の前に頁の layout が `CONTROL index=300 width=103`（Disconnect）から `width=84`（Connect）に変わっている（Connected が yes → no）。つまり押した時の頁は Connect を出していた。profile の無い仮想の相手では、Connect の試み（と pair の後の Connect）で ACL が上がって `Connected: yes` になり、使う profile の無い ACL を BlueZ（か kernel）が数秒で切って `Connected: no` に戻る、と推測。PNG（s7・s8）は yes の間に撮られ、押したのは no に戻った後。仮想の相手の性質で、Connected の扱いの誤りではない。
- host 試験に `disconnected-signal`（PropertiesChanged の `Connected` false で `connected` 0）を足した。

### 12.4 確かめ（host、2026-10-14 P3）

- `sh plan/ws143/tests/bt-linux-host-test.sh build/p3-bt-linux`: **PASS**（plain 66 checks ok、ASan+UBSan 66 checks ok。出力 `build/p3-bt-linux.run.7wWRs6`）。足した確かめ: `disconnected-signal`、5a（偽の org.bluez の `confirm-hold`: no の後も Pair が相手の側を待つ btvirt の形）`no-ends-at-once`（1 秒以内に ECONNREFUSED `rejected`、`pairing` 0）・`no-rejected`（Agent の答えは Rejected）・`no-cancels-pairing`（CancelPairing が送られ、遅れた答えは result にならない）、5b（`confirm-fail`: question の間に Pair が `AuthenticationCanceled` で失敗し、BlueZ は Cancel を送らない）`fail-ends-question`（question の id の END が result と共に来る）・`fail-lets-call-go`（保留の call は Canceled で放される）、5c `next-pair-asked`・`next-pair-no`（次の Pair の question がすぐ出る）。
- 同じ試験を直す前の `bluetooth-linux.c`（git stash で一時に戻した）で: 5 FAIL（`no-ends-at-once`・`no-cancels-pairing`、その request が残るため 5b・5c も EBUSY で落ちる）。出力 `build/p3-bt-linux-old.run.CCjRUU`。
- `sh plan/ws143/tests/bt-page-result-host-test.sh build/p3-keiland-linux`: **PASS**（12 件。足した: 窓の Cancel の 2 つ、ECONNREFUSED（頁の EACCES）→「The pairing was refused.」、ECANCELED（頁の EIO）→「It did not work.」）。
- `make -f userland/desktop/keiland-linux.mk -j8 KEILAND_LINUX_BUILD=build/p3-keiland-linux build/p3-keiland-linux/bin/wayland`: 成功、warning 0（log `build/p3-p007-build6.log`）。
- `sh plan/tools/keiland-os-boundary/check.sh`: PASS。`git diff --check`: 問題なし。
- zedBSD の build: 実施していない（zedBSD が build する file は変えていない）。

### 12.5 T1 への再試験の依頼（Q1 が T1 に送る文）

> T1-604 の観察の再試験（ws143-p007、P3 56f416f41）。T1-604 と同じ環境（WS131 の Debian 13 の QEMU+KVM の guest、SSH・QMP PNG の例外、main の tree の `make keiland-linux` と install、bluez・bluez-test-tools、`modprobe hci_vhci; btvirt -l2 &`、Keiland を kei で systemd-run（PAMName=login、tty1）、相手 hci1 は bluetoothctl の agent と default-agent を Keiland の後に）。**compositor の stdout（`KWL BT ask …`・`KWL BT answer …` の行）も stderr と共に取る**（窓が閉じた理由 `via=` を見るため）。(1) Other Devices の相手を Pair → 数の一致の窓で **Cancel**、相手の側は答えない: 1〜2 秒以内に窓が閉じ、頁が「The pairing was refused.」で Pair の button が濃く戻ること（PNG を Cancel の直後と 3 秒後）。stderr に `request N kind=3 error=111 reason=rejected`、stdout に `KWL BT answer no via=press`。その後、相手の側の bluetoothctl の確認を no か放置で終えさせてから、もう一度 Pair → 窓で Pair（相手 yes）で paired になること（`InProgress` で「Bluetooth is busy. Try again in a moment.」が出たら数秒後に押し直し、その旨を記録）。(2) Forget（Remove）して相手の側で remove せずに Pair（T1-604 の (3) と同じ古い bond）: 失敗の文「The pairing was refused.」が出た時に窓が残っていないこと。残っていれば何秒残るかと stdout の `KWL BT ask closed via=`（`end` か `time` か）、stderr の request の行（PNG を失敗の文が出た直後）。(3) 観察: paired の相手の行の button（Disconnect・Connect）を押す直前の PNG と、その時の `bluetoothctl info` の Connected、stderr の request の kind（Disconnect は 6、Connect は 5）が合うこと。合否は (1) と (2) の PNG・stderr・stdout。FreeBSD は省く。
