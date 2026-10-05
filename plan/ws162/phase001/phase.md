<!-- awesome-plan project=zedbsd record=ws162-p001 -->

# ws162-p001: FIDO2 の login（要件と設計）

Phase ID: `ws162-p001`
Parent: [WS162](../ws.md)
Status: planning（2026-10-05 P1 generation17 で第 1 版。2026-10-05 夕のユーザーの決定で mock の第 2 版（§9）に改めた、P1 generation19 q771。§9.7 の判断と WS161 の device の後に code）
Phase disposition: normal
Queue: ベータ2 の P1 の列の 6 番目（WS161 の後、q は Q1 が振る）
依存: WS161（第 2 版: `/dev/input/hidrawN`・`/dev/ccidN` と libpasskey）

## 範囲

- ユーザー（2026-10-05、原文）:「FIDO2ログイン」
- 入る: greeter の login と lock の画面の unlock を、登録した security key（WS161）で行う。登録・削除は Settings の Users の頁。password・PIN（WS163）との関係。
- 範囲外: sudo・su・SSH の security key（SSH は OpenSSH を libfido2 つきで build し直す別の題）。Linux・FreeBSD（login は各 OS の仕組み）。NFC（WS161 の v2）。

**§1〜§8 は第 1 版（sessiond の口・`/etc/keiland/fido-keys`・libfido2）。§9 の第 2 版が置き換える。**

## 1. 今の形と前提

- greeter の login は `AUTH name password`、lock の画面は `UNLOCK password`。sessiond（root）が `login_verify` で確かめる（WS163 の設計 §1 と同じ）。
- WS163（6 桁の PIN）の設計で、PIN の保存（`/etc/keiland/pins`）と sessiond の要求（`PIN?`・`PIN SET`・`PIN REMOVE`）を案にしている。この WS は同じ形に揃える。
- WS161 の案: `/dev/fidoN`（64 byte の report の read・write）、libfido2 と libcbor の package（OpenSSL に依る）。

## 2. 認証の形

| 案 | 中身 | 強さ |
| --- | --- | --- |
| (a) 鍵と鍵の PIN（推奨） | 鍵に触れ、鍵自身の PIN（CTAP2 の clientPIN、4 文字以上、鍵が 8 回の失敗で締める）を入れる。assertion の user verification（UV）を求める | 持ち物と知識の 2 要素。鍵を盗まれても PIN が要る |
| (b) 鍵だけ | 鍵に触れるだけ（user presence） | 持ち物の 1 要素。鍵を盗まれると login される |
| (c) password と鍵 | password の後に鍵に触れる | 2 要素。手間は一番大きい |

推奨は (a)（Windows の security key の login と同じ。鍵の PIN の試行の上限は鍵が持つ）。

## 3. 登録と保存

- 登録（Settings の Users の頁、自分の account）: 「Add security key」→ 今の password → 鍵に触れる（鍵に PIN が無ければ設定を促す）。
  - sessiond が非常駐の credential（resident でない）を作る: RP の id `kei-login`、user の id は account の名前と uid から作る。
  - 保存: `/etc/keiland/fido-keys`（0600 root）。1 行 1 鍵: `name:credential_id:public_key(COSE):sign_count:label`（base64）。1 account に 5 本まで。
- 削除: 一覧から選んで「Remove」、今の password で確かめる。
- 平文の秘密は無い（公開鍵と credential の id だけ）が、誰がどの鍵を持つかを隠すため root だけが読める file にする。

## 4. login の流れ

- greeter は、鍵を登録した利用者を選ぶと「Use security key」の button を出す（sessiond への問い合わせ `KEY? name` → `KEY yes|no`）。
- greeter → sessiond `AUTHKEY name pin`（lock の画面は `UNLOCKKEY pin`）。sessiond は:
  1. 挿さっている `/dev/fidoN` を開く（root。seat の利用者が持っていても root は開ける）。
  2. 32 byte の乱数の challenge で、その利用者の credential の id を全部付けて GetAssertion（UV 必須、鍵の PIN で）。
  3. 鍵が触れるのを待つ間、greeter に `TOUCH` を返し、greeter は「Touch your security key」を出す（最大 30 秒）。
  4. 署名を保存した公開鍵で検証し、sign_count が前より大きいこと（鍵の複製の兆し）を確かめ、保存の値を更新する。
  5. `OK`（login・unlock）か `FAIL`（理由: 鍵が無い・時間切れ・PIN の誤り・鍵が締まった・署名の誤り）。
- 失敗の遅れは今の規則（2 秒から 16 秒）を掛ける。鍵の PIN の失敗の上限は鍵が持つ（8 回で鍵が締まる。締まった鍵は鍵の reset が要る、その時は password で login）。
- password はいつでも使える（鍵を無くした時）。

## 5. 誰が鍵と話し、検証するか

