<!-- awesome-plan project=zedbsd record=ws169-p005 -->

# ws169-p005: browser の認証 code の自動入力

Status: uncleared（2026-10-07 T1-299: click で `ZBROWSER MAIL fill length=4 error=0` は出るが page の欄に入らない（CONSOLE の code-length 無し、欄は空、PNG /home/awe/zedBSD-worktrees/t1/build/t1-299/after-click.png）。P2 が直す）（旧: test-wait（2026-10-07 T1-298: titlebar の「Code 7351」と listen は確かめた（Q1 が PNG を目視）。control の click で欄に入る段は QEMU で未実施 → T1-299）（旧: test-wait（T1-291）））
Disposition: normal
Parent: [WS169](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)・[p004](../phase004/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §4。libbrowser の描画・engine には触れない（Q1 の ACK）。

- 設計からの変更: 「窓の下の帯」は、browser の窓の中身が engine の Vulkan の frame で shell に自前の描画の層が無いので、**titlebar の先頭の control「Code 482913」**にした。加えて action 付きの通知（「Sign-in code from …」）も出す（通知の popup は WS156 p003 が未実装なので、今は見えない。できたら click で入る）。
- `browser/shell/mail.c`（新規）: 起動で `kl_system_mail_listen(…, "browser")`（compositor が mail を持たなければ何もしない）。main loop の各 round で `kl_system_dispatch`、code 付きの mail を受けたら offer（titlebar の control と通知、2 分、新しい code が置き換える）、通知の ACTIVATED か titlebar の control の click で `browser_view_focus` と `browser_view_key`（数字は `DigitN`）で page の focus の欄に打つ。使った・時間切れで control と通知を取り下げる。log は長さだけ（`ZBROWSER MAIL code length=4 titlebar=1 notified=1`、`fill length=4 error=0`）。
- `browser/shell/titlebar.c`: `shell_titlebar_offer_code`（code の control を先頭に足して controls を宣言し直す、NULL で外す）。`shell.c`: state に mail、round の後に titlebar を宣言し直したら `shell_show_state`、`SHELL_CONTROL_CODE` の click、終了で offer を取り下げ。
- `mailer`（p004 の続き）: thread は起動時の account と sign-in の直後の account の folder を IDLE より先に自分で取る（IDLE が先に新着を告げて最初の取得を「新着」と見る競合を除いた）。folder を一度も取っていない時の取得は新着にしない。

## 試験の道具（回帰に使う）

- `plan/tools/mail/fake-mail-server.py`（p003 の `plan/ws169/tests/` から移した）: `--bind ADDRESS`・`--arrivals N` を足した。host の試験と AAT の両方が使う。**master の Tools への登録を Q1 に依頼**。
- AAT: `tests/scenarios/apps/mailer/read-compose.md` を backend に合わせて書き直し（account の追加・受信・本文・送信）、`tests/scenarios/apps/mailer/sign-in-code.md` を新規（Mail → compositor → Browser の code、3 の click は人か T1 の pointer）。helper は `plan/tools/aat/scenarios/helpers_mailer.py`（`helpers_apps.py` の古い read-compose を外した）。`check-scenarios.py` PASS。

## 確かめ

- host: `sh plan/ws169/tests/run-host-browser-mail.sh` → PASS 9（listen、code 無しの mail は offer しない、titlebar の「Code 482913」、通知の ACTION、通知の click で打つ・control が外れる、時間切れで取り下げ、titlebar の control で打つ、log に code が無い）。ASan・UBSan。
- host: `run-host-mail-backend.sh`（backend・thread）PASS、`run-host-mailer.sh` PASS 14。
- build: zedBSD の `bin/browser`・`bin/mailer` warning 0。style-check（mail.c・titlebar.c・shell.c・mailer の全 file）0。
- 未実施: QEMU（T1、AAT の `apps.mailer.read-compose`・`apps.mailer.sign-in-code`）、通知の popup からの入力（WS156 p003 の後）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS169 p005 の行。
