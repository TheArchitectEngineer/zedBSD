<!-- awesome-plan project=zedbsd record=ws052p010 -->

# ws052-p010: networkd の SLEEP_PREPARE・SLEEP_END

Phase ID: `ws052-p010`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-07 P2: 実装・build・host 試験まで。QEMU は sessiond の client（p011）の後に T1 へまとめて）
Phase disposition: normal
Queue: 2026-10-07 Q1 の ACK「p010（networkd の SLEEP_PREPARE・SLEEP_END）は N に依らないので着手してよい」

## 範囲

設計 [ws052-p007 §2](../phase007/phase.md)（第 4 版）の networkd の部分。sleep の前に sessiond（root）が SLEEP_PREPARE を頼み、networkd が Wi-Fi の方針を記録して接続を退かせ radio を止め（方針は書き換えない）、sleep の後の SLEEP_END で記録した方針から再開する。client（sessiond）は p011。

## 実装（2026-10-07）

- `userland/base/net/protocol.h`: `NETWORKD_OP_SLEEP_PREPARE = 80`・`NETWORKD_OP_SLEEP_END = 81`（64 は SUBSCRIBE）、`NETWORKD_SLEEP_PREPARE_SECONDS`（cleanup 10 + radio の 3 回の helper 30 = 40 秒、sessiond の待ちの上限の基）。
- 新しい `userland/base/networkd/sleep-state.c`・`.h`: 記録（asleep・記録した state・自分で終わる時刻）と規則。`networkd_sleep_begin`（RETIRING は retire の行き先を記録、2 回目は記録を保ち終わりだけ動かす）、`networkd_sleep_end`（記録した state → 再開の種類: DISABLED は何もしない、MANUAL_DISCONNECTED は radio を up して接続しない、AUTO_SEARCHING・CONNECTING・CONNECTED・RECONNECTING は自動の探索）、`networkd_sleep_due`・`networkd_sleep_poll_timeout`（10 分の安全）、`networkd_sleep_admits`（眠っている間は WIFI_ENABLE・DISABLE・CONNECT・DISCONNECT・CONFIRMED_ARM を EBUSY、他は受ける）。
- 新しい `managed-wlan-state.h`: 方針の state の enum を `managed-wlan.h` から分けた（sleep-state と host 試験が network の header 無しで使う）。
- `managed-wlan.c`: `networkd_managed_wlan_resume`（owner の居る idle の方針を別の idle の state に。接続中は EBUSY、無効・idle でない state は EINVAL）。
- `networkd/main.c`: `operation_name` に SLEEP_PREPARE・SLEEP_END（member の一覧には入れない = root だけ）。`dispatch_request` で SLEEP の要求を `handle_sleep_request` へ、眠っている間の拒否（"asleep"）。PREPARE: confirmed の transaction の最中は EBUSY、未処理の再開があれば先に、記録 → `sleep_radios_off`（owner が居れば `retire_managed_connection(MANUAL_DISCONNECTED, 1)`、radio の列挙、`stop_wlan_radios(…, 1)`）、失敗なら眠りを終えて方針を戻し ERROR。自動の仕事の最中の PREPARE は `interrupts_background_work` で仕事を中止させてから（利用者の仕事の最中は今の「Wi-Fi operation in progress」の EBUSY）。END: 断らない（Wi-Fi の仕事の入れ子の経路でも直ぐ OK）、再開は event loop の次の回（`sleep_resume_later` → `run_due_work` → `sleep_resume`: 探索は `networkd_managed_wlan_resume(AUTO_SEARCHING)` と `schedule_automatic_work(0)`、manual は radio の `prepare_wlan_radios`）。眠っている間は自動の仕事・requested scan・それらの poll の timer を止め、眠りの終わりの timer を poll に入れる。log: `networkd: asleep (Wi-Fi state N recorded)`・`networkd: awake via=end|safety|failed resume=none|search|manual`。

## 確かめ（2026-10-07）

- build: `make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws052 build/ws052/bin/networkd` rc=0、warning 0。
- host: `sh plan/ws052/tests/run-host-networkd-sleep.sh` → `checks=20 failures=0`・PASS（記録・冪等・再開の表・起きている時の END・安全の時刻と poll・眠っている間の受け付け）。`managed-wlan.c` は zedBSD の network の header が要るので host では build せず、`networkd_managed_wlan_resume` は review だけ。
- QEMU・実機: 未（p011 の sessiond の後に、PREPARE・END の往復と Wi-Fi の再接続を T1 と 5330 の UAT で）。

## 残り（正常系の外、backlog へ）

- PREPARE の途中で retire が失敗した時（RETIRING と再試行の timer が残る）の戻し方は今の再試行に任せる（方針の resume は接続中で EBUSY になり得る）。
- `wifi_disable_defer` の再試行の timer を眠っている間に止めるのは、retire が成功する正常系では要らないので入れていない。
