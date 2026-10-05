<!-- awesome-plan project=zedbsd record=ws172 -->
# WS172: passkey の認証の枠組み（/sbin/passkey と /etc/passkey、sessiond は外部の program で認証）

Status: planning（2026-10-05 追加、ベータ2。WS162・WS163 の ~/.config の mock を置き換える。担当 P1、WS161 の kernel の作業の後）
Master: [master](../master.md)
Primary Milestone: MG006
Related: [WS161](../ws161/ws.md)（hidraw・smartcard・libpasskey）、[WS162](../ws162/ws.md)（FIDO2 の login）、[WS163](../ws163/ws.md)（PIN の login）

## 由来（ユーザー、2026-10-05 夕）

「/etc/passwd, /etc/shadowは変更せず、/etc/passkeyを導入してrootだけがアクセス可能に。sessiondは外部プログラムを呼んでログイン認証を行うように変更。/sbin/passkeyを呼び出す。passkeyコマンドは、パスワード認証、PIN認証、FIDO2認証ができる。/etc/passkeyには、アカウントとPIN、アカウントとFIDO2のID、アカウントとセキュリティチップ上のID、みたいな情報が入っている。passkeyコマンドは、将来はセキュリティチップでの認証にも対応する。セキュリティチップはMicrosoft方式もありえるし、独自のプラットフォームの方法も実装できる。これでどうでしょう。プロセス生成はだめでしょうか？」

「passkeyはbaseに起きます。OpenSSLはリリースまでに独自実装に置き換える予定なので、問題ないです。PIN の失敗の回数はsessiondがメモリ上に持てばいいです。試行のたびにファイルアクセスするのは、おそらく何らかのサイドチャネルアタックに使われます。」

## 決まったこと

- `/etc/passwd`・`/etc/shadow` は変えない。新しい `/etc/passkey`（root だけ、0600）に、account ごとの PIN の hash、FIDO2 の credential（ID と公開鍵）、将来のセキュリティチップの上の鍵の ID を置く。
- sessiond は認証を外部の program **`/sbin/passkey`**（base）に任せる（BSD Authentication と同じ考え、process の生成で良い）。passkey は password・PIN・FIDO2 を確かめ、将来はセキュリティチップ（Microsoft の方式・独自の方式）も。
- passkey は base に置く。暗号は当面 OpenSSL の libcrypto を使い、**リリースまでに独自の実装に置き換える**（master-design-policy §2.1 の例外、期限はリリース。Guardrail の例外の表に記録）。
- PIN の失敗の回数は **sessiond が memory に持つ**（試行ごとの file の読み書きをしない。side channel を避ける）。

## 設計で決めること（p001、Q1 の案を土台に）

- 秘密の渡し方: password・PIN は argv や環境変数で渡さず pipe（標準入力）、結果は終了の code か専用の fd。
- FIDO2: 機器（hidraw・smartcard）の data の解析は権限の無い子の process（sandbox）、passkey（root）は challenge の生成と libpasskey の verify.c での署名の検証だけ。
- lock の画面も sessiond の UNLOCK → passkey（compositor は PIN の file を読まない）。WS163 の mock を置き換える。
- 登録・変更（PIN の設定、FIDO2 の登録・削除）は sessiond 経由で passkey を起動して /etc/passkey に書く（passkey を setuid にしない、password の確かめも中で）。
- sessiond の memory の失敗の回数: 再起動で消えることの扱い（遅延の増加、password の後の再有効化）。
- sessiond の口: `AUTH name style`（秘密は続く行か別の fd）と `UNLOCK style`。greeter・lock の UI の方式の選び方。
- `/etc/passkey` の形式（行の形、版）。docs（docs/architecture/security.md と keiland.md の login の節）に先に書く。
- セキュリティチップ（TPM 2.0）は後の Phase。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | 設計（上の項目）と docs、design-reviewer | planning | — |
| p002 | `/sbin/passkey` の password・PIN と `/etc/passkey`、sessiond の外部の認証と memory の失敗の回数、lock・greeter の PIN | planning | p001 |
| p003 | FIDO2（hidraw・smartcard、子の sandbox と root の検証） | planning | p002、WS161 の p002〜 |
| p004 | セキュリティチップ（TPM 2.0）の設計 | planning | p002 |
| p005 | OpenSSL を独自の暗号に置き換える（リリースの前） | planning | p003 |
