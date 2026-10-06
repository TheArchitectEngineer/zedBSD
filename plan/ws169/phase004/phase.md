<!-- awesome-plan project=zedbsd record=ws169-p004 -->

# ws169-p004: メーラの app（一覧・読む・書く）

Status: test-wait（T1-291）
Disposition: normal
Parent: [WS169](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)・[p003](../phase003/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §3。

- `store.c`（新規）: account と message の store（message は追加だけ、移動した物は folder を `ML_FOLDERS` にして隠す、index は run の間変わらない）。日付の語（今日は時刻、Yesterday、週内は曜日、今年は「Mon 28 Sep」、前は年つき。長い形「Monday, 5 October 2026 at 09:41」）、差出人の色は address の hash、添付の「File, 18.4 MB」。
- `sync.c`・`sync.h`（新規）: server と話す thread。job（sign-in・refresh・send・seen・move）を順に、account ごとの IMAP の session で行い、結果を queue に。job の無い間は全 account の INBOX で IDLE、poll で job の pipe と各 socket を待つ（25 分で入り直し、失敗した account は 5 分後）。新着は INBOX の UID の後を取り `arrived=1`。送信は SMTP の後に Sent へ APPEND して取り直す。
- `account.c`・`secret.c`（新規）: `~/.config/keiland/mailer.conf`（account ごとに name・address・user・imap・smtp）と、password の `~/.config/keiland/mailer-accounts`（0600、平文、2026-10-06 ユーザーの仮置き。読み書きは secret.c の 2 関数だけ）。どちらも `.new` に書いて rename。
- `view.c`: 試験の data を store に置き換え（既読は message の flag、一覧は日付の新しい順）。Send・Get Mail・Archive・Delete・未読を開く・Sign In・code の switch は window への request（`ml_view_take_request`）。account が無い時と Add Account で「Add an Account」の form（名前・Email・Password（点）・IMAP・SMTP、Sign In、Cancel、「Sign-in codes: Let Browser fill in sign-in codes」の switch）。sidebar の下に Add Account と、Get Mail の下に状態（「Updated 09:41」・「Getting mail...」・失敗の語）。返信は In-Reply-To の ID、全員に返信は To と Cc を Cc に。
- `main.c`: 起動で account を読み、desktop の設定（`mail.codes.browser`、変化は `kl_settings_watch`）、thread を起こし結果の pipe を `kl_app_watch_fd`。結果: message を store へ（同じ UID は捨てる）、新着の未読は `kl_app_notify`（「New mail from …」と件名）と、code があれば `kl_system_mail_arrived`（p002）。request: 送信は `ml_compose` して job に、Archive・Delete は隠して MOVE、Sign In は form から（server が空なら `imap.<domain>`・`smtp.<domain>`）、成功で store と disk に保存し refresh。menu に Add Account。
- 試験の data（p000 の mock の `data.c`）は program から外し `plan/ws169/tests/host-mailer-data.c` へ（store に入れる関数）。

## 確かめ

- host: `sh plan/ws169/tests/run-host-mailer.sh` → PASS 14（p000 の 10 の send・get を request の log に、Add Account の form・code の switch・Sign In の request・Cancel を足した）。絵 `build/ws169/host-mailer-setup.png` を目で見た。
- host: `sh plan/ws169/tests/run-host-mail-backend.sh` に thread の試験 `host-mail-sync.c` を足した（別の偽の server）→ host-mail-backend PASS・host-mail-sync PASS 10（誤った password の sign-in の失敗、sign-in、refresh、IDLE の新着と code 7351、送信と Sent の写し、Archive への移動、IDLE の途中の stop）。ASan・UBSan。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws169/tests/config-amd64-mailer.mk BUILD=build/ws169-zed build/ws169-zed/bin/mailer` が warning 0。`style-check.py` が mailer の全 file で 0。
- 未実施: QEMU（WS169 の最後に T1。guest から host の偽の server へ、`/etc/hosts` の名前と `SSL_CERT_FILE` の試験の CA で）、実の server。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS169 p004 の行。
