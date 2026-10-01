<!-- awesome-plan project=zedbsd record=ws105-p010 -->

# ws105-p010: libkeiland の Linux の backend（wpa_supplicant・Linux の interface・ALSA）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p008（Settings が Linux で動く）
実行者: phase-runner（high）か phase-runner-mid。**始める前に [design.md](../design.md) の §6 を読む。**

## 目的

決定 D14・D15。p002 の仮の backend を本物にする。`keiland.h` の API と意味は変えない（app は変えない）。

| file | 実装（design の節） |
| --- | --- |
| `libkeiland/wpa/network-wpa.c` | §6.1: wpa_supplicant の制御 socket（`/run/wpa_supplicant/<ifname>`）で状態・scan・接続・切断・on/off |
| `libkeiland/linux/network-link-linux.c` | §6.2: `getifaddrs`・`/sys/class/net`・`/etc/resolv.conf`・鍵の保存（wpa_supplicant の `ADD_NETWORK`・`SET_NETWORK`・`SAVE_CONFIG`） |
| `libkeiland/linux/audio-linux.c` | §6.3: ALSA の control の device の ioctl で音量と mute、event、`keiland_audio_available` |

wpa_supplicant の制御 socket の client は、自分で書く（`wpa_ctrl.c` を読んで形を知るのはよいが複写しない）: `socket(AF_UNIX, SOCK_DGRAM)`、自分の側を
`/tmp/keiland-wpa-<pid>-<n>` に `bind`、相手に `connect`、command を `send`、応答を `recv`（timeout 2 秒）。`ATTACH` した別の socket で `<N>CTRL-EVENT-...` を受ける。
終わりに自分の socket の file を `unlink`。権限: socket の directory の group（Debian の既定は `netdev`）。利用者 kei は `netdev` に入っている（p001）。

## 試験（guest）

### 試験用の WiFi（`mac80211_hwsim`）

`plan/tools/keiland-linux/wifi-setup.sh`（guest の中で root で走らせる script、`guest.sh put` で送る）:

1. `modprobe mac80211_hwsim radios=2`（`wlan0`・`wlan1` ができる）。
2. `wlan1` で hostapd を起動（SSID `keiland-test`、WPA2-PSK、passphrase `keiland-pass`、channel 6。設定は script の中で `/tmp/hostapd.conf` に書く）。
3. `wlan0` で wpa_supplicant を起動（`ctrl_interface=DIR=/run/wpa_supplicant GROUP=netdev`、`update_config=1`、network の定義は無し、設定は `/tmp/wpa.conf`）。
4. IP は試験に要らない（接続の状態だけを見る）。

### library の直接の試験

`plan/tools/keiland-linux/network-probe.c`（我々の libkeiland に link。guest で kei の利用者で走らせる）:

1. `keiland_network_open` → update を 10 秒まで回して `reachable = 1`・`wifi` が `DISCONNECTED` か `SEARCHING`。
2. `KEILAND_NETWORK_REQUEST_SCAN` → `CHANGED_SCAN` を待ち、scan に `keiland-test`（`secured = 1`）。
3. `keiland_network_save_key("keiland-test", "keiland-pass")` → `REQUEST_PROFILES` → `REQUEST_JOIN "keiland-test"` → 30 秒まで待って `wifi = CONNECTED`・`ssid = keiland-test`。
4. `REQUEST_DISCONNECT` → `DISCONNECTED`。
5. `keiland_network_get_links` に `wlan0`（WiFi）と有線の interface、`get_saved` に `keiland-test`。
6. `network-probe: PASS`。

`plan/tools/keiland-linux/audio-probe.c`（同じく）:

1. `keiland_audio_available()` が 1。`keiland_audio_open` → update で `reachable = 1`・`device = 1`。
2. `keiland_audio_set_volume(audio, 40, 40, 0)` → update で state の `left = right = 40`。`amixer -c 0 get Master`（または PCM）の割合が 40% 前後（`popen` で読む。ALSA の dB の曲線の差で ±2 を許す）。
3. 他の process（`amixer -c 0 set Master 70%`）の変更が `keiland_audio_fd` の readable と update の `CHANGED_VOLUME` で届く。
4. mute の on・off。`audio-probe: PASS`。

### app での確かめ

- Settings の Network の頁（compositor の上、p006 の手順）: `keiland-test` が一覧に出る screenshot、鍵を入れて接続する（QMP の key で打つ）→ 接続の表示の screenshot。
- system bar の network の icon が接続を表す。音量の icon で音量を変える（click）→ `amixer` の値が変わる。Settings の Sound の頁の slider も同じ。

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS。
2. `network-probe: PASS`・`audio-probe: PASS`（guest）。
3. app での確かめの screenshot（PNG をユーザーに見せる）。
4. zedBSD の回帰: libkeiland の共通の file を変えていなければ build だけ。変えたなら design §9.2 と `plan/ws089/tests/settings-regress.sh`・`plan/ws100/tests/host-audio.sh`。
5. design §10 の V7 の結果を記録。`mac80211_hwsim` が使えない場合は、wpa_supplicant の制御 socket の偽物の server（試験の道具）を相手に 2 の network の部分を確かめ、そう記録する。

## 結果

（実行の後に書く）
