<!-- awesome-plan project=zedbsd record=ws172-p003 -->
# ws172-p003: 鍵（FIDO2）の login と登録: passkey-fido2 と機器の helper、UI

Status: in-progress（2026-10-06 P2: 段 A（passkey-fido2・helper・`_passkey`）を実装し、host 試験 PASS。段 B（UI）と段 C（QEMU の鍵）は残り）
WS: [ws172](../ws.md)
設計: [phase001](../phase001/phase.md) の §7・§12（B1・B2・M3・M6・M7）と判断 P3・P4・P5・P8・P9、`docs/architecture/security.md` の「The parts」「The request」「The security key」
Queue: Q1 の P2 の列（2026-10-06、q824 → q826 → WS161 → **WS172**）

## 範囲

設計 §12.1 の p003。`/usr/libexec/passkey-fido2` と機器の helper（`_passkey`）、鍵の選び（PIN の前）、登録は 1 本だけ、Settings の鍵、greeter・lock の「Use security key」と CANCEL。第 1 段の規則で正常系だけ。準正常・異常は [backlog-p2](../../ws177/backlog-p2.md)。

## 段

| 段 | 内容 | 状態 |
| --- | --- | --- |
| A | `passkey-fido2`（auth・enroll-fido2・remove-fido2）、機器の helper、`_passkey` の account、`/etc/passkey` の鍵の行の追加・数の更新・削除、build の登録、host 試験 | 実装済み（2026-10-06） |
| B | sessiond の `ENROLLED` に鍵の一覧（id・label）、libkeiland-backend と compositor の口、greeter・lock の「Use security key」と TOUCH の表示と CANCEL、Settings の Users の鍵の card（登録・削除） | 未着手 |
| C | QEMU で鍵の流れを確かめる手段（判断 P5: 試験の kernel の loopback を、試験の秘密鍵で署名する CTAP2 の応答器に広げる）と T1 の試験 | 未着手（Q1 に相談: kernel の中の P-256 の署名が要る） |

## 段 A の実装（2026-10-06 P2）

- `userland/base/passkey-fido2/`（`/usr/libexec/passkey-fido2`、root、0500、package `passkey-fido2`、`base/passkey` と `security/openssl` を要る、amd64、既定は n）:
  - `main.c`（root）: 要求（`/sbin/passkey` と同じ形、`request.c`）、account（auth は名前だけ、登録・削除は `login_verify` で password）、使える account（uid 1000 以上、shadow の lock・期限）、`/etc/passkey` の鍵の行（private でなければ断る）。
    - auth: 32 byte の challenge、clientDataHash = SHA-256(`"zedbsd.login" NUL name NUL challenge`)、helper の答えを libpasskey の `verify.c` で確かめる（公開鍵は account の行から、UP と UV、署名、WebAuthn §7.2 の count、`cloned`）。大きくなった count は書き戻す。
    - enroll-fido2: label（1〜32 byte、制御文字と `:` 無し）、5 本まで、helper の authData を `pk_ctap2_read_made` で自分で読み直す（relying party、UP、UV、AT、ES256 の鍵が曲線の上）、行 `name:uid:fido2:ID:KEY:COUNT:zedbsd.login:LABEL:DATE`（base64url）を足し、`ok uid=N id=ID`。
    - remove-fido2: その account の ID の行を消す。
    - `/etc/passkey` の変更は account の lock の下で読み直し、signal を止め、版を確かめ、`passkey_record_edit`（新しい、`record.c`）で 1 行だけを足す・替える・消す。
  - `device.c`（root）: 鍵の node を開けて `HIDRAW_GRAB` で claim し、helper を fork して pipe の行を読む。`touch` は `status touch` としてすぐ出す。触れる待ちの 30 秒＋3 秒を越えたら helper を KILL して `timeout`。
  - `helper.c`（子）: 標準の fd を `/dev/null` に、core 無し、alarm、chroot `/var/empty`、`setgroups`・`setgid`・`setuid` で `_passkey`。
    - auth: PIN 無しの GetAssertion（`up:false`、allowList）で credential を持つ鍵を探し（B1）、その鍵にだけ PIN（PIN/UV の token、`getPinUvAuthTokenUsingPinWithPermissions` の GetAssertion の許可）を送り、触れて GetAssertion。鍵の PIN が無い鍵は使わない（P3）。
    - enroll: 鍵がちょうど 1 本（0 本は `no-key`、2 本以上は `many-keys`、B2・P9）、PIN の token（MakeCredential の許可）、MakeCredential（user id = 名前の SHA-256 の先頭 16 byte、登録済みの ID を excludeList に）。
    - 鍵の答えを passkey の理由の語に（`bad-secret`・`key-locked`・`no-key`・`timeout`・`device`）。
  - `wire.c`（純粋）: base64url、16 進、helper の行、鍵の行の作り・読み・count の書き替え、label、clientDataHash、user id。
