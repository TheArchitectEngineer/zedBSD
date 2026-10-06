<!-- awesome-plan project=zedbsd record=ws169-p001 -->

# ws169-p001: メールの要件と設計

Status: cleared（2026-10-06 P2、設計だけ。正常系の範囲は q831 の規則）
Disposition: normal
Parent: [WS169](../ws.md)
Queue: q831（2026-10-06、P2）
依存: なし

## 決定（2026-10-06）

- ユーザー（Q1 経由、クリック）: (a) password は `~/.config/keiland/mailer-accounts`（0600）に平文で仮置き。読み書きは 1 つの関数（`mailer/secret.c`）に閉じる。秘密の store への置き換えは [WS177 backlog-p2](../../ws177/backlog-p2.md)。(b) 今は IMAP/SMTP だけ。Gmail・Outlook の OAuth2（p006）は今回は作らない（ベータ2 から外す）。Gmail はアプリ パスワードなら IMAP/SMTP で使える。
- Q1: p002（compositor の口と許可）は p001 の後に入れる。

## 1. compositor の口（p002）

system manager の新しい拡張 `kl_system_mail_v1`（manager の version 15、`get_mail` は opcode 10、capability の bit `0x800`）。WS156 の notify と同じ作り（同じ uid の client だけ、system manager の global が見えるのは同じ uid）。

| 向き | 名前 | 引数 | 意味 |
| --- | --- | --- | --- |
| request 0 | destroy | — | object を捨てる |
| request 1 | arrived | request, account, from, subject, code | メーラが新しいメールを知らせる。本文は送らない。code は認証 code の候補（無ければ空） |
| request 2 | listen | request, app | 読み手の app が自分の名前で受け取りを頼む |
| event 0 | mail | from, subject, code | 許された読み手への受信の通知 |
| event 1 | result | request, applied, saved | 要求の答え（OK・INVALID） |

- 許可: desktop の設定 `mail.codes.<app>`（BOOL、既定 0、compositor が持つ、desktop.conf に残す）。表にある読み手は今は `browser` だけ（`mail.codes.browser`）。表に無い app の listen は INVALID。compositor は mail を送る時に読み手ごとに設定を読み、1 の読み手にだけ送る。
- 許可の UI: Mail の app の中の「Sign-in codes」の switch（「Let Browser fill in sign-in codes from Mail」）。kl_settings_set で compositor の設定を変える。Settings の Notifications の頁はまだ「later」なので、そちらへの移設は backlog。
- 秘密の扱い: compositor の log には差出人・件名・code を出さない（長さだけ）。メーラの log も code を出さない。
- libkeiland（KL_VERSION は次の番号）: `KL_SYSTEM_HAS_MAIL`、`KL_SYSTEM_CHANGED_MAIL`、`kl_system_mail_arrived`、`kl_system_mail_listen`、`kl_system_take_mail_event`（8 個の ring）。

## 2. backend（p003）

- メーラの中に置く（外部 package は使わない、libetpan などは無し）。TLS は browser と同じく OpenSSL の package の libssl を dlopen（`mailer/tls.c`、TLS 1.2 以上、chain と host 名の検証）。
- IMAP4rev1: 993 の implicit TLS、`LOGIN`、`LIST`（`\Sent`・`\Drafts`・`\Archive`・`\Trash` の special-use、無ければ名前で）、`SELECT`、`UID SEARCH ALL`、`UID FETCH`（`FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[]`、最新の 50 通）、`UID STORE +FLAGS (\Seen)`・`(\Deleted)`、`UID COPY`＋`EXPUNGE`（Archive・Delete）、`IDLE`（新着の待ち、28 分で入り直す）。
- SMTP: 465 の implicit TLS か 587 の STARTTLS、`EHLO`、`AUTH PLAIN`、`MAIL FROM`・`RCPT TO`・`DATA`（dot-stuffing）。送った物は IMAP の Sent に `APPEND`。
- MIME: header の unfold、RFC 2047（B・Q、UTF-8・US-ASCII・ISO-8859-1）、`multipart/*` は text/plain の部分（無ければ text/html の文字だけ）、quoted-printable・base64、charset は UTF-8・US-ASCII・ISO-8859-1。添付は名前と大きさだけ一覧に。
- 書く message: `Content-Type: text/plain; charset=utf-8`、`Content-Transfer-Encoding: 8bit`、件名は RFC 2047 の B、`Date`・`Message-ID`・`In-Reply-To`。
- network は worker thread 1 本。account ごとに IMAP の接続を 1 本持ち、命令の無い間は全部の接続で IDLE に入って poll で待つ。main の loop とは pipe 2 本（命令と結果）で、結果の pipe を `kl_app_watch_fd` で見る。

## 3. app（p004）

- account が無ければ、右の 2 つの pane に「Add an Account」の画面: Name・Email・Password・IMAP server・SMTP server（`host` か `host:port`）。Sign In で worker が IMAP に login と LIST を試し、成功で保存。
- 保存: 秘密でない物は `~/.config/keiland/mailer.conf`（account ごとの key=value）、password は `~/.config/keiland/mailer-accounts`（0600、`secret.c` だけが読み書き）。
- 起動の時と Get Mail で INBOX ほかの 5 つの folder の最新 50 通を取り、IDLE で INBOX の新着を待つ。新着: 一覧に足し、`kl_app_notify` で「New mail from …」、code があれば compositor に arrived。
- 既読は開いた時に `\Seen`、Archive・Delete は COPY と `\Deleted`・EXPUNGE。送信は SMTP と Sent への APPEND。
- local の cache は持たない（起動ごとに取り直す。cache は backlog）。

## 4. 認証 code（p004・p005）

- 抽出（メーラの `code.c`）: 件名か本文に code を示す語（code・passcode・verification・one-time・OTP・PIN・コード・認証・確認）があり、本文に前後が数字・英字でない 4〜8 桁の数字があれば、最初の物。
- browser（p005）: shell が `kl_system_mail_listen(…, "browser")`。mail の事象に code があれば、窓の下に「Sign-in code 482913 from Example Bank — Fill in」の帯を 2 分出し、Fill in で page の focus の欄に code を文字の入力として渡す（IME の commit と同じ経路）。libbrowser の描画には触れない。

## 範囲外（今回）

OAuth2（Gmail・Outlook、ユーザーの決定で外す）、local の cache・offline、添付の保存と添付の送信、HTML の表示、ISO-2022-JP・Shift_JIS、下書きの保存。準正常・異常系は実装した時に WS177 の backlog-p2.md へ。
