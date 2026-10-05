<!-- awesome-plan project=zedbsd record=ws162-p001 -->

# ws162-p001: FIDO2 の login（要件と設計）

Phase ID: `ws162-p001`
Parent: [WS162](../ws.md)
Status: planning（2026-10-05 P1 generation17。設計の第 1 版。WS161 の判断（UAPI・libfido2）と §6 の判断（root の daemon の口を含む）の後に code）
Phase disposition: normal
Queue: ベータ2 の P1 の列の 6 番目（WS161 の後、q は Q1 が振る）
依存: WS161（`/dev/fidoN` と libfido2）

## 範囲

- ユーザー（2026-10-05、原文）:「FIDO2ログイン」
- 入る: greeter の login と lock の画面の unlock を、登録した security key（WS161）で行う。登録・削除は Settings の Users の頁。password・PIN（WS163）との関係。
- 範囲外: sudo・su・SSH の security key（SSH は OpenSSH を libfido2 つきで build し直す別の題）。Linux・FreeBSD（login は各 OS の仕組み）。NFC（WS161 の v2）。

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

## 結果

（設計の第 1 版。判断 H1〜H5 と WS161 の判断待ち）
