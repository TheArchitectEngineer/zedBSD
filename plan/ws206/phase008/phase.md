<!-- awesome-plan project=zedbsd record=ws206-p008 -->

# ws206-p008: WLAN の core の FullMAC の engine（wlan-fullmac.c）と station の分岐と host の試験

Parent: [WS206](../ws.md)
Status: in-progress（2026-10-13 P3: 実装・host 試験（ASan/UBSan・TSan・既存の SoftMAC の試験）・amd64 と rpi4 の vmunix の build（warning 0）まで済み。Q1 の review と判定まで cleared にしない）
Phase disposition: normal
Queue: P3 の lane（2026-10-13 Q1 の割当。設計は [p001](../phase001/phase.md) の第 3 版の W6、[review-3](../phase001/review-3.md) で p008 は「可」、条件は指摘 5・6 と軽微 1）

## 目的・範囲

設計 p001 の W6（E3・Q-S1）。FullMAC の radio（Pi 4 の BCM43455）のために、WLAN の core に `wlan_wpa2_engine_*` と同じ面の FullMAC の engine を足し、station（`wlan.c`）は engine の分岐だけにする。kernel の内部の API だけを足し、HAL・hal.h・UAPI は変えない。既存の SoftMAC の driver（AX211・RTL8822BU）の動きは変えない。driver（p004・p005）は範囲の外。

受け入れ条件:

1. `include/kern/net/wlan.h` に FullMAC の op 2 つ（`fullmac_connect`・`ethernet_transmit`）、connect の要求の型、event の型、report の関数 2 つ（`wlan_station_report_fullmac_event`・`wlan_station_report_ethernet`）。op は `struct wlan_radio_ops` の末尾に足す（位置で初期化する既存の試験の表を壊さない）。
2. `src/kern/net/wifi/wlan-fullmac.c`・`.h`: W6.2 の engine（init・start・report・fail・timer・stop・state・last_error・next_deadline）。engine は start から戻った後に PMK を持たない。
3. station の W6.3 の表の全ての行を mode で分ける。SoftMAC の時は今の WPA2 の engine をそのまま呼ぶ。
4. review-3 の指摘 5（`report_ethernet` は engine を lock 無しで読まない）、指摘 6（attach の ops の検査を mode で書き分ける、FullMAC の channel は OFFLOADED_DWELL だけ）、軽微 1（BUG-157 の最初の理由を `wlan_fullmac_engine_fail` でも保つ、loss の後の同じ generation の event を無視する）。
5. host 試験: engine と station の表の行（ASan・UBSan）、並行（TSan）、既存の SoftMAC の host 試験 `plan/ws005/tests/host-wlan-retire.c` が変わらず PASS。
6. amd64・rpi4 の vmunix の build が warning 0（`-Werror`）。

## 実装（2026-10-13 P3）

変えた file:

| file | 内容 |
| --- | --- |
| `include/kern/net/wlan.h` | `WLAN_FULLMAC_PMK_LENGTH`、`struct wlan_fullmac_connect_request`、`enum wlan_fullmac_event`、`struct wlan_radio_ops` の末尾に `fullmac_connect`・`ethernet_transmit`、report の関数 2 つ、「Drivers cannot publish AUTHORIZED directly」の comment に FullMAC の文（W6.1・表の 22）、test の snapshot の `reserved[2]` の 1 byte を `fullmac` に |
| `src/kern/net/wifi/wlan-fullmac.h`・`.c`（新規） | engine（W6.2） |
| `src/kern/net/wifi/wlan.c` | station の分岐（W6.3 の表）、新しい report の関数、mode の検査、SoftMAC の WPA2 の profile の組み立てを `station_wpa2_start()` に移した（中身は同じ） |
| `Makefile` | `KERN_NET_SOURCES` に `src/kern/net/wifi/wlan-fullmac.c`（amd64・arm64 の両方がこの一覧を使う） |
| `plan/ws206/tests/fullmac-host-test.c`・`.sh`（新規） | host 試験 |

### driver から見える内部の API（p004・p005 の P4 と合わせる所）

