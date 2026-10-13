<!-- awesome-plan project=zedbsd record=ws206-p005 -->

# ws206-p005: BCM43455 の radio の op（scan の順、非同期の connect と firmware の supplicant、errno の表、data、link loss、restart）

Parent: [WS206](../ws.md)
Status: in-progress（2026-10-13 P3: 実装・host 試験（ASan/UBSan・TSan、新しい scenario radio・radio-smp、誤りの埋め込み 16 個を全て検出）・rpi4 と amd64 の vmunix の build（warning 0）まで済み。clearance は Pi 4 の実機（下の「Pi 4 で見る点」、Q1 が依頼）と T1 の QEMU raspi4b の起動の回帰の後に Q1 が判定）
Phase disposition: normal
Queue: P3 の lane（2026-10-13 Q1 の割当）
前提: [p004](../phase004/phase.md)（bring-up・bus・device、[values.md](../phase004/values.md)）、[p008](../phase008/phase.md)（core の FullMAC の engine と「driver から見える内部の API」）。設計は [p001](../phase001/phase.md) 第 3 版の W5・W7・W12

## 範囲と受け入れ条件

1. station を FullMAC の op の一式で attach する（`scan_channel_start`・`scan_stop`・`disconnect`・`quiesce`・`stop_retry`・`restart`・`fullmac_connect`・`ethernet_transmit`、SoftMAC の op は全て NULL）。scan の profile は全 channel `OFFLOADED_DWELL`（p004 のまま）。
2. scan: 1 channel ずつ escan、core へ **ready → その channel の結果 → complete** の順（ready の前に結果を出さない）。core の期限（ready まで 500 ms、complete まで 1 s）の中で終わる。`scan_stop` は firmware が走らせている escan だけを abort し、その scan の残りを報告しない。
3. connect: `fullmac_connect` は要求を写してすぐ返る（待たない）。command の列は bus の thread が自分で出す。結果は event から `ASSOCIATED`・`AUTHORIZED` と `report_link_loss`（W7 の errno の表）。carrier は core が持つ（driver は触らない）。EAPOL は firmware の supplicant。
4. PMK: driver の写しは firmware に渡した直後に消す。成功・command の失敗・timeout・disconnect・close・restart の全ての経路で、slot・作業の buffer・SDPCM と BCDC の frame・bus の slot が 0。
5. data: 受けた 802.3 frame を `wlan_station_report_ethernet` へ、送りは `ethernet_transmit` → driver の ring（古い generation は捨てる）。
6. link loss と restart: firmware の切断の event は link loss（errno の表）。bus・firmware の失敗は EIO の link loss と scan の EIO、`wlan_station_restart_request` で restart（連続 3 回まで）。
7. p004 の残件を確かめる（PMK の消去の試験、`bus:rxglom`、country の revision −1）。
8. rpi4・amd64 の vmunix の build が warning 0。host 試験（`bcm43455-host-test`・`fullmac-host-test`・`sdio-host-test`）が PASS、新しい scenario を足す（ASan/UBSan・TSan）。

## 実装（2026-10-13 P3）

| file | 内容 |
| --- | --- |
| `src/drivers/wifi/bcm43455/bcm43455-radio.h`・`.c`（新規） | 純粋な encode・decode・分類: chanspec（802.11n と 802.11ac の両方の配置、20 MHz の作りと 20/40/80 MHz の primary の channel の解き）、escan の要求（layout 0 と 2、start と abort）、escan の結果の header と network の記述（`bss_info`）の長さ、`bss_info` → probe response（SSID と DS parameter の要素を記述から作り、firmware の要素から同じ 2 つを除いて写す）、join の要求、supplicant の鍵（PMK を 64 文字の 16 進の text）、escan の status の分類、**接続の event の分類（errno の表）** |
| `src/drivers/wifi/bcm43455/bcm43455-station.c`（新規） | radio の op の表 `bcm43455_radio_operations`、scan と connect の slot、bus の thread の radio の仕事（escan の start、接続の command の列）、event の latch、poll の報告（scan・接続・bus の失敗）、data の受け渡し |
| `src/drivers/wifi/bcm43455/bcm43455-bus.h`・`.c` | bus の hook に `work`（bus の thread が `bcm43455_bus_request_work()` の後に走らせる）、`bcm43455_bus_thread_control()`（work の中から command を出し、答えを待つ間も chip を読む。戻り値は bus の誤り、`outcome` は command の結果: 0・ETIMEDOUT・ECANCELED・firmware の拒否）。direct mode の `direct_control` を共通の `own_request` に分けた（振る舞いは同じ）。待ちの間に thread の停止を見る（ECANCELED） |
| `src/drivers/wifi/bcm43455/bcm43455-device.h`・`bcm43455.c` | device に scan・connection の slot、scan の結果の ring（32）、bus の失敗の latch、`opened`・restart の数、bus の thread の作業の buffer（`work_key`・`work_buffer`）と poll の buffer。ring の frame に接続の generation、`bcm43455_device_ethernet_transmit` に generation の引数（違えば ESTALE、bus の失敗で ENETDOWN）、受けは接続中（JOINING で lost でない）だけ ring へ、送りは今の接続の frame だけ。起動の設定の最後に I/O の版（`WLC_GET_VERSION`、chanspec の配置）と `scan_ver` を読む。`runtime_active` を ring の spinlock の下で書く（radio の op が同じ lock で読む）。quiesce・stop_retry を `bcm43455_device_quiesce`・`bcm43455_device_stop_retry` に、`bcm43455_device_restart` を足した。net の poll は `bcm43455_station_poll` |
| `platform/arm64/vmunix.mk` | `bcm43455-radio.c`・`bcm43455-station.c` |
| `plan/ws206/tests/bcm43455-host-test.c`・`.sh` | 下の「確認」 |