| 案 | 中身 |
| --- | --- |
| (A) 検証の helper（推奨） | 小さな program `keiland-fido-auth`（libfido2 の package の側、`KEILAND_LIBEXECDIR`）を sessiond が root で起こし、fd 0 に要求（credential の一覧・challenge・PIN）、fd 1 に答え（OK・FAIL と新しい sign_count）。sessiond（base の daemon）は package の library を link しない |
| (B) sessiond が libfido2 を link | 手早いが、base の root の daemon が package（libfido2・libcbor・OpenSSL）に依る |

推奨は (A)。helper は鍵の device と標準入出力だけを使う（WS168 の sandbox の口ができたら、device の fd だけを渡して隔離できる）。

## 6. 人間の判断が要る点

| ID | 問い | 案 |
| --- | --- | --- |
| H1 | 認証の形 | (a) 鍵と鍵の PIN |
| H2 | **root の daemon の口**: sessiond に `KEY?`・`AUTHKEY`・`UNLOCKKEY`（greeter・session）と、登録の `KEY ADD password label`・`KEY REMOVE password id`（session の control）を足してよいか | 足す（WS163 の PIN の口と同じ形） |
| H3 | 鍵と話す・検証する場所 | (A) 検証の helper（sessiond は package を link しない） |
| H4 | 保存の場所と形 | `/etc/keiland/fido-keys`（0600 root、公開鍵・credential の id・sign_count、1 account 5 本） |
| H5 | 鍵を使う場面 | greeter と lock の画面だけ。sudo・SSH は別の題 |

## 7. 試験

- host: `fido-keys` の読み書き、sign_count の規則、helper の要求と答えの形（libfido2 の代わりに試験の stub）。
- QEMU（T1）: WS161 の試験の loopback の FIDO の device（CTAPHID の応答器）で、登録・login・時間切れ・sign_count の後戻り（複製）を断ること。
- 実機（UAT）: 実物の YubiKey で、登録、greeter の login、lock の画面の unlock、鍵の PIN の誤り、抜いた鍵。

## 8. 段（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | sessiond の口と保存、検証の helper、host 試験 | WS161 p003、H1〜H5 |
| p003 | greeter と lock の画面の「Use security key」、Settings の Users の頁の登録・削除（WS089 の領域、Q1 の調整）、T1（loopback） | p002 |
| p004 | 実機の UAT、全文規約の見直し | p003 |

## 9. 第 2 版: mock（2026-10-05 夕のユーザーの決定、P1 generation19 q771）

### 9.0 決まったこと

- ユーザー（2026-10-05、原文）:「sessiondに制御を入れず、libfido2をコンポジタのgreeterが直接叩くモックアップを作ってください。設定は~/.configの中でOKです。
  あとで鍵管理やPAMのような仕組みをきちんと考えます。」
- 「FIDO2周りは、libfido2, libcborも含めて、独自のライブラリ libpasskey にまとめて、独自に作ります。」→ libfido2 の所は WS161 の **libpasskey**。
- sessiond に口を足さない（第 1 版の H2 の `KEY?`・`AUTHKEY`・`UNLOCKKEY`・`KEY ADD`・`KEY REMOVE` はやめる）。保存は利用者の `~/.config`（H4 の
  `/etc/keiland/fido-keys` はやめる）。
- 検証の場所の後の形は未定（2026-10-05 Q1: BSD Auth の形 = `login_<style>` の helper、sandbox の CTAP の helper と libcrypto の小さな検証器を検討中）。
  mock は libpasskey の `passkey_get_assertion`（鍵と話す）と `passkey_verify_assertion`（検証だけ、WS161 §9.4）を呼ぶだけにし、後で別の process に
  分けられる形にする。

### 9.1 認証の形（第 1 版の H1 の案 (a) のまま）

鍵に触れ、鍵自身の PIN（CTAP2 の clientPIN、UV）を入れる。鍵の PIN の失敗の上限は鍵が持つ（8 回で締まる）。password はいつでも使える。

### 9.2 保存（`~/.config/keiland/passkeys`）

- 1 行 1 鍵（1 account に 5 本まで）: `credential_id`（base64url）、`public_key`（COSE の EC2 P-256、base64url）、`sign_count`、`label`、それと §9.4 の
  greeter のための `hmac_salt`・`wrapped`（下）。mode 0600、利用者の持ち物、書き換えは一時 file から rename（WS163 の pin-store と同じ）。
- RP の id は `keiland.login`（この端末の login の印。web の RP と混ざらない名前）。credential は非常駐（resident でない）で、allowList に
  credential の id を付けて GetAssertion する。

### 9.3 lock の画面（compositor が直接、sessiond に何も送らない）

- lock の画面は session の利用者の compositor なので、自分の `~/.config/keiland/passkeys` を読み、seat から渡された `/dev/input/hidraw*`（WS161 U3）を開ける。
- 「Use security key」→ 鍵の PIN の欄 → compositor が thread で: 32 byte の乱数の challenge から clientDataHash を作り、`passkey_get_assertion`（allowList・
  UV・UP）、「Touch your security key」を出す（最大 30 秒）→ `passkey_verify_assertion`（保存の公開鍵、rpIdHash、UP・UV、sign_count が前より大きい）→
  合えば unlock（WS163 の PIN と同じ道）、sign_count を更新して書く。
