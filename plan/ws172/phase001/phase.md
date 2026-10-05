<!-- awesome-plan project=zedbsd record=ws172-p001 -->

# ws172-p001: passkey の認証の枠組みの設計

Phase ID: `ws172-p001`
Parent: [WS172](../ws.md)
Status: planning（2026-10-05 P1 generation19 q774。設計の第 1 版と docs、design-reviewer の指摘で第 2 版（§12）。判断 P1〜P10 待ち）
Phase disposition: normal
Queue: q774

## 範囲

ws.md の「設計で決めること」。外の設計は docs が先: `docs/architecture/security.md` の「Login authentication」と `docs/architecture/keiland.md` の
login の節（この Phase で書いた）。ここは内部の決めごとと、その理由・判断。範囲外: 実装（p002〜）、セキュリティチップ（p004・p004b）。

## 1. 部品と境界

| 部品 | 権限 | 仕事 |
| --- | --- | --- |
| greeter・lock の画面（compositor） | 利用者・`_greeter` | 入力を集めて sessiond に送るだけ。秘密を確かめない、file を読まない |
| sessiond | root（常駐） | 方式の選び、失敗の数（memory）と遅れ、`/sbin/passkey` の起動と答えの中継、login・unlock の決定 |
| `/sbin/passkey` | root（sessiond の子、短命） | 1 つの要求を確かめて終わる: password（shadow）・PIN（`/etc/passkey`）・FIDO2（challenge の生成と署名の検証）、登録・削除（`/etc/passkey` を書く） |
| passkey の機器の子 | `_passkey`（新しい system account）、chroot `/var/empty` | FIDO2 の機器（hidraw・smartcard の fd、親が開けて渡す）と CTAP で話し、assertion の bytes だけを返す。鍵の file も challenge の意味も知らない |
| libpasskey | base の library（`userland/base/libpasskey/`） | CTAPHID・NFC の APDU・CTAP2・CBOR・PIN/UV。verify.c は機器に触れない純粋な検証 |

- WS161 の設計で libpasskey は Keiland の側（package の境界）だったが、passkey が base に入るので **libpasskey も base** に置く（Guardrail の例外: OpenSSL の
  libcrypto を release まで使う。passkey と libpasskey は package の `openssl` を require する。release の前に独自の暗号に替える = p005）。
- passkey は setuid にしない（mode 0500 root）。real uid が 0 でなければ断る。sessiond（と console の道具の将来）だけが起こす。

## 2. sessiond と passkey の間

- 起こし方: `fork` → 子は環境を消し、fd 0 に要求の pipe、fd 1 に答えの pipe、他を閉じて `execv("/sbin/passkey", {"passkey", NULL})`。argv に秘密・名前を載せない。
- 要求（標準入力、1 行 1 項目、終わりは EOF、最大 4 KiB）:

  ```text
  <operation>          auth | enroll-pin | remove-pin | enroll-fido2 | remove-fido2 | styles
  <account name>       sessiond が決めた名前（unlock・登録は session の利用者、greeter の login は greeter が選んだ名前）
  <style>              password | pin | fido2（auth だけ）
  <secret>             auth: password・PIN・鍵の PIN（鍵に PIN が無ければ空）。登録・削除: 今の password
  <argument...>        enroll-pin: 新しい PIN。enroll-fido2: label と鍵の PIN。remove-fido2: credential の ID
  ```

- 答え（標準出力、行）: 途中に `status touch`（鍵に触れて、の合図。0 回以上）、最後に `ok` か `fail <reason>`、exit status 0・1（2 は内部の誤り）。
  reason は固定の語: `bad-secret`・`no-such-user`・`not-enrolled`・`locked-account`・`no-key`・`timeout`・`device`・`replay`・`bad-request`・`busy`・`internal`。
- sessiond は `status touch` を greeter・lock の画面に `TOUCH` として中継する。passkey の時間の上限: password・PIN 5 秒、FIDO2 35 秒（触れる待ち 30 秒）。
  越えたら sessiond が kill する（`timeout`）。

## 3. sessiond の口（greeter と session の control）