HAL・hal.h・UAPI・WLAN の core（`wlan.c`・`wlan-fullmac.c`）は変えていない。

### 並行と lock（W12 の形のまま、足した物）

- radio の op は全て driver の ring の spinlock（leaf）の下で slot を読み書きし、bus を待たない。待つのは barrier の 2 つだけ: `scan_stop` の escan の abort と `disconnect` の leave（`WLC_DISASSOC`）。どちらも bus の request の slot（`bcm43455_bus_control`、上限 500 ms、超えたら ETIMEDOUT で core が再試行）。
- **順の保証**: bus の thread は 1 回の loop で `service` → radio の work → `send_pending`（slot の要求）の順に回すので、slot に入れた abort・leave は、その時に走っている work（escan の start・接続の command の列）の**後**に firmware へ届く。disconnect は先に `cancel` を立てるので、work は次の command を出さずに止まる（slow bus の試験で key と join が出ないことを確かめた）。
- bus の thread は work の中でも mutex を取らない。work の command の待ちは thread 自身が chip を読むので閉じる。停止の要求で ECANCELED。
- 報告（`report_*`）は全て poll（network worker）から。bus の thread の hook は latch を立てて `net_device_schedule_poll` するだけ。

### scan

- `scan_channel_start`: chanspec が作れない channel は EINVAL、bus が動いていなければ ENETDOWN、別の generation の scan や終わっていない channel があれば EBUSY。slot に置いて work を頼みすぐ返す。
- work: escan の id（sync id、0 を飛ばす counter）を付けて `escan`（action 1、channel 1 つ、SSID なし、BSSID broadcast、時間は firmware の既定 −1）を出す。答えが成功なら ready の latch、失敗なら error の latch（EIO、timeout は ETIMEDOUT）。
- 能動・受動の表（driver の私的な表、E6 の世界共通の domain）: 2.4 GHz の 1〜11 と 5 GHz の 36〜48 は probe する、他は聞くだけ（WHD の `WHD_COUNTRY_WORLD_WIDE_XX` の注記「passive Ch12-14」と合う）。
- event `ESCAN_RESULT`: sync id が違えば捨てる。status 8（partial）は network を probe response にして ring へ、0 は complete、9・10（別の scan・join に取られた）は EBUSY、それ以外（頼んでいない abort を含む）は EIO。
- poll: ready → ring の今の step の network（古い step・generation の物は捨てる。ready の前なら残す）→ ring が空になってから complete → error。complete か authorized が来たら restart の数を 0 に戻す。
- `scan_stop`: 起動中・走行中・停止中、または firmware が受け付けて終わっていない escan があれば `escan` の abort（action 3、WHD `whd_wifi_stop_scan()`）を出す。終わった channel・始まっていない channel は忘れるだけ。firmware の拒否・bus の停止は 0、答えが無ければ ETIMEDOUT。

### 接続（firmware の supplicant）

- `fullmac_connect`: request が NULL・generation 0 は EINVAL、`idsup` が無ければ EOPNOTSUPP、channel が作れなければ EINVAL、bus が動いていなければ ENETDOWN、slot が使用中なら EBUSY。slot に generation・BSS・PMK を写し work を頼んで**すぐ 0**。
- work が slot を取る時に PMK を鍵の message（`work_key`）にして slot の PMK を消す。command の列（WHD `whd_wifi_prepare_join()` の順）:
  1. `WLC_SET_INFRA` 1、`WLC_SET_AUTH` 0（open）、`WLC_SET_WSEC` 4（AES）、`WLC_SET_WPA_AUTH` 0x80（WPA2-PSK）
  2. `bsscfg:sup_wpa` {0, 1}（firmware の supplicant）
  3. `bsscfg:sup_wpa2_eapver` {0, −1}・`bsscfg:sup_wpa_tmo` {0, 3000 ms}（無い firmware でも続ける。WHD も続ける）
  4. `WLC_SET_WSEC_PMK`（長さ 64・flag 1・PMK の 16 進）。出した直後に `work_key` を消す
  5. `WLC_SET_SSID`（SSID、BSSID、chanspec 1 つ。WHD `whd_wifi_join_specific()` の ioctl の形）
  各 command の前に `cancel` を見る。途中の失敗は残りを出さず、`WLC_DISASSOC` で戻し、link loss（timeout は ETIMEDOUT、firmware の拒否は EIO）。
