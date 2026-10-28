<!-- awesome-plan project=zedbsd record=ws169 -->

# WS169: メーラの app と compositor のメールの API

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p000〜p005 cleared（IMAP・SMTP、メールの API、認証 code の自動入力、T1-298・T1-302）。p006 の OAuth2 は 2026-10-06 夜 ユーザー（クリック）「今は IMAP/SMTP だけ」で作らない）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/ の source（browser/shell/mail.c:9、include/keiland/keiland.h:1613、libkeiland/system/kl-system-protocol.h:634、mailer/code.c:9・mail.h:9・secret.c:11、wayland/mail-shell.c:9）の comment が設計の節とユーザーの決定を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

メーラの app と compositor のメールの API を作り、許可された app（例: browser）がメールの受信の通知を受けて認証の code を自動で入力できるようにする。backend はまず IMAP4 と SMTP。

## ユーザーの決定

- 2026-10-05「[メール] - メーラappを追加する - コンポジタにメールAPIを追加する - メーラappはコンポジタに受信を連絡する - 許可されたアプリはコンポジタからメール受信の通知を受け取れる - たとえばブラウザは認証メールの通知を受け取って自動入力できる - バックエンドはIMAP4とSMTPをまずは対応 - GmailとOutlookのバックエンド対応が目標」
- 2026-10-05「WS169も、夕方のUATにあるとうれしいですが、必須ではないです。」
- 2026-10-06 夜（Q1 経由、クリック）: (a) password は `~/.config/keiland/mailer-accounts`（0600）に平文で仮置き。読み書きは 1 つの関数（`mailer/secret.c`）に閉じる。秘密の store への置き換えは WS177 の backlog。(b)「今は IMAP/SMTP だけ」: Gmail・Outlook の OAuth2（p006）は作らない（Gmail はアプリ パスワードなら IMAP/SMTP で使える）。
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p007 は WS の外の一括の見直しへ。

## 結果

- メーラの app（`userland/desktop/mailer/`: account・folder の一覧、一覧・読む・書く、IMAP4（TLS）・SMTP（STARTTLS）の backend と同期、認証 code の抽出）、compositor のメールの API と app ごとの許可（KL_VERSION 54）、browser の認証 code の自動入力（欄が無ければ clipboard と通知）が main にある。
- 証拠: QEMU の T1-298 の AAT（needs-person、Q1 が PNG で目視: read-compose の log と受信箱 3 通、p002〜p004）、T1-302（`ZBROWSER MAIL fill length=4 error=0`、p005。欄の PNG は撮れていない）。AAT の apps.mailer.read-compose・sign-in-code（`tests/scenarios/apps/mailer/`）。

## 制限・移管

- Gmail・Outlook の OAuth2（p006）はユーザーの決定で作らない。local の cache・offline、添付の保存と送信、HTML の表示、下書きの保存、ISO-2022-JP・Shift_JIS は範囲の外だった（文字コードの一部は WS177 p016 が足した）。password の平文の仮置きの置き換えは WS177 の backlog。
- 規約の全文（p007）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws169-p000 | UI の mock（account・folder・一覧・読む・書く） | cleared（T1-181 の撮影の後、p002〜p005 で実装） |
| ws169-p001 | 要件と設計（API・許可・backend・app） | cleared |
| ws169-p002 | compositor のメールの API と許可 | cleared（T1-298） |
| ws169-p003 | IMAP4・SMTP の backend | cleared（T1-298） |
| ws169-p004 | メーラの app（一覧・読む・書く） | cleared（T1-298） |
| ws169-p005 | browser の認証 code の自動入力 | cleared（T1-302） |
| ws169-p006 | Gmail・Outlook（OAuth2）（directory なし） | planning のまま取り下げ（ユーザー「今は IMAP/SMTP だけ」） |
| ws169-p007 | 規約の全文の見直し（directory なし） | 規約の全文は WS の外の一括の見直しへ |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws169/` にある。AAT と fake-mail-server が使う host 試験（`run-host-browser-mail.sh`・`run-host-mail-backend.sh` 一式）は plan/tools/mail/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
