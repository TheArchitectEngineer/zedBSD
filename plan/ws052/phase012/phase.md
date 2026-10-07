<!-- awesome-plan project=zedbsd record=ws052p012 -->

# ws052-p012: compositor の sleep（契機・lock の 2 frame・中止の理由の表示）

Phase ID: `ws052-p012`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-07 q850、P2: 実装・build（warning 0）・host 試験まで。QEMU・実機は p010・p011 とまとめて T1・5330 の UAT へ）
Phase disposition: normal
Queue: q850（2026-10-07 Q1「ws052-p012 の実装を始めてよい」、p011 の test-wait は妨げない）

## 範囲

設計 [ws052-p007](../phase007/phase.md) 第 4 版の §0・§3〜§5・§1.3、§11 の R4・R5 の口。2026-10-07 ユーザーの後の指示で、**電源ボタンの短押しでは眠らせない**（N5 を置き換え、電源ボタンのメニューは WS182）。眠る契機は sleep button・蓋・無操作（と app の SUSPEND）。

## 実装（2026-10-07）

| 部分 | file |
| --- | --- |
| 規則（server を知らない、host 試験） | 新しい `userland/desktop/wayland/sleep-rules.c`・`.h`: 状態（IDLE → PENDING → WAITING）、無操作の時間（電源・電池、0 は Never）、2 frame と 500 ms の上限、EBUSY の送り直し（3 秒）、延びる間隔（30 秒・2 分・10 分、入力で戻る）、無操作の失敗は新しい入力まで試さない、恒久の UNSUPPORTED、driver の EOPNOTSUPP は 10 分、速い起床 3 回で間隔、利用者でない起床（ac・timer・spurious・other）の後の 60 秒の休み、押下（1 秒）と鍵（500 ms）を捨てる窓、理由の分類（Wi-Fi・disk・USB・display・他の driver の busy・口の無い driver・PCI でない部分・resume の失敗・confirmed・利用者の Wi-Fi・ERROR） |
| server | 新しい `sleep.c`: `kwl_sleep_tick`（`display.c` の `kwl_schedule` の先頭、lock 画面の tick より前）、`kwl_sleep_button`、`kwl_sleep_request`（app）、`kwl_sleep_answer`、`kwl_sleep_waiting`、`kwl_sleep_keys_held_now`、`kwl_sleep_lid_opened`。session は lock してから、lock 画面（蓋は黒）を 2 frame 描いてから `kl_backend_power_action(SUSPEND)`、WAITING の間は frame を出さない（`sleep_hold`、`display.c`）。答えの後は描き直し、蓋の level を backend の `lid` に合わせ（保留した open を実行）、理由を lock・greeter の行と 1 度の system の通知に。半分の時間で画面を消し（入力で点く）、全画面の窓が前の間は無操作を数えない（N7）。zedBSD（`kl_backend_power_outcome` が答える backend）だけ。 |
| 蓋（R5 と N8 の口） | `backend-host.c` の `kwl_lid_follow`（事象と level の両方から）: WAITING の間の open は `POWER cancel` を送って答えの後へ保留（猶予の unlock が眠る前に来ない）、PENDING の間の open は要求を取りやめる。出している出力が外部（名前が `:hdmi:`・`:dp:`、`compose.c` の `kwl_output_lid_matters`）なら蓋を閉じても何もしない（起動時に外部だけの機械、R5）。蓋を閉じた時に外部へ切り替える（R4）は WS113 p004a・p011a の後に足す。それまでは内蔵に出している時に蓋を閉じると、外部が繋がっていても眠る（暫定）。 |
| 電源ボタン | `kwl_backend_power_button`: sleep button は `kwl_sleep_button`、電源ボタンは log だけ（WS182 のメニューの口） |
| 答えの経路 | `handoff.c`: SUSPEND の答えは login・lock 画面の分岐より前に `kwl_sleep_answer` へ |
| app | `system.c`: zedBSD の SUSPEND は `kwl_sleep_request(VIA_APP)`（lock を通る） |
| 画面 | `backend-host.c` の `kwl_screen_off(server, why)`（旧 `lid_screen_off` を public に）、`kwl_lid_screen_restore` は `screen_idle_off` も戻す |
| greeter | `kwl_greeter_say`（UTF-8 の境で切る）、`kwl_greeter_starting`、`kwl_greeter_key` は sleep の前後の鍵を捨てる |
| 設定 | `settings-keys.c` に `power.sleep.ac`（0〜240、既定 30）・`power.sleep.battery`（既定 15）、`settings.c` が `server->sleep_ac_minutes`・`sleep_battery_minutes` に（`KWL PREFERENCES key=power.sleep.ac applied value=N`）。既定は `main.c` |
| backend | `power-zedbsd.c` の `power_actions`: SUSPEND の bit（descriptor があり can_sleep、wheel を問わない、UNSUPPORTED の答えの後は落とす）、`session_gone` の時は 0 |
| 翻訳 | `locale/ja/wayland.tr` に 13 件（`tools/i18n/tr.py update`・`check` 140/140） |
| build | `wayland/Makefile`・`Makefile.linux`・`Makefile.freebsd` に `sleep-rules.c`・`sleep.c` |

## 確認（2026-10-07）

- `sh plan/ws052/tests/run-host-sleep-rules.sh` PASS（plain・ASan/UBSan、約 60 項目）。p011 の `run-host-backend-sleep.sh` 13/0 PASS（power_actions の変更の後）。
- build（warning 0）: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws052-p012 build/ws052-p012/bin/wayland build/ws052-p012/bin/settings`、`make keiland-linux KEILAND_LINUX_BUILD=build/ws052-p012-linux`（rc 0、OpenSSL 以外の warning 0）。FreeBSD は native の build なので未実施。
- style-check: 新規の file は違反 0、変えた既存の file は新しい違反 0。
- 未実施: QEMU（T1。QEMU は `CAN_SLEEP` が 0 なので、sleep button・蓋は今の動き、無操作で半分の時間に画面が消え入力で点くまで。試験の短い時間は `keiland-settings set power.sleep.ac 1` で 30 秒に消える）、実機（5330 の UAT: 蓋・sleep button・無操作で入り、蓋・電源ボタンで戻る、中止の理由の表示、外部の HDMI だけに出している時に蓋を閉じても続く）。

## 残り

- R4（蓋を閉じたら外部へ切り替え、開けたら内蔵へ）: 2026-10-07 に `backend-host.c` の `kwl_lid_follow` に入れた（ws113-p011a の記録）。確認は 5330 の UAT。
- p013（Settings の Power の頁）。