- event の latch（generation ごと、ring でない）: `associated`・`authorized`・`lost`（最初の理由）。poll が ASSOCIATED → AUTHORIZED → link loss の順に報告。
- `disconnect(generation)`: 違う generation・空の slot は 0。work が取る前なら忘れるだけ。取った後は `cancel` を立てて `WLC_DISASSOC`（slot 経由、work の後）、答えか bus の停止・拒否で slot を消す（PMK も）。冪等。

### errno の表（`bcm43455_connect_classify`、host 試験の `check_connect_table` と同じ）

firmware の数は WHD の `whd_events_int.h`（WHD は supplicant の状態に 256、理由に 512 を足して enum にする: `whd_cdc_bdc.c:867-870`。driver は firmware の生の数を使う）。

| event（status・reason） | 接続の進み | 結果 |
| --- | --- | --- |
| SET_SSID（0）status 0、LINK（16）の flag bit 0（up） | join の前 | ASSOCIATED |
| PSK_SUP（46）status 6（KEYED） | join の後、鍵の前 | AUTHORIZED |
| SET_SSID status 3（no networks）・2（timeout）・5（no ack） | join の前 | ETIMEDOUT |
| SET_SSID のその他の失敗（1 など） | join の前 | ECONNREFUSED |
| AUTH（3）・ASSOC（7）status 2・5 | join の前 | ETIMEDOUT |
| AUTH・ASSOC のその他の失敗 | join の前 | ECONNREFUSED |
| PSK_SUP status 4（M1 を待つ）・10（G1 を待つ）、reason 15（timeout） | join の後、鍵の前 | ETIMEDOUT |
| **PSK_SUP status 8（M3 を待つ）、reason は何でも（15 timeout、14 deauth）** | join の後、鍵の前 | **EACCES**（M2 を出した後に M3 が来ない = 鍵の誤り） |
| PSK_SUP のその他（status 4 で reason 14、5・9・11 など） | join の後、鍵の前 | EPROTO |
| DEAUTH_IND（6）・DISASSOC_IND（12）の 802.11 reason 15（4-way の timeout） | join の後、鍵の前 | EACCES |
| DEAUTH（5、firmware の自分の deauth）、DEAUTH_IND・DISASSOC_IND のその他、LINK down | join の後 | ECONNRESET |
| 上の全て | join の前の deauth・LINK down、鍵の後の PSK_SUP | 変えない |
| bus・firmware の失敗（trap・halt） | 何時でも | EIO（restart も頼む） |
| 30 s の全体の期限 | 鍵の前 | ETIMEDOUT（core の FullMAC の engine） |

- 証拠の更新: 報告前の ECONNRESET の loss に、後から EACCES の証拠（PSK_SUP の M3 待ち）が来たら EACCES に替える（AP が先に deauth し、firmware の supplicant が後で失敗を言う順の時）。実機での event の順は Pi で見る（下）。

### link loss・restart

- bus の thread が誤りで止まると hook が `failure_pending` を立てる（以後 frame と radio の要求は ENETDOWN）。poll が 1 度だけ、接続があれば EIO の link loss、scan があれば EIO の scan error、interface が開いていて restart の数が 3 未満なら `wlan_station_restart_request`。
- `restart`（retirement の thread）: 要求が無い・閉じた device は 0。close と同じ手順（station の close → quiesce の下で chip を止める → close）、閉じられていなければ start（firmware の load から）と station の open。AX211 の restart と同じ形。利用者の open で数を 0 に戻す。

## 判断（設計の範囲の中、P3）

1. **connect の command の列は bus の thread の work hook で出す**（設計 W7 の「bus の thread が ioctl の列を自分で出す」）。p004 の bus は slot の要求を他の thread が待つ形なので、thread 自身のための `bcm43455_bus_thread_control()` を足した。別の thread は作らない。
2. barrier の abort と leave は slot 経由（`bcm43455_bus_control`）にした。thread の loop の順で work の後に届くので、明示の待ちの waitq が要らない。
3. escan の sync id は generation と step から作らず counter（0 を飛ばす）。古い scan の結果と取り違えない。
4. scan の能動・受動は上の世界共通の表。firmware の CLM が受動の channel を守るかを確かめていないので、driver の側でも probe しない。
5. join は `WLC_SET_SSID` と assoc の params（BSSID と chanspec 1 つ）。`join` の iovar（WHD の第 1 の手）は使わない（43455 の firmware にあるか未確認、ioctl は bwfm・WHD の両方にある）。
6. `bsscfg:sup_wpa2_eapver`・`bsscfg:sup_wpa_tmo` の失敗は無視（WHD と同じ）。`sup_wpa` の失敗は接続を止める（firmware の supplicant が無ければ接続できない）。
7. PSK_SUP の G1 の timeout は ETIMEDOUT（WHD の注記「edge of the cell」）。設計 W7 の表は「その他 → EPROTO」だったが、timeout は鍵の誤りの証拠ではなく、再試行してよい物として ETIMEDOUT に分けた。
8. join の前の deauth・LINK down は無視し、SET_SSID の結果で決める。鍵の後の PSK_SUP（rekey）は無視（致命なら firmware が deauth・LINK down を出す）。
9. 受けの data は JOINING で lost でない接続だけ ring へ（それ以外は hook で数えて捨てる）。core の `report_ethernet` が port の開閉で更に門をする。
10. chanspec の配置は `WLC_GET_VERSION`（1 = 802.11n、2 = 802.11ac）で決める。読めなければ 802.11ac（43455 の firmware の 7.45 は ac と推定、Pi の log で確かめる）。`scan_ver` が 2 なら layout 2、他は layout 0。layout 2 の `length` の欄は bwfm `bwfm_scan_v2()` と同じく要求の全体の長さ（公開の寛容な出典は bwfm だけ）。
11. restart の close が失敗した時は restart を再び頼まない（利用者の Wi-Fi の切と入で回復）。連続の restart の上限 3。

