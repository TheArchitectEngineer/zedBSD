# ws143-p007 review-1（phase.md bd4f767a6、途中で 4ff97d093 まで、の設計の敵対的 review、2026-10-14、Q1 が起動した design-reviewer）

確かめに使った物: dbus-linux.c・.h、notify-linux.c、seat-logind-linux.c、keiland-backend.h 690〜863、bluetooth-unsupported.c、bluetooth-zedbsd.c、compositor の bluetooth-shell.c・bluetooth-ask.c・main.c の poll・system.c の tick、host の /usr/share/dbus-1/system.d/bluetooth.conf と system.conf の limits、org.bluez.service、systemctl is-active/is-enabled bluetooth、/etc/bluetooth/main.conf、bluez 5.82 の man（org.bluez.Adapter・Device・Agent・AgentManager・Battery）。GPL の source は読まない。

**判定: このままでは実装に入れない、直せば進めてよい。** 構成（専用の connection、Introspect と GetAll と signal、待たない call、DisplayYesNo の Agent）は妥当。重大 4 と中 M1・M3・M5・M7・M8 を phase.md に入れれば構成は変わらないので再 review なしで実装してよい（Q1 の判断）。HAL・UAPI・toolchain の変更不要。L1 は利用者に見える制限でユーザーの受け入れを記録。

## 重大

**R1. 同期の dbus_call を混ぜると待たない call の返事と Agent の call が黙って消える。** dbus_call は待つ間、自分の serial の返事と signal 以外（他の返事・error、METHOD_CALL）を dbus_reply_free で捨てる（dbus-linux.c:289-314）、dbus_add_match も同期（333）、期限 DBUS_TIMEOUT_MS＝5 秒（27）で期限が来ると dbus_close（268-272）。§3.1 の GetNameOwner、§5.4 の RegisterAgent・RequestDefaultAgent、開き直した後の AddMatch をどちらで送るか書かれていない。1 つでも同期だと 5 秒の間の物が消え、Pair の返事が消えれば 90 秒後に ETIMEDOUT、RequestConfirmation が消えれば BlueZ は自分の期限まで待ち、bluetoothd が遅いと main loop が最大 5 秒止まる。直し: §4 に不変条件「この connection で同期にしてよいのは開く時の Hello と AddMatch（宛先 org.freedesktop.DBus）だけで、最初の待たない call と Agent の export より前。他（GetNameOwner、Register／RequestDefault／UnregisterAgent、Introspect、GetAll）は全部 dbus_send_call」。別案は dbus_call が METHOD_CALL と自分のでない返事も queue に積む（共有の code の変更、seat と notify への影響を確かめる）。

**R2. 接続を失った時と bluetoothd が入れ替わった時の回復が無い（HEAD の 63 行で一部対処）。** どの受け取りの誤りでも bus を閉じる（dbus-linux.c:382-384・525-527）、送りの失敗でも閉じる（bus_emit、dbus_call 259-262）、dbus_open_system は connect・認証・Hello を同期で 5 秒まで（96-131）。HEAD の「次の tick で開き直す」は dbus-daemon が応答しないと 10 ms ごとの tick のたびに最大 5 秒止まり、64 KiB 超の返事が繰り返すと閉じて開き直す loop が止まらない。62 行の「EMSGSIZE など」は存在しない（EOVERFLOW と close）。直し: 状態機械として、開き直しは backoff（BT_RETRY_MS 1 秒 → 30 秒）、接続を失った時と NameOwnerChanged（owner の変化）で出ている request に ENOTCONN、question に ASK_END、保留の Agent の call を捨て、serial と device・adapter の表を空に。開き直した時は AddMatch・RegisterAgent・RequestDefaultAgent をやり直し、scan を求めている間は StartDiscovery を送り直す（BlueZ は切断で discovery を止める）。adapter の電源の入り直し・差し直しでも discovery を送り直す。

**R3. match と最初の読みの順の競合（§3.3、59-61）。** 「Introspect → GetAll → signal（match を足す）」の順で、読みと match の間に現れた device・変わった property を取りこぼし、周期の再読みは adapter の GetAll だけ（62）なので次の再接続まで直らない。直し: match（NameOwnerChanged arg0=org.bluez、InterfacesAdded・InterfacesRemoved、PropertiesChanged）を先に → GetNameOwner → Introspect と GetAll。同じ送り手は順に届く。GetAll が UnknownObject ならその device を捨てる、InterfacesAdded と GetAll は upsert、GetAll を待つ間に InterfacesRemoved が来たら結果を捨てる（表の項目に path）。

