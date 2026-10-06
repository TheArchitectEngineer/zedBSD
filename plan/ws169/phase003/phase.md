<!-- awesome-plan project=zedbsd record=ws169-p003 -->

# ws169-p003: IMAP4・SMTP の backend

Status: cleared（2026-10-07 Q1 の判定: T1-298 の AAT（needs-person）を Q1 が PNG で目視: Mail の read-compose の log（SIGNED-IN・REFRESHED・OPEN・SENT）と受信箱 3 通の PNG）（旧: test-wait（T1-291））
Disposition: normal
Parent: [WS169](../ws.md)
Queue: q831（2026-10-06、P2）
依存: [p001](../phase001/phase.md)

## 実装（2026-10-07、P2）

設計は [p001](../phase001/phase.md) §2。`userland/desktop/mailer/` の中、外部の package の source は使わない（TLS は OpenSSL の package を dlopen）。

- `mail.h`: backend の型と関数（libkeiland に依らない、host の試験が単独で build する）。`mailer.h` は `mail.h` を include し、folder と flag の定義を移した。
- `tls.c`: browser の `libbrowser/net/tls.c` と同じ作りの blocking 版（libssl・libcrypto を dlopen、TLS 1.2 以上、chain と host 名の検証、試験の CA を足す `ml_tls_add_ca_file`、load は mutex の下）。
- `conn.c`: `host[:port]` の読み（993・465 は最初から TLS、他は STARTTLS）、getaddrinfo で接続、行と literal の読み（30 秒の timeout）、TLS の pending を含む `ml_conn_ready`。
- `imap.c`: LOGIN（STARTTLS の後）、LIST の special-use（`\Sent` `\Drafts` `\Archive`／Gmail の `\All` `\Trash`）と普通の名前、SELECT、最新 N 通・UID の後の FETCH（`BODY.PEEK[]<0.1048576>`、literal の前後の UID・FLAGS・RFC822.SIZE）、`UID STORE`、COPY＋`\Deleted`＋EXPUNGE の移動、APPEND、IDLE（start・take・DONE）。
- `smtp.c`: 465 の TLS か STARTTLS、EHLO、AUTH PLAIN、MAIL・RCPT・DATA（行頭の dot を二重に、CR LF）、QUIT。
- `mime.c`: header の unfold、RFC 2047（B・Q、UTF-8・US-ASCII・ISO-8859-1）、From の名前と address、Date（`+hhmm`・GMT、秒の無い形）、multipart の text/plain（無ければ text/html から tag を除いた文字、style・script は捨てる、entity）、QP・base64、最初の添付の名前と大きさの見積もり、base64 の encode。
- `compose.c`: From（名前は quoted か encoded word）・To・Cc・Subject（ASCII でなければ encoded word）・Date（UTC）・Message-ID・In-Reply-To・References、本文は UTF-8 の quoted-printable、CR LF。
- `code.c`: 件名か本文に code の語（code・passcode・verification・verify・one-time・OTP・PIN・コード・認証・確認）があれば、文字・数字に接しない 6〜8 桁、無ければ年（1900〜2099）でない 4〜5 桁。

## 確かめ

- host: `sh plan/ws169/tests/run-host-mail-backend.sh` → PASS 50（ASan・UBSan、host の OpenSSL）。`fake-mail-server.py`（試験の CA と localhost の証明書をその場で作る）に対して、IMAP を最初から TLS と STARTTLS の 2 通りで（login・誤った password の EACCES・folder・select・最新 2 通と添付・既読・UID の後・\Seen・Archive への移動・Sent への APPEND・IDLE の新着と code）、SMTP を 465 と 587 の 2 通りで（server が受けた bytes が同じ、envelope、行頭の dot）。MIME の 5 通と code の 5 件、compose の読み戻し。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws169/tests/config-amd64-mailer.mk BUILD=build/ws169-zed build/ws169-zed/bin/mailer` が warning 0。`style-check.py` が新しい 7 file で 0。
- 未実施: QEMU（実の server には繋がない。T1 の試験は p004 の後に同じ偽の server を guest の外に立てて）、実の Gmail・ISP の server。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS169 p003 の行。