## p004 の残件の確認

| 残件 | 結果 |
| --- | --- |
| PMK の消去の試験 | 済み。radio の scenario で、成功・firmware の key の拒否（EIO）・join の無応答（ETIMEDOUT、join を待つ間に `work_key` が既に 0）・誤った鍵・no network・M1/M3 の timeout・disconnect（取る前・command の途中）・bus の失敗・close の後に、connection の slot の PMK、`work_key`、`work_buffer`、bus の `tx_frame`、bus の slot の request・answer が全て 0、`direct.answer` が NULL（`check_no_secret`） |
| `bus:rxglom` | 公開の寛容な出典に無いことを確かめた（WHD tree bd99da7b は `bus:txglom` だけ（`whd_wlioctl.h:1088`）、OpenBSD bwfm は設定せず、glom の frame を解く（`if_bwfm_sdio.c:1294-1301`・`bwfm_sdio_rx_glom()`））。p004 のまま（送って、断られても続け、glom の frame は 1 度 log して捨てる）。**Pi で `glommed frame dropped` の log が出たら、glom の frame を解く口を足す**（値と手順は bwfm、別の Phase） |
| country の revision −1 | 公開の寛容な出典で −1 は見つからなかった。WHD は世界共通を `WHD_COUNTRY_WORLD_WIDE_XX = MK_CNTRY('X','X',0)`（`whd_types.h:859`、「passive Ch12-14」）と rev 0 で定義する。p004 の順（−1、断られたら 0、読み戻しを log）は変えていない（T1・実機の結果待ちの code を変えないため）。**推し（Q1 の判断）**: 出典のある rev 0 を先にする。Pi の log `bcm43455: country XX revision <n>` で −1 が受け付けられた時の rev を見てから決めてもよい |

## 値の出典（p005 で足した物。p004 の [values.md](../phase004/values.md) と同じ扱い、字面は写さず値だけ）

上流の取得（worktree の `build/tmp/p005-upstream/`、tree・git に入れていない）: OpenBSD src commit `28854fb076ac71e356cef81d70cff1c80a9d6bc3`（2026-10-13）の `sys/dev/ic/bwfm.c` 1.113・`bwfmreg.h` 1.26・`bwfmvar.h` 1.31・`sys/dev/sdmmc/if_bwfm_sdio.c` 1.44・`if_bwfm_sdio.h` 1.3（ISC）、Infineon wifi-host-driver commit `bd99da7b005c97500d7c697f544b36fabdaa3f5e` の `WHD/COMPONENT_WIFI6/` の `inc/whd_events.h`・`inc/whd_types.h`・`src/include/whd_wlioctl.h`・`src/include/whd_events_int.h`・`src/include/whd_types_int.h`・`src/whd_events.c`・`src/whd_wifi.c`・`src/whd_wifi_api.c`・`src/whd_cdc_bdc.c`・`src/whd_utils.c`（Apache-2.0）。Linux brcmfmac（GPL）は取得も比較もしていない。