```c
#define WLAN_FULLMAC_PMK_LENGTH 32U

struct wlan_fullmac_connect_request {
	uint64_t generation;          /* この接続の generation。event・link loss・ethernet はこの値で報告する */
	uint64_t deadline_ticks;      /* 全体の期限（clock_ticks の領域、作った時から 30 s） */
	struct wlan_bss_record bss;   /* 選んだ BSS（SSID・BSSID・channel・security） */
	uint8_t pmk[WLAN_FULLMAC_PMK_LENGTH];  /* PBKDF2-SHA1(passphrase, SSID, 4096) */
};

enum wlan_fullmac_event {
	WLAN_FULLMAC_EVENT_ASSOCIATED = 1,   /* firmware が AP に繋いだ（SET_SSID の成功・LINK up） */
	WLAN_FULLMAC_EVENT_AUTHORIZED = 2    /* firmware の supplicant が 4-way と group key を終えた（PSK_SUP の keyed） */
};

/* struct wlan_radio_ops の末尾（restart の後） */
int (*fullmac_connect)(void *context, const struct wlan_fullmac_connect_request *request);
int (*ethernet_transmit)(void *context, uint64_t generation, const uint8_t *frame, size_t length);

int wlan_station_report_fullmac_event(struct wlan_station *station, uint64_t generation, enum wlan_fullmac_event event);
int wlan_station_report_ethernet(struct wlan_station *station, uint64_t generation, const uint8_t *frame, size_t length);
```

約束（`wlan.h` の comment と同じ）:

- **mode は attach の時に決まる**: `fullmac_connect` が非 NULL なら FullMAC。FullMAC の radio は `scan_channel_start`・`scan_stop`・`disconnect`・`ethernet_transmit`・`quiesce`・`stop_retry` を全て持ち、SoftMAC の `connect_start`・`management_transmit`・`association_set`・`association_clear`・`frame_transmit`・`key_install`・`key_delete`・`keys_activate` を全て NULL にする。違えば attach は EINVAL。SoftMAC の radio が `ethernet_transmit` を持つのも EINVAL。`restart` は任意（SoftMAC と同じ）。
- **scan の profile**: FullMAC では全ての channel が `WLAN_SCAN_CHANNEL_OFFLOADED_DWELL` で、`WLAN_SCAN_CHANNEL_ACTIVE_ALLOWED` は付けない（`management_transmit` が無いので EINVAL）。escan の active・passive は driver の私的な表で持つ（review-3 の 6）。`wlan_station_scan_profile_update` も同じ検査。scan は SoftMAC と同じ段: `scan_channel_start` → driver が `report_scan_channel_ready` → 結果 `report_scan_frame`（ready の前の結果は core が ESTALE で断る）→ `report_scan_channel_complete` → 最後の channel の後に core が `scan_stop`。core の期限は ready まで 500 ms（`WLAN_SCAN_TUNE_DEADLINE_TICKS`）、ready から complete まで 1 s（`WLAN_SCAN_OFFLOAD_DEADLINE_TICKS`）。
- **`fullmac_connect`**: control gate の下・thread の文脈で呼ばれる（network worker の timer から）。driver は request を自分の slot に写してすぐ返る（待たない）。core は戻った直後に request の PMK を消す。driver の写しの PMK は firmware に渡した後に driver が消す（p005、W7）。0 を返すと core は `disconnect` を借りとする（失敗を返しても core は後で `disconnect` を呼ぶ。`disconnect` は冪等であること）。
- **結果は event で**: 成功の段は `wlan_station_report_fullmac_event`（ASSOCIATED → AUTHORIZED、firmware が 1 つにまとめた時は AUTHORIZED だけでよい）。失敗・切断は全て既存の `wlan_station_report_link_loss(station, generation, error)`（error は W7 の表、p005 で確定）。poll・thread の文脈（`poll_receive` から）で呼ぶ。core は control gate を取る。違う generation は ESTALE、loss の後の同じ generation の event も ESTALE（無視してよい）。
- **AUTHORIZED と carrier**: core の FullMAC の engine が AUTHORIZED を受けると core が carrier（controlled port）を上げる。driver は carrier を触らない。loss・disconnect では core が下げる。
- **`ethernet_transmit`**: core は control gate を持ったまま、port が開いている時だけ、その接続の generation と 802.3 の frame（14〜1514 byte）を渡す。driver は写して送りの ring に入れすぐ返る（満杯は ENOBUFS）。core は EAPOL（0x888e）の送りを EACCES で断る（firmware の supplicant の物）。port が閉じていれば ENETDOWN。net device の `transmit` は SoftMAC と同じく `wlan_station_transmit()` を呼べばよい。
- **`wlan_station_report_ethernet`**: `poll_receive` から、受けた 802.3 の frame（14〜1514 byte）を渡す。その generation の port が開いていれば `net_device_receive` へ。EAPOL は 0 を返して捨てる。port が閉じていれば EACCES、古い generation は ESTALE、SoftMAC の station では EOPNOTSUPP。判定は station の spinlock の下で station の公開の値（`connection_generation`・`controlled_port`・state）だけを見る（review-3 の 5。engine は読まない）。control gate は取らない。
- **SoftMAC の入口**: FullMAC の station では `wlan_station_report_frame`・`wlan_station_report_tx_complete` は EOPNOTSUPP。test の SoftMAC 専用の関数（`seed_authorized`・`begin_*_rekey`・`set_initial_phase`・`complete_authorized`・`transmit_eapol`）も EOPNOTSUPP。
- **beacon の監視**: FullMAC では core は beacon を見ない（firmware が DEAUTH・LINK down で知らせ、driver が link loss で報告する）。
- **期限**: AUTHORIZED の前に 30 s（`WLAN_CONNECT_DEADLINE_TICKS`）を過ぎたら core が ETIMEDOUT で失敗にし、`disconnect` を呼ぶ。
- lifecycle（open・close・quiesce・detach・shutdown・stop_retry・restart）の約束は SoftMAC と同じ。