| 要求 | 誰 | 答え |
| --- | --- | --- |
| `STYLES name` | greeter | `STYLES password[ pin][ fido2]`（登録がある方式。PIN が失敗で止まっていれば `pin` は出さない） |
| `AUTH name style` + 次の行に秘密 | greeter | `TOUCH`（0 回以上）、`OK` か `FAIL reason` |
| `UNLOCK style` + 次の行に秘密 | session | 同じ（名前は session の利用者、compositor は名前を送らない） |
| `ENROLL pin` + 次の 2 行（今の password、新しい PIN） | session | `OK` か `FAIL reason` |
| `ENROLL fido2 label` + 次の 2 行（今の password、鍵の PIN） | session | `TOUCH`…、`OK id` か `FAIL reason` |
| `REMOVE pin` / `REMOVE fido2 id` + 次の行（今の password） | session | `OK` か `FAIL reason` |

- 今の `AUTH name password` と `UNLOCK password` は `AUTH name password` + 秘密の行・`UNLOCK password` + 秘密の行に替わる（同じ版の greeter と sessiond
  で入れ替えるので互換の口は残さない）。秘密を要求の行と分けるのは、log に要求の行を出しても秘密が載らないため。
- 一覧（Settings の Users の頁）は `ENROLLED` → `ENROLLED pin fido2:<id>:<label>...`（session の利用者の物だけ）。

## 4. 失敗の数（ユーザーの決定: sessiond の memory）

- sessiond は account ごとに: password の続けての失敗の数、PIN の続けての失敗の数、FIDO2 の続けての失敗の数、最後の失敗の時刻。file には書かない。
- 遅れ: 今の規則（2 秒から、3 回ごとに 2 倍、最大 16 秒）を方式に依らず account ごとに掛ける（遅れは passkey の答えの後、sessiond が掛ける）。
- **PIN は 5 回続けて失敗したら止める**（`STYLES` から消え、`AUTH name pin` は `FAIL locked`）。password か FIDO2 で入れたら戻す。
- sessiond の再起動で数は消える（ユーザーの決定の帰結）。補い: sessiond の起動から 60 秒は PIN を受けない（再起動を繰り返させて試す手を遅くする）。
  sessiond が落ちること自体は syslog に残る。判断 P2。
- FIDO2 の鍵の PIN の失敗の上限は鍵が持つ（8 回で鍵が止まる）。sessiond の数は遅れのためだけ。

## 5. `/etc/passkey`

- root:wheel 0600。`/etc/shadow` と同じく、共通の account の核（`account.c`）の lock・全体の読み直し・新しい file に書いて rename で替える。
- 形（text、1 行 1 記録、`:` で区切る。base64url は `:` を含まない）:

  ```text
  # zedBSD passkey 1
  <name>:pin:<SHA-512 crypt>
  <name>:fido2:<credential id, base64url>:<COSE key, base64url>:<sign count>:<rp id>:<label>:<登録の日 YYYY-MM-DD>
  <name>:chip:<provider>:<key id>:<label>          （将来、p004b）
  ```

- 1 account に PIN 1 つ、FIDO2 は 5 本まで。label は 32 文字まで（`:` と改行を含まない）。
- 読みの規則: 知らない kind の行は残して書き戻す（新しい版の passkey が書いた行を古い版が消さない）。壊れた行は無視して log、書き戻しでは残す。
- account の削除（account-admin の remove）は `/etc/passkey` のその名前の行も消す（account-admin の変更、p002）。名前の変更は無い。

## 6. PIN

- 6 桁の数字（WS163 の H5 の規則を引き継ぐ: password は 8 文字以上なので混ざらない。ただし PIN の方式は `style` で明示されるので、6 桁の password を
  PIN と取り違える問題は無くなる）。hash は SHA-512 crypt（`$6$rounds=...`）。rounds は password と同じ高さでよい（passkey は短命の別 process で、
  画面の event loop を止めない）。
- 使える場面: greeter と lock の画面だけ（WS163 の H1）。sudo・su・SSH・console の login は password だけ（passkey を使わない）。
- 登録: 今の password が要る（passkey が shadow で確かめる）。password が locked（`!`・`*`）の account には登録させない。

## 7. FIDO2