- 鍵の PIN は検証の後に memory から消す。

### 9.4 greeter（`_greeter` の uid）: 判断 K1

WS163 の G1 と同じ壁がある: greeter は `_greeter` で、利用者の home（0700）の `~/.config` を読めない。login は sessiond の `AUTH name password` だけで始まる
（sessiond に口を足さない）。鍵で login するには、greeter が最後に利用者の **password** を sessiond に送るしかない。

- 案 (a): **mock は lock の画面だけ**。greeter は password のまま（WS163 の G1 の推奨と同じ）。
- 案 (b): **鍵の hmac-secret で password を包む**。CTAP2 の hmac-secret の拡張は、鍵の中の秘密と salt から 32 byte を返す（鍵と PIN が無ければ出ない）。
  登録の時に compositor は（今の password を sessiond の `UNLOCK` で確かめた後で）その 32 byte の鍵で password を AES-256-GCM で包み、`wrapped` と
  `hmac_salt` を保存する。greeter は GetAssertion（hmac-secret、UV）で同じ 32 byte を得て password を開き、`AUTH name password` を送って消す。
  - 強さ: 包んだ password は鍵の秘密（256 bit）で守られ、PIN の 6 桁のような手元の総当たりはできない（WS163 の G1 (b) と違う）。鍵と鍵の PIN が要る。
  - 欠点: (1) greeter が file を読めるよう、置き場所を変える必要がある: 利用者の home を 0711 に（`.config` と `.config/keiland` も 0711、file 0644）するか、
    `_greeter` が読める別の場所（例 `/var/db/keiland/passkeys/<name>`、書くには特権が要る）。どちらも「設定は ~/.config」と home 0700 の今の規則に
    触れる。(2) password を変えると包みが古くなる（greeter は失敗を見て「password で login して鍵を登録し直す」を出す。compositor が password の変更の
    時に包み直すのは、新しい password を compositor が持つ Settings の変更の道でだけできる）。(3) 鍵が hmac-secret に対応している必要がある
    （YubiKey 5 は対応）。
- 案 (c): sessiond に口を足す（ユーザーの方針に反する。BSD Auth の形の検討の結論を待つ）。

推奨: **(b)**、置き場所は利用者の home を 0711 にせず、`~/.config/keiland/passkeys` は今のまま 0600 で、greeter 用の小さな file
`~/.config/keiland/passkey-login`（credential の id・hmac_salt・wrapped だけ、公開鍵なし）を 0644 に、`~` と `~/.config`・`~/.config/keiland` を 0711 にする。
home の中身の名前は 0711 で一覧できない（読めるのはその file の名前を知る者だけ）。ただし home の mode の規則の変更なので、ユーザーの判断（K1）。

### 9.5 登録（Settings の Users の頁、WS163 と同じ道）

- 「Add Security Key」→ 今の password・label → 「Touch your security key」（鍵に PIN が無ければ鍵の PIN の設定を先に: libpasskey の setPIN）。
- Settings → compositor の `kl_system_account_v1` に要求を足す: `add_passkey(uint request, string current, string label, string key_pin)`・
  `remove_passkey(uint request, string current, string credential_id)`（WS163 の set_pin と同じく、compositor は password を sessiond の今の `UNLOCK` で確かめる）。
  compositor が thread で `passkey_make_credential`（RP `keiland.login`、UV、K1 が (b) なら hmac-secret）を行い、`~/.config/keiland/passkeys` に書く。
- 一覧と削除は Settings が自分で file を読んで出す（label・登録の日・credential の id の頭）。

### 9.6 段（第 2 版）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | 保存（passkeys の読み書き、sign_count）と compositor の lock の画面の「Use security key」、host 試験（WS161 の software authenticator） | WS161 p004、K2 |
| p003 | Settings の登録・削除（`add_passkey`・`remove_passkey`）、greeter（K1 の答えによる）、T1（WS161 の loopback の device） | p002、K1 |
| p004 | 実機の UAT（YubiKey 5、USB と ACR1252U の NFC）、全文規約の見直し | p003、WS161 p006 |

### 9.7 人間の判断が要る点（第 2 版）

| ID | 問い | 案 |
| --- | --- | --- |
| K1 | greeter の鍵の login: (a) mock は lock の画面だけ、(b) hmac-secret で password を包み、greeter が読める小さな file（home・`.config` を 0711、file 0644）、(c) sessiond の口（BSD Auth の検討の結論の後） | (b)。home の mode を変えたくなければ (a) |
| K2 | 認証の形: 鍵と鍵の PIN（UV） | 第 1 版の H1 (a) のまま |
| K3 | protocol: `kl_system_account_v1` に `add_passkey`・`remove_passkey`（version 11）を足す | 足す（WS163 の set_pin と同じ形） |

## 結果

（設計の第 1 版。判断 H1〜H5 と WS161 の判断待ち）

2026-10-05 generation19: ユーザーの決定（mock、sessiond に口を足さない、~/.config、libpasskey）で第 2 版（§9）に改めた。K1〜K3 と WS161 の device 待ち。
