<!-- awesome-plan project=zedbsd record=ws161-p004 -->

# ws161-p004: libpasskey（cbor・CTAPHID・CTAP2・PIN/UV・verify）と host 試験

Phase ID: `ws161-p004`
Parent: [WS161](../ws.md)
Status: in-progress（2026-10-05 P1 q773: 中心の module と host 試験。2026-10-06 P2: os 層・道具 `fidoctl`・build の登録と host 試験。T1 の QEMU 待ち。規約の 38 件は p007）
Phase disposition: normal
Queue: q773

## 実装（`userland/base/libpasskey/`、WS172 P1 で base に）

- `cbor.c`・`cbor.h`: CTAP2 の正規の部分集合（最短の形、不定長・浮動小数・tag を拒む、map の key の正規の順と重複の拒否、深さ・数の上限）。
- `crypto.h`・`crypto-openssl.c`: 暗号の口（SHA-256・HMAC・乱数・P-256 の検証・ECDH・点の検査・AES-256-CBC・HKDF・wipe・定時間の比較）。OpenSSL を呼ぶのはこの file だけ（release の前に独自の実装に替える = WS172 p005）。
- `verify.c`・`verify.h`: 純粋な検証（credential を検証側の一覧から ID で引く、rpIdHash、UP・UV、AT 無し、拡張の map だけ、ES256 の COSE、WebAuthn §7.2 の sign count）。
- `hid.c`・`hid.h`: CTAPHID（INIT の nonce の照合、他の channel の report を飛ばす、KEEPALIVE の通知、取引ごとの期限、CANCEL）。
- `pin.c`・`pin.h`: PIN/UV protocol 1・2。
- `ctap2.c`・`ctap2.h`: GetInfo、ClientPIN（retries・keyAgreement・setPIN・changePIN・getPinToken 0x05・with permissions 0x09）、MakeCredential（authData を自分で読む: rpIdHash・UP・AT・ID・曲線の上の ES256 鍵）、GetAssertion（allowList・up:false の静かな問い）、Selection、CTAPHID の transport。

## 確認

- host: `plan/ws161/tests/libpasskey-host-test.sh`（ASan・UBSan、host の OpenSSL）PASS:
  - cbor（RFC 8949 の例と拒む入力）、verify（良い答え・high-S・拒む道の全部）、
  - ctap2（この test の中のソフトウェアの authenticator。暗号は OpenSSL を直接呼ぶ別の書き方で pin.c を照らす。protocol 2 の CTAP 2.1 と protocol 1 の CTAP 2.0: GetInfo、setPIN、誤った PIN で PIN_INVALID と retries 7、token、MakeCredential、他の RP の hash の拒否、静かな問いの持つ・持たない、GetAssertion を pk_verify_assertion で検証、changePIN、短い PIN の拒否。report には他の channel と KEEPALIVE と他の program の INIT の答えを混ぜた）。
- target の build: 未登録（package.mk の登録は passkey の道具と一緒に p004 の残りで）。

## 実装の続き（2026-10-06 P2）

- os 層（`userland/base/libpasskey/os.h`）:
  - `os-zedbsd.c`: `/dev/input/hidraw*` のうち `HIDRAW_GET_INFO` が FIDO の usage page と CTAPHID の usage の物。`pk_os_open` は `HIDRAW_GRAB` で取る。番号付きの report の鍵は取らない。
  - `os-linux.c`: `/dev/hidraw*` のうち、report の記述の最初の application collection が FIDO の物（`descriptor.c`）。grab は無い。
  - `os-posix.c`: 両方に共通の report の書き（ID の byte ＋ 64 byte）と読み（`poll` で待って 64 byte）。
  - `descriptor.c`: report の記述の読み（純粋な関数）。
- 道具 `userland/base/fidoctl/`: `list`・`info`・`set-pin`・`change-pin`・`[-p] register RP USER`・`[-p] assert RP CREDENTIAL`・`verify RP CREDENTIAL KEY CDH AUTH-DATA SIG`。PIN は標準入力からだけ読み、使った後に消す。`-d NODE` が無ければ最初の鍵を使う。答えは `NAME VALUE` の行。
- build の登録: `fidoctl` の package（base、amd64、既定は n、`security/openssl` を要る）。`platform/amd64/vmunix.mk` に、OpenSSL の staged の header と `libcrypto.so` で link する規則を足した。暗号の OpenSSL は WS172 と同じ Guardrail の例外の範囲（libpasskey）。試験の image は `plan/ws161/tests/config-amd64-fidoctl.mk`（hidraw の image＋openssl＋fidoctl）。

## 確認（2026-10-06 P2、host）

| 確認 | 結果 |
| --- | --- |
| `plan/ws161/tests/libpasskey-host-test.sh`（cbor・ctap2・verify に、新しい descriptor を足した。ASan・UBSan） | 4 本とも PASS |
| `plan/ws161/tests/fidoctl-host-test.sh`（Linux の os 層で host に build。`list`、使い方の誤りで終了状態 2、`fidoctl-assertion.py` の作った assertion を `verify` が通す、署名の違う物を拒む） | PASS |
| zedBSD の build（`ZEDBSD_CONFIG=plan/ws161/tests/config-amd64-fidoctl.mk BUILD=build/p2-fido build/p2-fido/bin/fidoctl`） | 自分の code の warning 0（OpenSSL の package の build の警告と perl の locale の警告は外部の物） |
| style-check（os 層・descriptor・fidoctl・試験） | 0 |

未実施:
- QEMU（T1）: `plan/ws161/tests/fidoctl-p004.sh`。loopback の鍵に対して list、info（CTAPHID INIT まで通り、GetInfo は loopback が ERROR を答える）、guest の libcrypto での verify。
- 実物の鍵（YubiKey）での info・PIN・register・assert は p006 の UAT。

## 残り

- NFC（p005）。
- 規約: `plan/tools/style-check.py` の指摘 38 件（ctap2.c・hid.c・pin.c、条件の中の呼び出し・ブロックの後の空行・三項演算子など）。p007 の全文規約で直す。