- RP の id は `zedbsd.login`（この機械の login の印。web の RP と混ざらない）。credential は非常駐、`ES256`（COSE -7）だけを求める。
- **auth**:
  1. passkey（root）が `/etc/passkey` から名前の credential を集める（無ければ `not-enrolled`）。
  2. 32 byte の乱数の challenge を作り、clientDataHash = SHA-256(`"zedbsd.login\0"` ‖ name ‖ `"\0"` ‖ challenge)。
  3. 機器を開ける: `/dev/input/hidraw*`（FIDO の usage）と `/dev/smartcard*`（card が在る slot）。root で開け、WS161 V1 の GRAB が入れば grab する。
  4. 子を作る: fd（機器と pipe）だけを残し、chroot `/var/empty`、`_passkey` に setgid・setuid、RLIMIT（CPU 40 秒・memory）。子は libpasskey で、どの機器に
     触れたかを待ち（複数の鍵: 全部に要求を出し、最初に触れた物を使い他に cancel）、GetAssertion（rpId、clientDataHash、allowList、UV に鍵の PIN、
     PIN の protocol は GetInfo で選ぶ）を行い、`status touch` を親に知らせ、答えの bytes（credential ID・authData・署名）を長さつきで親に書いて終わる。
  5. 親（root）は libpasskey の `verify.c` で: credential ID から `/etc/passkey` の公開鍵を引く（子が言う鍵を信じない）、rpIdHash が `zedbsd.login` の
     SHA-256、flags の UP と UV、署名（authData ‖ 自分の作った clientDataHash）、sign count が 0 か保存の値より大きい（下がったら `replay`、複製の兆し）。
     合えば sign count を書き戻して `ok`。
- **enroll-fido2**: 今の password を確かめ、同じ形の子で MakeCredential（rp `zedbsd.login`、user id = 名前の SHA-256 の先頭 16 byte、`ES256`、UV、
  excludeList に登録済みの ID）、attestation は検証しない（鍵の型を信用の根に使わない。v1 の制限として docs に書く）、得た credential ID と公開鍵を保存。
- 鍵に PIN が無い時: auth は UV 無し（UP だけ）を許すか → 判断 P3（案: 許さない。登録の時に鍵の PIN の設定を求める）。

## 8. password

- passkey の `auth password` は今の `login_verify`（`userland/base/login/verify.c`）を使う。sessiond は `login_verify` を直接呼ばなくなる。
- console の `login`・`su`・`sudo`・`passwd` は変えない（passkey を通さない）。

## 9. 試験

- host: passkey の要求の読み（境界・長さ・不明の語）、`/etc/passkey` の読み書き（知らない kind の保持、壊れた行、5 本の上限）、PIN の hash、FIDO2 の
  verify（WS161 p004 のソフトウェアの authenticator の assertion、改ざん・replay・他の rpId）。sessiond の失敗の数と遅れ・PIN の停止（host で compile する部分）。
- QEMU（T1）: login の image で PIN の登録（Settings）、greeter と lock の PIN、5 回で止まる、password で戻る、sessiond の再起動の後の 60 秒。FIDO2 は
  試験の kernel の loopback の鍵が署名をしないので、loopback を「試験の鍵の秘密を持つ CTAP2 の応答器」に広げるか（判断 P5）、実機（YubiKey）だけで確かめる。
- 実機（UAT）: YubiKey 5（USB）と ACR1252U＋YubiKey 5 NFC で登録・greeter・lock。

## 10. 段（ws.md の確定）

| Phase | 内容 |
| --- | --- |
| p002 | `/sbin/passkey`（password・PIN・`styles`・登録・削除）と `/etc/passkey`、sessiond の口と memory の失敗の数、greeter・lock の PIN の UI、Settings の PIN、account-admin の remove。WS163 の mock（compositor の pin-store・set_pin）を外す |
| p003 | FIDO2: 子の sandbox（`_passkey`）、libpasskey（WS161 p004・p005）を base に、auth・enroll、greeter・lock の「Use security key」、Settings の登録・削除 |
| p004・p004b・p005 | ws.md のとおり |

## 11. 人間の判断が要る点

| ID | 問い | 案 |
| --- | --- | --- |
| P1 | libpasskey を base（`userland/base/libpasskey/`）に置く（WS161 の設計の「Keiland の側」を改める）。OpenSSL の例外は passkey と同じく release まで | 置く |
| P2 | sessiond の再起動で失敗の数が消えることの補い: 起動から 60 秒は PIN を受けない | この補いで足りる |
| P3 | 鍵に PIN（UV）が無い時の FIDO2 の login | 許さない（登録の時に鍵の PIN を求める） |
| P4 | 新しい system account `_passkey`（機器の子の uid）を足す | 足す |
| P5 | QEMU の FIDO2 の試験: 試験の kernel の loopback の鍵を、試験だけの秘密鍵で署名する CTAP2 の応答器に広げる（U4 の範囲を広げる） | 広げる（実機の前に QEMU で流れを確かめる） |
| P6 | WS163 の mock（~/.config の pin・compositor の確かめ・`kl_system_account_v1` の set_pin）を p002 で外す（protocol の version 10 の set_pin は残して `unsupported` を返すか、request ごと消すか） | set_pin を sessiond の `ENROLL pin` に繋ぎ替える（Settings の口はそのまま、compositor が sessiond に中継） |