| 値 | driver の名前 | 出典 |
| --- | --- | --- |
| chanspec: channel 0〜7 bit。802.11n: 主の part 8〜9（1 lower・2 upper・3 none）、幅 10〜11（2 = 20・3 = 40）、band 12〜13（1 = 5 GHz・2 = 2.4 GHz）。802.11ac: part 8〜10、幅 11〜13（2 = 20・3 = 40・4 = 80）、band 14〜15（0 = 2.4・3 = 5 GHz） | `D11N_*`・`D11AC_*` | bwfm `bwfmreg.h:194-234` |
| 40/80 MHz の主の channel: 40 は中心 −2 + 4×part、80 は中心 −6 + 4×part（802.11n の 40 は lower −2・upper +2） | `control_channel_d11n/_d11ac` | bwfm `bwfm.c:1903-1983`（`bwfm_spec2chan_d11n/_d11ac()`） |
| I/O の版 `WLC_GET_VERSION` 1、1 = 802.11n・2 = 802.11ac | `BCM43455_COMMAND_IO_VERSION`・`BCM43455_CHANSPEC_LAYOUT_*` | bwfm `bwfmreg.h:247`・`bwfm.c:256-260`。WHD `whd_wlioctl.h:1304` |
| `scan_ver`（無ければ 0） | `read_layouts` | bwfm `bwfm.c:277-278` |
| escan: 版 1（layout 0）・2（layout 2）、action 1 start・3 abort、sync id、params layout 0（SSID 36・BSSID 6・bss type 1（2 = any）・scan type 1（0 active・1 passive）・nprobes・active・passive・home の時間（−1 = 既定）・channel の数（下位 16 bit）・chanspec の列）、layout 2（版・長さ・SSID・BSSID・bss type・pad・scan type 4 byte・…） | `bcm43455_escan_encode` | bwfm `bwfmreg.h:509-575`・`bwfm.c:2138-2255`。WHD `whd_wlioctl.h:496-554`（`WL_SCAN_ACTION_*`・`ESCAN_REQ_VERSION`）、abort は WHD `whd_wifi_api.c:3394-3419` |
| escan の結果: buflen・版・sync id・数・記述の列。記述（`bss_info` 版 109）: 長さ 4、BSSID 8、beacon 14、capability 16、SSID 長 18・SSID 19、chanspec 72、RSSI 78（int16）、`ctl_ch` 88、要素の offset 116（16 bit）・長さ 120 | `bcm43455_escan_result_decode`・`bcm43455_bss_*` | bwfm `bwfmreg.h:344-375`・`:577-583`、`bwfm.c:2611-2665`（記述の列の歩き方）。WHD `whd_wlioctl.h:493`（版 109）・`:556-562` |
| escan の status: 0 success、4 abort、8 partial、9 newscan、10 newassoc | `bcm43455_scan_classify` | bwfm `bwfmreg.h:736-751`。WHD `whd_events.h:61-77` |
| 記述 → beacon・probe response の形に組み直す考え方 | `bcm43455_bss_to_frame` | bwfm `bwfm.c:2717-2763`（`bwfm_scan_node()`、手順の要点だけ。zedBSD は SSID・DS の要素を自分で作る） |
| 接続の command: SET_INFRA 20、SET_AUTH 22、SET_SSID 26、DISASSOC 52、SET_WSEC 134、SET_WPA_AUTH 165、SET_WSEC_PMK 268 | `BCM43455_COMMAND_*` | WHD `whd_wlioctl.h:1317-1544`。bwfm `bwfmreg.h:253-312` |
| open 0、AES 4、WPA2-PSK 0x80 | `BCM43455_AUTHENTICATION_OPEN`・`_CIPHER_AES`・`_KEY_MANAGEMENT_WPA2_PSK` | WHD `whd_wlioctl.h:4273`・`whd_types.h:65`・`whd_wlioctl.h:831`。bwfm `bwfmreg.h:167-191` |
| `bsscfg:sup_wpa`・`sup_wpa2_eapver`（−1）・`sup_wpa_tmo`（3000 ms）、bsscfg の index と値の 8 byte | `connect_commands` | WHD `whd_wlioctl.h:1077`・`:1103`・`:1141`、`whd_wifi_api.c:79`・`:716-735`・`:1817-1852` |
| 鍵の message: 長さ 16 bit・flag 16 bit（1 = passphrase の形）・key 65 byte。PMK は 64 文字の 16 進で flag 1 | `bcm43455_key_encode` | WHD `whd_wlioctl.h:781-785`、`whd_wifi_api.c:1140-1176`（`whd_wifi_sae_password()`）。bwfm `bwfmreg.h:649-654` |
| join の要求: SSID（長さ 4 + 32）、assoc（BSSID 6・数 2・chanspec の数 4・chanspec） | `bcm43455_join_encode` | WHD `whd_wlioctl.h:4239-4256`、`whd_wifi_api.c:2343-2386`。bwfm `bwfmreg.h:585-611` |
| 接続の順: WSEC → WPA_AUTH の前に INFRA・AUTH、`sup_wpa`・eapver・tmo・PMK、join | `connect_commands` | WHD `whd_wifi_api.c:1717-2077`（`whd_wifi_prepare_join()`）・`:2433-2490` |
| LINK の flag bit 0 = link up | `LINK_UP` | WHD `whd_wifi_api.c:58`・`:1421-1430` |
| supplicant の状態（firmware の数）: 4 M1 を待つ、5 M2 の準備、6 keyed、8 M3 を待つ、9 M4 の準備、10 G1 を待つ、11 G2 の準備。理由: 14 deauth、15 4-way の timeout | `SUPPLICANT_*` | WHD `whd_events_int.h:224-236`・`:329-344`、offset は `whd_cdc_bdc.c:867-870` |
| M1・M3・G1 の timeout の意味（M3 の timeout は passphrase の誤りの印） | `classify_handshake` | WHD `whd_wifi_api.c:1360-1400`（`whd_wifi_join_events_handler()`） |
| 802.11 の reason 15（4-way handshake timeout） | `REASON_FOUR_WAY_TIMEOUT` | IEEE 802.11 の reason code 15（4-way handshake timeout） |
| 世界共通 `XX` rev 0、「passive Ch12-14」 | （確認だけ） | WHD `whd_types.h:859` |

## 由来と license の確認（WS218 の方針）

道具: P1 の WS218 の `csim.py`・`cruns.py`・`ccom.py`・`cid.py`（scratchpad）を worktree の `build/tmp/p005-tools/` に写し、接頭辞の表だけ `bcm43455_`・`bwfm_`・`bwfm_sdio_`・`whd_`・`whd_bus_`・`whd_wifi_`・`wlc_`・`wl_` に替えて `python3 -I` で実行。使い方は [ws218-p001](../../ws218/phase001/phase.md) の「方法」。