- `userland/base/passkey/record.c`: `passkey_record_edit`（name と kind の行のうち 4 番目の欄が ID の行だけを替える・消す、足す）。`passkey_record_replace` は同じ下請けを使う形にした（動作は同じ）。
- libpasskey: `struct pk_made_credential` に authData そのものを持たせ、`pk_ctap2_read_made` を公開した（登録の authData を root が読み直すため）。
- `_passkey` の account（uid・gid 79、`/var/empty`、nologin、shadow `*`）を `userland/base/etc/passwd`・`group`・`shadow` に足した（P4）。
- build: `platform/amd64/vmunix.mk` に fidoctl と同じ形の link の規則（OpenSSL の staged の header と `libcrypto.so`）。試験の config `plan/ws172/tests/config-amd64-fido2.mk`（passkey の image ＋ openssl・passkey-fido2・fidoctl・loopback の鍵）。
- zedBSD に `RLIMIT_NPROC` が無いので、helper の「process を作れない」は rlimit ではなく空の root（exec する program が無い）で代えた（backlog に記録）。

## 確認（2026-10-06 P2、host）

| 確認 | 結果 |
| --- | --- |
| `plan/ws172/tests/fido2-host-test.sh`（`fido2-wire-host-test.c`: base64url の RFC 4648 の例と拒む物、16 進、helper の行、鍵の行の作り・読み・count、label、clientDataHash と user id を手で hash した物と比べる。Linux 向けの passkey-fido2 の全体を build し、root でなければ何も出さず終了状態 2） | PASS |
| `plan/ws172/tests/passkey-host-test.sh`（`passkey_record_edit` の追加・count の替え・削除・他の名前の ID を足した） | PASS |
| `plan/ws161/tests/libpasskey-host-test.sh`・`fidoctl-host-test.sh`（libpasskey の変更の後） | PASS |
| zedBSD の build（`ZEDBSD_CONFIG=plan/ws172/tests/config-amd64-fido2.mk BUILD=build/p2-fido`: passkey-fido2・fidoctl・passkey） | warning 0 |
| style-check（passkey-fido2 の 4 file と試験） | 0（`record.c`・`ctap2.c` の既存の指摘の数は変わらない: 10・33、p006 の全文規約で直す） |

未実施: 鍵との実際のやり取り（helper の sandbox、GRAB、GetAssertion・MakeCredential）は host では動かせない。QEMU は段 C の手段が要る。実物の YubiKey は WS161 p006 の UAT。

## 残り

- 段 B・段 C（上の表）。
- WS161 p005（NFC の transport）が入ったら、helper が `/dev/smartcard*` の鍵も使う。

## 2026-10-06 夜 ユーザーの決定（段 C、P5 の置き換え）

P2 の案 (a) kernel の試験の driver に CTAP2 の応答器、(b) userland の応答器と kernel の中継、(c) 実機の鍵だけ、へのクリックの回答「(c) 実機の鍵だけで確かめる」: QEMU では鍵の流れを試さず、実機の YubiKey（WS161 p006 の UAT）で確かめる。承認済みの P5（kernel の loopback を CTAP2 の応答器に広げる）は行わない。
