<!-- awesome-plan project=zedbsd record=ws132-p003 -->
# ws132-p003: Keiland の backend の事象と compositor の input の探し直し・電池の表示

Status: in-progress（2026-10-05 P1 generation17 / q717-i01。実装・build・host の試験まで。QEMU の試験を Q1 経由で T1 に依頼する。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS132](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q717 / q717-i01（P1）

## 範囲

[p001](../phase001/phase.md) の設計の U1 と compositor の分のうち、人間の判断 D1・D2 に依らない物:

1. libkeiland-backend の事象の口（events-zedbsd.c）: `/dev/system` を購読し、compositor の main loop に fd を渡す。Linux・FreeBSD の backend は stub（ベータ1 までは不要、2026-10-05 ユーザー）。OS の境界の checker（plan/tools/keiland-os-boundary/check.sh）を通す。
2. compositor: INPUT の add・remove で input の device を探し直す（USB キーボード・マウスの後挿し）。
3. compositor: AC・BATTERY の事象と `KERN_SYSTEM_GET_POWER` で system bar に電池・AC の状態を出す（電池が無い機械では出さない）。
4. POWER・LID の事象は受け取って記録するだけにし、動作は [p008](../ws.md)（D1・D2 の後）。

## 受け入れ

build の warning 0（zedBSD と Linux の keiland-linux.mk）、host の試験、OS の境界の checker が PASS。QEMU は T1 に依頼（USB キーボードの後挿しで入力が効く、電池の無い guest で bar に電池が出ない）。

## 所有 path

`userland/desktop/` の libkeiland-backend・compositor の該当の file、`plan/ws132/`。

## 実装（2026-10-05 P1）

| 所 | 内容 |
| --- | --- |
| `libkeiland-backend/keiland-backend.h` | host の callback に `input_changed`・`power_changed`・`power_button(button)`・`lid_changed(open)` を追加（`KL_BACKEND_BUTTON_POWER`・`_SLEEP`）。電源の状態は zedBSD で kernel から読み、どの thread からでも読めると明記 |
| `libkeiland-backend/backend-private.h`・`backend.c` | `events_descriptor` と `kl_backend_events_open/close/poll_count/poll_fill/poll_done`。open で開き close で閉じ、poll では seat の後ろに並べる |
| `libkeiland-backend-zedbsd/events-zedbsd.c`（新） | `/dev/system` を nonblocking・cloexec で開き POWER・LID・AC・BATTERY・INPUT（0x2f）を購読。readable で待っている記録を全部読み（8 件ずつ、EAGAIN まで）、INPUT は `input_changed`、AC・BATTERY は `power_changed`（1 回の poll で各 1 回）、OVERFLOW は両方、POWER の PRESS は `power_button`（subject `sleep-button` なら SLEEP）、LID は `lid_changed(value)`。購読を拒む古い kernel、読みの失敗・記録の端切れ・終わりは閉じて以後聞かない（compositor の 2 秒の scan は残る） |
| `libkeiland-backend/unsupported/events-unsupported.c`（新） | Linux・FreeBSD の stub（何も聞かない）。`Makefile.linux`・`Makefile.freebsd` に追加 |
| `libkeiland-backend-zedbsd/power-zedbsd.c` | `kl_backend_power_get_state` は呼ぶたびに `/dev/system` を開いて `KERN_SYSTEM_GET_POWER`: AC の有無で source（AC・BATTERY、不明）、電池があれば percent と charging |
| `wayland/backend-host.c`・`main.c`・`zwl.h` | `input_changed` は `input_scan_time = 0` で次の pass に探し直す（2 秒を待たない）。`power_changed` は `zwl_power_read`（`server->power`）と system の拡張の読み直しと frame の要求。`power_button`・`lid_changed` は log だけ（`ZWL EVENT power button` など、動作は p008）。起動時に `zwl_power_read`（`ZWL POWER source=… percent=… charging=…`） |
| `wayland/system.c` | `zwl_system_power_changed`: 次の tick で（読みの途中ならその後で）電源を読み直し、client に変化を伝える（`power_again`） |
| `wayland/shell.c` | bar の電池は mock-up をやめ、`server->power.percent >= 0` の時だけ描く。中身は % に比例（100% で 16 px、0% より上なら最低 2 px）、充電中は端子の右に「+」。電池の無い機械では描かず、その場所は空けたまま（他の icon の位置と試験の座標を変えないため） |

他の WS の試験の link の直し（backend.c が events を呼ぶようになったため、その場で直した）: `plan/ws131/tests/host-power.sh`・`host-session.sh`（`-Iinclude` も、power-zedbsd.c が `uapi/system.h` を読むため）・`host-seat-freebsd.sh`・`host-seat-linux.sh`、`plan/ws005/tests/bug149-build.sh` に `events-unsupported.c` を足した。

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws132-keiland-linux all`） | 成功、warning 0 |
| FreeBSD の Keiland | **未実施**（この host に FreeBSD の build 環境が無い。変更は source の一覧に stub を 1 つ足しただけ） |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `sh plan/ws132/tests/run-host-events.sh`（ASan・UBSan） | 54 checks passed: poll に seat の後ろで加わる、INPUT・AC・BATTERY の 4 件で各 callback 1 回、POLLIN の無い poll は読まない、power・sleep の button、PRESS でない POWER は無視、蓋の開閉、OVERFLOW で両方、20 件を 1 回の poll で全部読み callback 1 回、端切れと終わりで閉じる |
| `plan/ws131/tests/host-power.sh`・`host-session.sh`・`host-seat-freebsd.sh`・`host-seat-linux.sh` | 6/6・31/31・13/13・12/12 passed |
| `plan/ws005/tests/bug149-build.sh` | build 成功 |
| style-check（新しい file、変えた hunk） | 指摘 0（`power-zedbsd.c:84` の既存の指摘は変えていない行） |
| QEMU（`plan/ws132/tests/p003-guest.sh BUILD`、pen の guest） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330 の電池・AC） | **未実施**（p007） |

## QEMU の試験（T1 への依頼の内容）

- image: `plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk BUILD`（この branch の kernel に p002 の事象がある）。起動 `plan/ws079/tests/pen-guest.sh start IMAGE`。
- 試験: `plan/ws132/tests/p003-guest.sh BUILD [OUTDIR]`（BUILD/bin/wayland を guest に写す）。QMP で usb-kbd（xhci.0 port 5）を挿し、mouse で App Home を開き、挿した keyboard だけに Esc を送り、抜く。
- 合格: 全行 ok（subscribed、power-unknown、plug-event、plug-keyboard-taken（1.5 秒以内）、home-opened、escape-from-plugged-keyboard、unplug-event、unplug-closed、alive、no-error）。`OUTDIR/bar.png` で bar に電池が無いことを目で見る（ユーザーに見せる）。

- 2026-10-05 T1-107 FAIL 2: QMP の `usb port 5 (bus xhci.0) not found`（T1-106 と同じ）。p003-guest.sh も port 4 に。bar.png が console の文字の画面だった（QMP screendump は GL の scanout を写さない）ので、zdesktop-p013 と同じ VNC からの撮影（`plan/ws035/tests/zdesktop-check.py`）に替えた。再試験は T1。

## 残り

- QEMU の結果の判定（T1、Q1）。
- （済、q722）電池の無い機械で bar の電池の場所を詰める（下）。
- POWER・LID の動作は p008（D1・D2 の後）。

## q722: 電池の無い機械で bar の電池の場所を詰める（2026-10-05、ユーザーの変更「電池がないときは詰める」）

- `wayland/shell.c` の `bar_layout`: `server->power.percent < 0`（電池が無い）なら、電池の場所（44 px）を取らず、network の icon を時計の 36 px 左に置く。network・音量・IME・desktop・docked の窓の button が 44 px 右へ動く（電池のある機械は今まで通り）。電源の状態は最初の frame の前に読む（`zwl_power_read`）ので、log の位置（`ZWL NETWORK icon`・`ZWL VOLUME icon`・`ZWL GLASS desktops`・`ZWL IME indicator`）は最初から詰めた値。
- QEMU の試験の bar の座標: plan の試験を探したが、bar の右側（network・音量・IME・desktop・docked の button）の座標は全部 log から読んでいて（menu-bug148・connecting-bug154・zdesktop-p013/p065/p072/p104・zdesktop-p010・volume-*・ime-p005・settings-p021・p032-guest）、決め打ちの x は無かった。zdesktop-p010 の「右上の角の帯が network の icon と重ならない」は、icon が x=1107..1136 になっても帯（x≥1252）の外。直した試験の file は無し。
- zedBSD の compositor の build: 成功、warning 0。style-check: 変えた hunk の指摘 0。
- 未実施: QEMU（T1）。電池の無い QEMU の guest で bar の icon が時計の隣に詰まる PNG と、上の log を読む試験が通ること。