### engine（`wlan-fullmac.h`、kernel の private）

W6.2 の形のまま。設計で決まっていなかった所を次のように決めた:

- `struct wlan_fullmac_profile { struct wlan_bss_record bss; const uint8_t *passphrase; size_t passphrase_length; uint64_t total_deadline_ticks; }`。
- `struct wlan_fullmac_engine` は `ops`・`callback_context`・`state`・`generation`・`total_deadline_ticks`・`last_error`・`configured`（connect を呼んだ = `radio_stop` が借り）・`authorized`（`authorized_set(1)` を呼んだ = `authorized_set(0)` が借り）と、**connect の間だけ request を置く scratch**（`request`）。scratch は connect の直後と失敗の経路で消し、`wlan_fullmac_engine_secrets_clear()` で 0 を確かめられる（stack に置くより試験で確かめられる。「engine は PMK を持たない」は「connect の呼び出しの外では持たない」として満たす）。
- `start`: IDLE の時だけ（それ以外は EBUSY、station が前の接続を retire してから呼ぶ）。profile の不正（SSID 0・33 以上、passphrase 8〜63 の外、期限 0）は EINVAL で状態を変えない。受け付けた後の失敗（期限切れ、PBKDF2、connect の失敗）は FAILED。connect の前に `configured` を立てる（WPA2 の engine の `radio_start` と同じく、失敗でも radio の後始末を借りとする）。
- `report`: 古い generation と、IDLE・FAILED の engine への event は ESTALE。AUTHORIZED の後の event は 0 で何もしない。CONNECTING・ASSOCIATED の重複も 0。**期限を過ぎた後の event は ETIMEDOUT で失敗にする**（30 s の後に来た AUTHORIZED で接続を救わない）。`authorized_set(1)` の前に `authorized` を立て、失敗なら FAILED（port は stop が閉じる）。
- `fail`: 最初の理由を保つ（BUG-157、review-3 の軽微 1）。AUTHORIZED なら `authorized_set(0)` を試み、失敗は stop に残す。戻り値は記録できれば 0。
- `stop`: port を閉じる → radio を止める の順の checked barrier。どちらかの失敗は FAILED のまま（最初の理由を保ち、無ければその error）にして error を返す。次の stop が残りからやり直す。両方済めば IDLE。
- `timer`: CONNECTING・ASSOCIATED で期限を過ぎたら FAILED（ETIMEDOUT）。

### station の分岐（W6.3 の表の行と実装）