| 比較（p005 の新しい 3 file: radio.c・radio.h・station.c、約 8700 token） | bwfm | WHD | 対照 RTL8822B（2.3 万 token） |
| --- | --- | --- | --- |
| ident k=8 | 0.15%（13） | 0.17%（15） | bwfm 0.03%・WHD 0.18% |
| ident k=12 | 0.01%（1） | 0.00% | — |
| norm k=20 MIND=10 | 0.05%（4） | 0.08%（7） | bwfm 0.02%・WHD 0.05% |
| comment の 6 語・5 語の一致 | 0・0 | 0・0 | — |

- ident k=8 で 8 token 以上続く一致は全部を目で見た: `; for ( i = 0 ; i <`、`) ; break ; default : break ; } }`、`) return 0 ; return 1 ; }`、`, NULL , NULL , NULL , NULL ,`（op の表の NULL の並び）、`if ( channel == NULL ) return`、`status ) { switch ( status ) {` — C の定型だけ。写しは無い。
- 名前（`cid.py`）: 初めは macro 2（`IO_TYPE_D11N`・`IO_TYPE_D11AC`、bwfm と同じ）と内部の macro 3（`BSS_TYPE_ANY`・`SCAN_TYPE_ACTIVE`・`SCAN_TYPE_PASSIVE`、WHD の enum と同じ）が同じだった。`BCM43455_CHANSPEC_LAYOUT_N`・`_AC`、`NETWORK_KIND_ANY`・`SCAN_PROBING`・`SCAN_LISTENING` に付け直した後、p005 の 3 file の macro 129・enum 定数 9・tag 3・field 2・function 47 で同じ名前は 0。
- driver の全体（21 file、10371 行）の再測定: bwfm ident k=8 0.19%・k=12 0.03%・norm k=20 0.09%、WHD 0.13%・0.00%・0.09%、comment の 6 語 0。k=12 の 12 token 以上の一致は p004 の `bcm43455-chip.c:60`・`:844`・`:901` の core の reset の関数の引数の並び `struct …core * core , uint32_t prereset , uint32_t reset , uint32_t postreset )`（bwfm `bwfm.c:1062`・`:1101` と引数の名前まで同じ）。p004 の file なので変えず、**p007（値の出典の監査）で引数の名前の付け直しを Q1 に提案**する。名前の一致は function `set_country`（WHD の `WLC_SET_COUNTRY` と接尾辞が同じ、一般の語）と p004 の時と同じ一般の struct の tag・field。

## 確認（2026-10-13 P3、host と build だけ。QEMU・実機は使っていない）

```sh
sh plan/ws206/tests/bcm43455-host-test.sh <worktree>/build/ws206-p005/bcm43455
sh plan/ws206/tests/fullmac-host-test.sh
sh plan/ws206/tests/sdio-host-test.sh <worktree>/build/ws206-p005/sdio /home/awe/zedBSD-claude1/vendor/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb
make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/vmunix
make -j16 -o <worktree>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/vmunix
```

- `bcm43455-host-test.sh` → `unit` PASS、`smp` PASS、**`radio` PASS、`radio-smp` PASS**（新しい 2 つ）、`bcm43455-host-test: PASS`。
  - unit に足した物: `check_radio_codecs`（chanspec の作りと解き（20/40/80、n と ac、範囲の外）、escan の layout 0・2 の start と abort の byte の位置、ENOSPC・EINVAL、記述 → probe response を **core の本物の `wlan_frame_parse_bss`（`src/kern/net/wifi/wlan-frame.c` を link）** で解いて SSID・BSSID・channel・beacon・capability・RSSI・WPA2|PSK|CCMP|PRIVACY、`ctl_ch` が勝つこと、壊れた記述の EPROTO・EMSGSIZE、escan の結果の header と記述の長さ、join と鍵の message の byte、escan の status）、`check_connect_table`（上の errno の表の 39 行）。probe の要求の数を 14 → 16（I/O の版・`scan_ver`）、runtime の op の検査を FullMAC の一式に、接続の無い時の送り ESTALE・受けを ring の前で捨てることに直した。smp は接続 1 を JOINING にして data を流す。
  - radio（ASan/UBSan、chip の model の firmware が escan・join・supplicant の event を出す）: scan の ready → 2 network（core の parser で WPA2-PSK CCMP）→ complete、**firmware が答えの前に network と終わりを出す時、答えまで何も報告せず、答えの後に同じ順**、受動の channel 13（scan type 1、chanspec 0x100d）、別の escan の結果を捨てる、EBUSY・EINVAL、終わった channel は abort しない・走っている物は abort する、firmware の拒否で ready なしの EIO、firmware の busy で ready の後の EBUSY、abort の後の遅い結果を報告しない、空の・別の generation の stop。接続: command の 9 つの順と中身（鍵の 16 進、join の SSID・BSSID・chanspec 0x1006）、ASSOCIATED → AUTHORIZED、data の送り（ESTALE の古い generation）と受け、AP の deauth で ECONNRESET の後は data を取らず送りも捨てる、disconnect の leave 1 回と冪等、**誤った鍵（AP の deauth → supplicant の M3 待ち）で EACCES**、no network ETIMEDOUT、M1 timeout ETIMEDOUT、M3 timeout EACCES、key の拒否で EIO と leave と join なし、join の無応答で ETIMEDOUT、**bus を 1 frame 40 ms に遅くしても `fullmac_connect` が 20 ms 未満で返る**、command の途中の disconnect で key・join が出ず最後が leave で報告なし、work が取る前の disconnect で leave なし、idsup なしで EOPNOTSUPP。restart: firmware の halt で EIO の link loss と restart の要求、restart で firmware の再 load と station の再 open、続けて 3 回まで、4 回目は要求しない、利用者の close と open で数が 0。各経路の後に `check_no_secret`。
  - radio-smp（TSan、1.5 s）: 接続と切断・scan の start と stop（半分は firmware が止めず abort を要する）・poll・送り・firmware の data と event を並べる。453 join・666 escan・333 abort・453 leave・3019 報告・268 送り、TSan の報告 0、seq の誤り 0、close の後に秘密 0。
  - **試験が誤りを捕まえることの確かめ**（scratchpad の写しの tree に 1 つずつ誤りを入れて同じ script、tree は変えていない）: ready の前に結果を出す、鍵の message を消さない（2 箇所）、M3 待ちを EPROTO、disconnect で cancel を立てない、古い generation の送りを捨てない、sync id を見ない、EACCES への更新をしない、restart を頼まない、走っている scan を abort しない、取る前の disconnect で PMK を消さない、slot の PMK を消さない、ready の前に complete を出す、5 GHz の band の bit を変える、key の前に join を出す、bus の失敗の link loss を出さない → **16 個とも FAIL**（初めの 3 つ（ready の前の結果、鍵を送った直後の消去、complete の順）を見逃したので、firmware が答えを止める model と、join を待つ間の `work_key` の検査を足して捕まえた）。
