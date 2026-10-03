<!-- awesome-plan project=zedbsd record=ws135-p003 -->
# ws135-p003: libkeiland の `kl_settings_*` と probe

Status: cleared（q656-i01、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q656 / q656-i01（2026-10-04 user「WS135の設計D1-D6を承認します。」）

## 実装

| file | 内容 |
| --- | --- |
| `keiland/keiland.h` | `kl_settings_open/close/get/get_int/set/set_int/reset/watch/unwatch/dispatch/take_result`、`KL_SETTINGS_KEY_MAX`・`VALUE_MAX`・`KL_SETTINGS_DEFAULT`、`kl_settings_watch_fn` |
| `libkeiland/settings.c`（新） | wire: 専用の queue で registry を探し `kl_system_manager_v1` を bind、`get_settings` の object も同じ queue のまま（移さない: 既に queue に入った event は元の queue に残るため）、open で snapshot まで一度 roundtrip。`dispatch` は `wl_display_dispatch_queue_pending` の後に watch を呼ぶ。display の error で compositor の key を lost（watch に NULL、以後 ENOTSUP・EPIPE）。result を errno に。app の key は file へ直接、result は即時。`wl_interface` の表は library の中 |
| `libkeiland/settings-cache.c`（新、host 試験） | pending と done、合流（一度の dispatch で key ごとに一度、最後の値。A→B→A は呼ばない）、watch（prefix、callback の中の watch は次の変化から、unwatch は走査の後に除く）、lost、settle（open の時の値は変化として伝えない）、result の ring |
| `libkeiland/settings-app.c`（新、host 試験） | `~/.config/keiland/<app>.conf` の `<name>=<value>`。open で既定（DEFAULT）→ file の値。書きは 1 行の置換か除去・他の行を残す・fsync・rename。別の process には通知しない（D3 (a)） |
| `libkeiland/settings-private.h`（新） | cache と app の内部 |
| `libkeiland/Makefile*`・`exports.map` | 新しい source と `settings-keys.c`。export は 11 の関数を名前で（`kl_settings_key_*` は出さない、`nm -D` で確認） |
| `userland/tests/keiland-settings/`（新）・`platform/amd64/vmunix.mk` | probe（get・dump・set・reset・watch、errno は名前で出す）と link の規則 |
| `plan/ws135/tests/config-amd64-settings.mk`（新） | Settings の IME の image ＋ probe |

## 検証

- host: `sh plan/ws135/tests/host-settings.sh` → **26 passed, 0 failed**（ASan・UBSan）。`host-store.sh` → 45/45（p002 のまま）。
- build: zedBSD の libkeiland・probe・compositor（`ZEDBSD_CONFIG=plan/ws135/tests/config-amd64-settings.mk`）warning 0、`make keiland-linux` warning 0、`nm -D libkeiland.so` の `kl_settings_*` は 11（zedBSD・Linux）。
- QEMU（T2 に依頼）: `plan/ws135/tests/settings-p003.sh`（p002 と p003 をまとめて: snapshot・2 つの process の通知・範囲・read only・未知の key・FIFO の壁紙・壁紙・repeat・session の間に書かない・SIGTERM で merge・次の session・app の file）と回帰 `plan/ws089/tests/settings-p007.sh`（Settings が file を書き compositor が follow）。未実施。
- 未実施: FreeBSD の native build。probe の `main.c` の全文規約は p006 で見直す（test の道具）。

## 結果（Q1、2026-10-04）

cleared。T2-014（QEMU Venus、agent/p2 47b3418 の image）PASS 10/10: settings-p003・p007・p004・p005・settings-pages（1280x800、24 頁）・terminal-p009-guest（広い幅が restart の後も読み戻される）・files-open always（files.conf の選択と cleared）・files-open mouse・volume-p005（desktop.conf は書かれない、feedback の音 4）・boot-test。証拠 worktrees/t2/build/t2-014/out/。FreeBSD の native build と host 試験は T2-016（47b3418）で別に確かめる。

## FreeBSD（Q1、2026-10-04）

T2-016（FreeBSD 15.1 の QEMU guest、47b3418）: backend-test（native build warning 0・install・audit・host-seat-freebsd・host-session・host-power・sync-rejected・dmabuf-export-rejected）PASS、guest の clang 19.1.7 で host-store 43/0・host-settings 38/0（ASan・UBSan）。