## 12. 第 2 版: 敵対的な見直し（2026-10-05、design-reviewer）の扱い

docs（`docs/architecture/security.md` の「Login authentication」、`keiland.md`、`docs/reference/security-keys.md`）を先に直した（aae0188a・6dadd5ac）。
§1〜§11 と食い違う所は、この節と docs が正しい。

| 指摘 | 中身（要点） | 扱い |
| --- | --- | --- |
| B1 | 鍵の PIN を挿さっている全部の鍵に送ると、他の鍵の PIN の試行を減らし、悪い機器は PIN の hash を得て手元で総当たりできる | **直した**: 先に PIN 無しで鍵を選ぶ（allowList で `up:false` の GetAssertion を全部に、該当が 1 本ならそれ、複数なら触れて選ぶ = selection 0x0B か 2.0 では UP だけの GetAssertion）。PIN は選んだ 1 本にだけ。該当しない鍵は PIN を見ない（`no-key`） |
| B2 | 登録の時に悪い機器が先に答えると、その鍵で後から入れる | **直した**: 登録は鍵がちょうど 1 本の時だけ（2 本以上は `many-keys`）、Settings に鍵の名前と場所を出す、root が答えの authData を自分で読む（rpIdHash・UP・UV・AT・credential ID・P-256 の ES256 鍵）。attestation を見ないことが何を守らないかを docs に書いた |
| B3 | 再起動（greeter の `POWER reboot` は認証が要らない）で PIN の 5 回が戻る。60 秒の待ちでは足りない | **直した（P2 を改める）**: sessiond の起動の後、その account が password か鍵で一度入るまで PIN を出さない（端末の再起動の後の passcode と同じ）。試行は passkey を起こす前に数え、成功でだけ戻す（落ちても戻らない）。時間切れ・kill は失敗に数える |
| M1 | account の削除の途中で落ちると、同じ名前の新しい account が古い PIN と鍵を受け継ぐ | **直した**: 行に uid を持ち名前と uid の両方が合う時だけ数える、削除は `/etc/passkey` を最初に、追加は名前の残りを消す（docs の Robustness の順） |
| M2 | password が locked・期限切れでも PIN と鍵で入れる | **直した**: PIN と鍵は毎回 shadow の lock と期限を見る（`locked-account`）、uid 1000 未満と root には出さない。account-admin の reset-password は PIN と鍵を消す（判断 P7） |
| M3 | 「0 か大きい」は 0 で上書きして複製の検出を消す | **直した**: WebAuthn §7.2 の規則（どちらかが 0 でなければ新しい方が大きいこと）、下げない、理由の語は `cloned`。libpasskey の `verify.c` は既にこの規則（host 試験あり） |
| M4 | CANCEL が無く、sessiond が最大 35 秒止まる | **直した**: `CANCEL`、passkey の答えは sessiond の loop が poll する fd、別の要求には `ERROR busy` |
| M5 | kill で helper と account の lock が残る | **直した**: passkey は自分の process group、sessiond は SIGTERM の 2 秒後に SIGKILL、passkey は自分の期限を守り書き換え中は signal を止める、helper は親の pipe の EOF で CANCEL して終わり alarm も持つ |
| M6 | password の login が OpenSSL の package に依る | **直した**: `/sbin/passkey` は libc の `crypt()` だけ（password と PIN）、鍵は別の exec `/usr/libexec/passkey-fido2`（libpasskey と libcrypto を link）。Guardrail の例外の行を passkey-fido2 と libpasskey に広げる依頼（判断 P1） |
| M7 | greeter（`_greeter`）にも鍵の node が渡っている。touch hijack は未解決 | **直した**: seat は login の画面の account に hidraw・smartcard を渡さない（6dadd5ac、sessiond の seat.c）。passkey-fido2 は開いた node を claim する（WS161 V1 の (a) の GRAB を p003 の必須の前提に = 判断 P8）。NFC は contactless の slot だけ、他が持つ slot は触らない |
| M8 | ENROLL・REMOVE が遅れの無い password の当て物になる | **直した**: その password の失敗も同じ数と遅れ |
| m1〜m13 | 返事の buffer（greeter.c の 16 byte）、要求の枠（行の数を固定、制御文字を拒む、AUTH の次の行は必ず秘密）、fd 2 を /dev/null に、signal・RLIMIT_CORE・OpenSSL の設定を読まない、helper の rlimit（NOFILE・NPROC 0）と chdir、NFC の「tap」・途中で外れた鍵・途中で挿した鍵、CTAP の細部（maxCredentialCountInList・PIN_BLOCKED の語・内蔵 UV・protocol の選び）、file の細部（持ち主と mode の確かめ・新しい版は読むだけ・label の文字）、数の表は uid ごと（不明の名前は 1 つにまとめる）、`ok uid=N` で sessiond が uid を照らす、docs と phase の食い違い、WS163 の mock の移行（`~/.config/keiland/pin` は取り込まず消す、H5 の 6 桁の判定・pin-store・forgive を外す）、WS161 の道具の名前の衝突、規約の Phase と依存 | docs に入れた物は直した。実装の注意として p002・p003 の phase に持ち越す。WS161 の道具は `passkey` から **`fidoctl`** に改める（WS161 p001 §9.4 を直す） |