**R4. 試験の計画が書いたとおりには実行できない（§6、139-141）。** (a) host の bluetoothd は動いていない（2026-10-14 inactive、enabled、controller 無し）。(b) 私的な試験の bus に繋げない: dbus_open_system は path が固定（dbus-linux.c:112-115）、BT_SOCKET_PATH（bluetooth-zedbsd.c:43-46）と同じ形の build 時の define か dbus_open_address が要る。(c) 一番危ない部分（error の写し、owner の再起動、Agent の Cancel・Release、返事の順、64 KiB 超過と再接続、32 の上限）は QEMU の btvirt では確かめられない → host の dbus-python 1.4.0 と gi で私的な bus の上に偽の org.bluez（Adapter1・Device1・AgentManager1、error を返す台本）の host 試験。(d) QEMU の手順: Keiland を root でない利用者で（[F13] を実行で確かめる）、bluez-test-tools（5.82-1.1）に btvirt が入っているか未確認、1 つの bluetoothd が hci0 と hci1 を持つと相手側の pairing も default agent に行くので bluetoothctl default-agent を Keiland の後に、仮想の controller が SSP の numeric comparison を行うか未確認（Just Works だと ASK_CONFIRM が出ない）。(e) Linux の build（keiland-linux.mk、warning 0）と plan/tools/keiland-os-boundary/check.sh（guardrail:35）が確かめの一覧に無い。

## 中

- M1. D-Bus の auto-start: host の org.bluez.service は SystemdService=dbus-org.bluez.service、bus_build の flags 0 で auto-start が効き、owner が居ない時に org.bluez へ送るたびに systemd が bluetoothd を起こそうとし service_start_timeout 既定 25 秒（system.conf:119）で返事が遅れる（2 秒ごとの GetAll のたび）。直し: dbus_send_call に flags を足し NO_AUTO_START（0x2）、owner が居ない間は org.bluez に送らない。
- M2. PowerState は experimental（man Adapter「[readonly, experimental]」、/etc/bluetooth/main.conf は #Experimental = false）で Debian の既定では出ない（推測、guest の busctl introspect で確かめる）。35 行の「5.66 以降」は誤りで Powered の経路が普通。rfkill の block は区別できない。off-enabling は KL_BACKEND_BT_STARTING（keiland-backend.h:719）に、試験は PowerState が無い経路を主に。
- M3. 起動時の Battery1 が取れない: GetAll は interface ごとで GetAll(Device1) に Battery1 は入らず、起動時に既に在る device には InterfacesAdded が来ない、Percentage は byte（y）。直し: Battery1 の GetAll（Introspect で在るか見るか送って InvalidArgs を無視）、InterfacesRemoved を interface ごと（Battery1 なら -1、Device1 の時だけ消す）、variant の読みに y・n・q・b。
- M4. PropertiesChanged の invalidated（sa{sv}as の as）を扱っていない（61 行は a{sv} だけ）。無くなった property（discovery の後の RSSI など）は既定値（RSSI 0）に。
- M5. 上限 32 と表から落ちた device（54）: 落とした device の PropertiesChanged に Address が無く戻せない、paired が 32 を越えると失われその device への PAIR は EINVAL。直し: 内部の表は大きく（例 128、paired はいつも残す）外に出す時だけ 32、知らない path の signal が来たら GetAll。
- M6. 変化の通知の洪水: discovery の間 RSSI の PropertiesChanged のたびに CHANGED_DEVICES、bluetooth-shell.c:216-224 が全 object へ送り log も 1 行（zedBSD は 2 秒ごと）、filter を設定すると既定の RSSI の差の閾値が効かない（man Adapter 59-60）。直し: RSSI だけの変化は watch の周期（2 秒）にまとめる、SetDiscoveryFilter({"Transport":"auto"}) は既定と同じなので削る（§4 の a{sv} の書きも不要に）。
- M7. 内部の call の期限と表の溢れ: 26 行の表は 8 個で期限は request の 90 秒だけ、system.conf に reply_timeout の設定無し（daemon 側の期限は無い、推測）、bluetoothd が固まると項目が漏れ止まる。直し: 項目ごとに期限（内部は約 25 秒、Pair は 90 秒）、満ちた時は送りを延ばす。
- M8. error の写しの漏れと誤り（§5.3、104-111）: Connect の AlreadyConnected と Disconnect の NotConnected は 0（成功）、Pair の AlreadyExists は EBUSY でなく 0 か EINVAL、RegisterAgent の AlreadyExists は登録済み、NoReply で reachable を 0 にしない（owner の追跡だけで決める）。漏れ: org.bluez.Error の NotSupported・NotAvailable・NotAuthorized・Rejected・Canceled、org.freedesktop.DBus.Error の UnknownObject・UnknownMethod・InvalidArgs・AccessDenied・LimitsExceeded。reason は Failed の時に error の本文（br-connection-page-timeout など）を 31 byte に切って使う。
- M9. Agent の call の受け入れの検査: sender が今の org.bluez の owner の unique name、path が Agent の path、interface が Agent1。知らない method は UnknownMethod。1 つ保留中に 2 つ目が来た時（Rejected か待ち行列）を決める。
- M10. AuthorizeService（125）は pairing の後の service の接続の許可で、ASK_CONSENT（「pair するか」、keiland-backend.h:754）で尋ねると誤らせ、uuid を見ずに許すとどの profile も通る。Pair の後に Trusted を立てるので自分が pair した device からは来ない（推測）。未 paired は拒む、paired だが untrusted は HID の UUID（0x1124・0x1812）だけ尋ね他は拒む。→ **2026-10-14 ユーザーの決定「HID だけ尋ね、他は拒む」**。
- M11. ASK_PASSKEY の終わり方: DisplayPasskey・DisplayPinCode の終わりは Cancel で知らされる（man Agent）、成功の時に Cancel を呼ぶか未確認で、呼ばないと窓は compositor の 60 秒（bluetooth-ask.c:45）まで残る。自分の Pair の返事が来た時とその device の Paired が true になった時にも ASK_END。
- M12. Pair の後の接続: zedBSD の PAIR は HID の link を引き継ぐ（phase005 §4.12、311-313）が BlueZ の Pair は pairing の後に service の発見で profile は接続しない（推測）、BR/EDR のキーボードが相手から接続し直すまで使えない恐れ。→ **2026-10-14 ユーザーの決定「Pair → Trusted → Connect（zedBSD と同じ）」**、Connect の失敗は log、result を出す時点も決める。
- M13. Trusted を自動で立てる（87）は service の許可を尋ねられなくなる安全の方針。→ **2026-10-14 ユーザーの決定で zedBSD と同じく立てる**。