| # | 箇所 | 実装 |
| --- | --- | --- |
| 1 | attach | `radio_ops_mode_valid()`（scan の対、mode ごとの op の組。SoftMAC の検査は前と同じ意味に書き直した）と `scan_profile_mode_valid()`、`wlan_fullmac_engine_init`、`fullmac` を ops から決める。WPA2 の engine も init する |
| 2・3 | report_frame・report_tx_complete | FullMAC で EOPNOTSUPP（`fullmac` は attach で固定なので lock 無しで読む。SoftMAC の経路は前と同じ） |
| 4 | transmit | FullMAC は `station_fullmac_transmit()`: 長さ・EAPOL の検査、lock の下で generation と port を見て、lock の外で `ethernet_transmit` |
| 5 | link loss | `station_engine_idle()` で ENOTCONN、FullMAC は `wlan_fullmac_engine_fail` の後 `station_engine_stop()` |
| 6・7 | 新しい report の関数 | 上の API |
| 8 | beacon の監視 | FullMAC は常に 0 |
| 9・15・21 | retire の終わり・ioctl_scan・scan・lifecycle | 変えない |
| 10 | sync | `station_sync_engine_locked()` が mode で分ける。FullMAC の写し `station_sync_fullmac_locked()`: CONNECTING → ASSOCIATING、ASSOCIATED → FOUR_WAY（authenticated・associated）、AUTHORIZED → CONNECTED（key_installed・controlled_port、terminal_error 0、connection_deadline 0、beacon の監視なし）、FAILED → FAILED（last_error、connect_stop_pending、retry 1 tick）。controlled_port は毎回 engine の AUTHORIZED から写す |
| 11 | radio の start | `station_fullmac_connect()`: 選んだ BSS と generation を確かめ `ops->fullmac_connect`、成功で `connect_driver_active = 1` |
| 12 | SoftMAC の鍵・frame の関数 | 呼ばれない（FullMAC では WPA2 の engine を start しない） |
| 13 | authorized_set | `station_fullmac_authorized_set()`: generation を確かめ carrier を上げ下げ（鍵の検査なし） |
| 14 | radio_stop | `station_wpa_radio_stop` を共用 |
| 16 | ioctl_connect | mode の connect の op で検査、`station_engine_idle()` |
| 17 | retire | `station_engine_idle()`・`station_engine_stop()` |
| 18 | connection_start | FullMAC は `station_fullmac_start()`、SoftMAC は `station_wpa2_start()`（前の profile の組み立てを移しただけ）。passphrase は呼び出しの後に消す、sync |
| 19 | connection_timer | engine の状態は `station_engine_idle()`・`station_engine_failed()`、timer・stop・sync は mode の関数 |
| 20 | test の関数 | `secrets_clear` は FullMAC の engine の scratch も見る、SoftMAC 専用の関数は EOPNOTSUPP、snapshot の `wpa_state` に FullMAC の engine の状態、`fullmac` の印 |
| 22 | wlan.h の comment | FullMAC の文を足した |

### 判断（設計に無かった所、P3 が決めた）

1. op は `struct wlan_radio_ops` の**末尾**に足した（W6.1 は位置を決めていない）。`plan/ws005/tests/host-wlan-retire.c` が位置で初期化しているため。
2. FullMAC の radio に `quiesce`・`stop_retry` を必須にした（W6.1 の「要する」の通り）。`restart` は任意のまま。
3. engine の PMK は stack ではなく engine の中の scratch に置き、connect の直後に消す（試験で 0 を確かめるため）。
4. 期限を過ぎた後の event は接続を救わず ETIMEDOUT（W6.2 は timer だけを書いていた）。
5. engine の stop の失敗は最初の理由を保つ（WPA2 の engine は上書きするが、FullMAC は BUG-157 の規則に合わせた）。
6. `report_ethernet` の EAPOL は 0 で捨てる（driver に error を数えさせない）。port が閉じている時は EACCES、古い generation は ESTALE。
7. `ethernet_transmit` への EAPOL の送りを core が EACCES で断る（firmware の supplicant と競わせない。設計の「EAPOL は捨てる」の送りの側）。
8. FullMAC の scan の profile は全ての channel が `OFFLOADED_DWELL` でないと EINVAL（review-3 の 6 の「channel は OFFLOADED_DWELL だけ」を attach の検査にした）。
9. SoftMAC の `report_frame`・`report_tx_complete` の FullMAC の検査は、attach で固定の `fullmac` を lock 無しで読む（SoftMAC の受けの経路に lock を足さない、前と同じ順の検査を保つ）。