- `fullmac-host-test.sh` → asan・tsan PASS、既存の SoftMAC の `host-wlan-retire` PASS（core は変えていない）。
- `sdio-host-test.sh` → unit・stop・smp・nopackage・refused PASS（DTB は main の checkout の `vendor/`、worktree には無い）。
- rpi4 の vmunix → rc 0、warning 0、`arm64 vmunix check: PASS`（`build/arm64/vmunix` SHA-256 `bd3341c63f439753a97d09962736b7a7e70c8a8252dd9d0a419621406108837f`、`strings | grep -c bcm43455:` = 73）。kernel の build は `bcm43455-radio.c`・`bcm43455-station.c`・`bcm43455.c` を `-Wall -Wextra -Werror`。amd64 の vmunix → rc 0、warning 0、`amd64 vmunix check: PASS`。
- `git diff --check` → 空。
- host 試験の compile: `bcm43455-radio.c` は `-Wall -Wextra -Werror`。`bcm43455-station.c`・`wlan-frame.c` は network の header と host の SIOC の再定義のため `-w`（p004 の `bcm43455.c` と同じ扱い。kernel の build で warning 0）。

## Pi 4 で見る点（Q1 がユーザーに依頼、試験の AP は main の checkout の `.wifi`、P3 は読んでいない）

dmesg: `dmesg | grep -E 'rpi4-(wifi|sdio)|bcm43455'`。p004 の「Pi 4 で見る dmesg の行」に加えて:

1. 起動: `bcm43455: channel layout <2>, scan layout <0>`（2 = 802.11ac。違えば記録）、`bcm43455: country XX revision <n>`（−1 か 0 か、上の country の判断の材料）、`receive glomming not turned off` の有無。
2. **scan**: `ifconfig wlan0 up`（または Settings の Wi-Fi を入れる）→ `wifi search`・`wifi list` に周りの AP が出る（2.4 GHz と、5 GHz の AP があればその channel）。SSID・channel・RSSI・security（WPA2）が正しいか。`bcm43455: scan result on channel … not taken` の log の有無、`glommed frame dropped` の有無（出たら glom を解く口が要る）。
3. **WPA2-PSK（AES）の接続**: `.wifi` の AP に `/sbin/wifi`（または networkd・Settings）で接続 → dmesg に `bcm43455: event 0/16/46 …` の行（最大 16）、`wifi` の status が CONNECTED。
4. **DHCP**: `ifconfig wlan0` に IPv4 の address、`route` に既定の経路。
5. **ping**: AP の gateway と外の host へ `ping`（10 回、損失 0 が目安）。大きめの転送（数 MB の取得）で `glommed frame dropped` が出ないか。
6. 切断と再接続: `wifi disconnect` → `wifi connect` で戻る。AP の電源を切る・遠ざけると `bcm43455: connection lost (104)`（ECONNRESET）。
7. **誤った passphrase**: `bcm43455: connection lost (13)`（EACCES）になり networkd が再試行を止める。ならずに 104・110・71 の時は、その時の event の順（dmesg の `event … status … reason …`）を記録する（上の「証拠の更新」の材料）。
8. Wi-Fi の切と入（`ifconfig wlan0 down`・`up`）、Pi の起動（SD・Ethernet・USB）が壊れないこと。