## 軽微

- m1. 28 行の「fd を poll に足す」は不要で足すと p006 の口を変える（poll の期限は最大 10 ms、main.c:1021-1024、kwl_bluetooth_tick は毎回、system.c:542、Bluetooth の口に fd を返す関数は無い）。
- m2. 9 行の「p006 test-wait」は古い（phase006 は 2026-10-08 に cleared）、ws.md の行も古い。
- m3. 87 行の「address から path を作る」は表に持った path を使う（LE の privacy で path と Address がずれる恐れ、推測）。
- m4. AddressType の写し（47）: 5.82 の man に Bearer の interface は無く LE だけの public な device が BR/EDR と表示される。Class が在れば BR/EDR、Appearance だけなら LE_PUBLIC。
- m5. Alias はいつも在る（man Device）ので 48 行の「無ければ address」には来ない。Name が在るかで決め 63 byte に UTF-8 の境界で切る。
- m6. own（132）は「Pair の間」でなく自分の Pair の device の path と比べる。
- m7. input-unsupported（128）を result に載せる仕組み（拒んだ印を持ち Pair の error の reason を差し替える）。reason は log にしか出ない（bluetooth-shell.c:692）。
- m8. adapter を探す手順（/org/bluez の Introspect）が抜けている。Introspect の大きさの見積に interface の XML（数 KB）も。
- m9. close の時に出ている Pair があれば CancelPairing、UnregisterAgent は無くても切断で外れる。どちらも送りが止まらないように。

## L1〜L5

- L1（DisplayYesNo）: 技術的に妥当、利用者に見える制限でユーザーの受け入れの記録が要る。制限は書いたより狭い（man Agent: DisplayPinCode の PIN はいつも 6 桁で SSP の無い古いキーボードに使われ、古いキーボードは ASK_PASSKEY で pairing できる）。失敗するのは RequestPinCode（PIN の要る古いキーボードでない device）と RequestPasskey（相手が DisplayOnly）。BlueZ が一般的な PIN を自分で試すかは推測、QEMU で確かめる。→ **2026-10-14 ユーザーの決定「制限を受け入れる」**。
- L2（RequestDefaultAgent）: 賛成。自分の connection の agent は default かどうかに関わらず自分の Pair に使われ、default は相手からの pairing と他の app の時だけ。利用者の切り替えや別の VT の session では後に登録した方が勝ち、question が表に出ていない session に行く恐れ（lock の時の自動の no は bluetooth-ask.c:116、表に出ていない VT は lock ではない）。session が active になった時に送り直すか制限として記録。
- L3: 賛成、R3・M3・M5・m8 と超過の時の backoff（R2）を加える。
- L4: 賛成。notify の callback は METHOD_CALL 以外を無視する（notify-linux.c:422）ので返事を渡しても既存は変わらない。足す物: flags（NO_AUTO_START）、*_to の返事（HEAD で済み）、R1 の不変条件。
- L5（専用の connection）: 賛成。BlueZ は Pair を呼んだ connection の agent を使い agent の寿命も connection に結びつくので分けるのが必要。seat の bus（seat-logind-linux.c:90）とは別。

確かめていない: build・試験、QEMU の guest での PowerState の有無、btvirt の入手、SSP の数の一致、Cancel の振る舞い、BlueZ が PIN を自分で試すか。