## 確認（2026-10-13 P3、host と build だけ。QEMU・実機は使っていない）

- host 試験 `sh plan/ws206/tests/fullmac-host-test.sh`（出力は `build/ws206-host/fullmac.run.*`、fresh_out）:
  - `asan`（`-fsanitize=address,undefined -fno-sanitize-recover=all`）: 93 checks、`fullmac-host-test: PASS`。engine（不完全な ops、短い passphrase、IEEE 802.11 J.4.2 の PMK（"password"・"IEEE" → f42c6fc5…a12e）、EBUSY、ESTALE・EINVAL の event、重複・順の外、AUTHORIZED と port の 1 回、AUTHORIZED の後に期限なし、fail の最初の理由、loss の後の event、stop の失敗と再試行、期限、期限の後の event、ASSOCIATED を飛ばす AUTHORIZED、port の失敗、connect の失敗と借りの stop、scratch の消去）、attach の mode の検査 6 つ、FullMAC の station（offload の 2 channel の scan と ready の前の結果の拒否、SoftMAC の入口の EOPNOTSUPP、connect で PMK = PBKDF2(passphrase, SSID)・BSS・期限が driver に渡り core に秘密が残らない、ASSOCIATING、port の前の受け EACCES・送り ENETDOWN、ESTALE の event、FOUR_WAY、CONNECTED と carrier・beacon の監視なし、受け・EAPOL の消費・古い generation・短い frame、送りと EAPOL の拒否、link loss の ECONNRESET と disconnect、loss の後の event、30 s の期限の ETIMEDOUT と disconnect、EACCES で disconnect が失敗→回復の EIO でも EACCES→再試行で retire、明示の disconnect、close・detach）、SoftMAC の station の新しい入口の EOPNOTSUPP。
  - `tsan`（`-fsanitize=thread`、`FULLMAC_TEST_THREADS`: spinlock の stub を 1 つの再帰の pthread mutex にし、data の thread が `report_ethernet` と `transmit` を回す間に main が connect → ASSOCIATED → AUTHORIZED → link loss を 200 回）: 95 checks PASS、TSan の報告 0、`ethernet_transmit` が carrier の下りている時に呼ばれた回数 0。
  - `retire`（既存の `plan/ws005/tests/host-wlan-retire.c` を変えずに、変えた core と ASan/UBSan で）: 23 checks、`host-wlan-retire: PASS`（SoftMAC の BUG-157・BUG-158 の振る舞いが変わらない）。その script（`host-wlan-retire.sh`）は中に `rm -rf` があるので使わず、同じ source を上の script から build した。
  - 試験の build で core の source は `-Wall -Wextra -Wno-unused-parameter` で warning 0（host の SIOC* の再定義だけを除く）。
- build:
  - `make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix` → rc 0、`warning:` 0、`arm64 vmunix check: PASS`（`-Werror`）。
  - `make -o <wt>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix` → rc 0、`warning:` 0、`amd64 vmunix check: PASS`。
- `git diff --check` 0。
- 未実施: QEMU（amd64 の起動の回帰、RTL8822BU・AX211 の SoftMAC の WiFi の回帰）と実機。SoftMAC の経路は host 試験と review で同じことを確かめただけで、実際の WiFi の機器では確かめていない。FullMAC の経路を実際の driver で使うのは p004・p005（Pi 4 の実機、T1）。

## 残り・引き継ぎ

- p004・p005（P4）: 上の「driver から見える内部の API」を使う。p005 の errno の表（W7、review-3 の 1）は driver が `report_link_loss` の error で出す（core は error をそのまま `terminal_error` に写し、最初の理由を保つ）。
- T1 への依頼は p008 だけでは出していない。Q1 の判断で、WS の最後（p006）か p004 の QEMU raspi4b の起動の回帰と一緒に、amd64 の起動の回帰と（要れば）SoftMAC の WiFi の回帰を流す案。
- 試験の行き先: `fullmac-host-test` は WS の終わりに回帰に残すか Q1 が決める（残すなら master の Tools に登録を Q1 に依頼）。review-3 の軽微「`plan/ws005/tests/host-wlan-retire.sh` は master の Tools に無い」も同じ判断。
- 方針の記録（`plan/master-design-policy.md` の FullMAC の行）は Q1 が記録済み。