### 12.1 段（第 2 版）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | `/sbin/passkey`（password・PIN・styles・enrolled・enroll-pin・remove-pin）と `/etc/passkey`（uid つきの行）、sessiond（passkey の非同期の起動・CANCEL・busy・uid ごとの数・起動の後の PIN の規則・`ok uid` の照らし）、greeter・lock の PIN、Settings の PIN（set_pin を `ENROLL pin` に）、account-admin（削除・追加・reset の順）、WS163 の mock を外す。host 試験と T1 | P2・P6・P7 |
| p003 | `/usr/libexec/passkey-fido2` と機器の helper（`_passkey`）、鍵の選び（PIN の前）、登録は 1 本だけ、Settings の鍵、greeter・lock の「Use security key」と CANCEL | p002、WS161 p004・p005・V1 の GRAB（P8）、P3・P4・P5 |
| p004・p004b・p005 | ws.md のとおり | |
| p006 | 全文規約の見直し（WS の終わり） | p005 まで |

### 12.2 人間の判断が要る点（第 2 版）

| ID | 問い | 案 |
| --- | --- | --- |
| P1 | libpasskey を base（`userland/base/libpasskey/`）に置く。password・PIN の `/sbin/passkey` は libc だけ、鍵の `/usr/libexec/passkey-fido2` と libpasskey が OpenSSL の例外（release まで）。Guardrail の例外の行をこの 2 つに広げる | この形 |
| P2 | （改めた）sessiond の起動の後、account が password か鍵で一度入るまで PIN を出さない（再起動で試行が戻らない）。60 秒の待ちはやめる | この形 |
| P3 | 鍵に PIN（UV）が無い時の鍵の login と登録 | 許さない |
| P4 | system account `_passkey` を足す（uid 1000 未満、nologin、`/var/empty`、shadow `*`） | 足す |
| P5 | QEMU の鍵の試験: 試験の kernel の loopback の鍵を、試験だけの秘密鍵で署名する CTAP2 の応答器に広げる（試験の build だけ。release の passkey-fido2 は `HIDRAW_BUS_VIRTUAL` を拒む） | 広げる |
| P6 | WS163 の mock を p002 で外し、Settings の set_pin を sessiond の `ENROLL pin` に繋ぎ替える（空の PIN は `REMOVE pin`） | この形 |
| P7 | account-admin の reset-password で、その利用者の PIN と鍵を消す | 消す |
| P8 | WS161 V1 の (a)（hidraw の排他の GRAB の UAPI）を WS172 p003 の必須の前提にする（passkey-fido2 は開いた鍵を claim する） | 必須にする |
| P9 | 登録は鍵がちょうど 1 本挿さっている時だけ（2 本以上は断る） | この形 |
| P10 | 再起動の後、password か鍵で一度入るまで PIN を出さない規則は、autologin（`/etc/keiland/autologin`）の起動にも同じく効く（autologin は「入った」に数えない） | 数えない |

## 結果

（設計の第 1 版、2026-10-05。docs を先に書いた。design-reviewer の指摘で第 2 版（§12）、判断 P1〜P10 待ち）