## T1 への依頼（Q1 へ、まとめて流す時に）

- image: agent/p3 の commit（下）で、Q1 の Pi 4 の image の手順（`bcm43455-firmware` の package を含む config）。QEMU raspi4b の `plan/tools/boot-test.sh` で login prompt（WiFi は `no onboard Wi-Fi to bring up (19)` で何もしない、p004 と同じ）。p004 の QEMU の回帰と同じ 1 回で足りる。
- amd64 の起動の回帰は p008 と同じ（WLAN の core は p005 で変えていない）。

## 未実施・残件

- Pi 4 の実機（上の 1〜8）と QEMU raspi4b の起動の回帰（T1）。どちらも未実施。
- country の rev（−1 と 0 の順）は Q1 の判断（推しは出典のある 0 を先）。
- glom の frame の解き: Pi で log が出たら別の Phase。
- p007 で: p004 の `bcm43455-chip.c` の core の reset の引数の名前（bwfm と同じ並び）の付け直し、規約の全文の見直し（この Phase は guard の if の comment などを自分で見直したが、全文の review は p007）。
- WPA3・PMF・省電力・roaming・glom・50 MHz・DMA は範囲の外（WS の完了の条件のまま）。
- 試験の file `plan/ws206/tests/bcm43455-host-test.*` は p006 まで使う。WS の終わりに Master の Tools に登録するかを Q1 が判断。

## 削除の依頼（Q1 へ）

- `/home/awe/zedBSD-worktrees/p3/build/tmp/p005-upstream/`（bwfm・WHD の取得物）、`/home/awe/zedBSD-worktrees/p3/build/tmp/p005-tools/`（写した比較の道具と出力）
- `/home/awe/zedBSD-worktrees/p3/build/ws206-p005/`（host 試験の出力）、`/home/awe/zedBSD-worktrees/p3/build/ws206-host/fullmac.run.*`（今回の fullmac の出力）、`/home/awe/zedBSD-worktrees/p3/build/tmp/p005-*.log`
- scratchpad の `p005/`（試験の断片、誤りの埋め込みの tree の写し `mut-base/` と出力 `mut-out/`、`mutate.py`）

## 追記（2026-10-13 P3、Q1 の依頼の 2 つ、main 1f41809ad を merge した後）

1. **country の revision の順**（Q1 の判断）: 出典のある rev 0（WHD `whd_types.h:859` の `WHD_COUNTRY_WORLD_WIDE_XX`）を先に設定し、firmware が断ったら（EIO・EOPNOTSUPP）rev −1（firmware に任せる）で設定し直す。p004 の順（−1 が先）を置き換えた。`bcm43455.c` の `COUNTRY_REVISION_WORLD`（0）と `COUNTRY_REVISION_ANY`（−1）、log の行は `bcm43455: country XX revision 0 refused (<errno>); trying the firmware's revision`（読み戻しの行 `bcm43455: country XX revision <n> (code XX)` は同じ）。host 試験の期待: probe の最初の `country` の payload が `XX\0\0` + rev 0 + `XX\0\0`、model が rev 0 を断ると 2 つ目は rev −1 で、要求の数は 17。p004 の [phase.md](../phase004/phase.md) の設計からの違いの行・失敗の時の log の行、[values.md](../phase004/values.md) の country の行に注記した。
2. **core の reset の関数の引数の名前**（Q1 の許可で p004 の file）: `bcm43455-chip.c` の `hold_core_in_reset()`・`restart_core()` の `prereset`・`reset`・`postreset`（bwfm `bwfm_chip_ai_disable()`・`bwfm_chip_ai_reset()` と同じ並びと名前）を `control_running`・`control_held`・`control_released` に、`halt_cores()` の局所変数 `prereset` を `halt_kept` に替えた。動作は同じ（名前だけ）。

確認:
- rpi4 の vmunix: rc 0、warning 0、`arm64 vmunix check: PASS`（SHA-256 `ec135bef59b033e8f2399bc7ac4e3133daac74a32d309fb733a1ac85b78db099`）。amd64 の vmunix: rc 0、warning 0、`amd64 vmunix check: PASS`。
- `bcm43455-host-test.sh`: unit・smp・radio・radio-smp PASS（radio-smp: 453 join・667 escan・333 abort、TSan の報告 0）。`git diff --check` 空。
- bwfm（同じ commit 28854fb0 を worktree の `build/tmp/p005b-upstream/` に取り直した）と driver の全体の ident k=12: 0.03% → 0.00%（1 gram）。12 token 以上の一致は `bcm43455-station.c:768` の `0 ) return ; for ( i = 0 ; i <`（C の定型）だけになり、chip.c の 3 箇所は消えた。
- QEMU・実機は未実施（T1 の起動の回帰は Q1 がまとめて依頼）。

削除の依頼（Q1 へ）: `/home/awe/zedBSD-worktrees/p3/build/tmp/p005b-upstream/`、`/home/awe/zedBSD-worktrees/p3/build/tmp/p005b-tools/`、`/home/awe/zedBSD-worktrees/p3/build/tmp/p005b-*.log`、`/home/awe/zedBSD-worktrees/p3/build/ws206-p005b/`。
