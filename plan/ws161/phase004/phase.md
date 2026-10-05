<!-- awesome-plan project=zedbsd record=ws161-p004 -->

# ws161-p004: libpasskey（cbor・CTAPHID・CTAP2・PIN/UV・verify）と host 試験

Phase ID: `ws161-p004`
Parent: [WS161](../ws.md)
Status: in-progress（2026-10-05 P1 generation19 q773。中心の module と host 試験まで。os 層・道具 `fidoctl`・build の登録は残り）
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

## 残り

- os 層（zedBSD の `/dev/input/hidraw*`・GRAB、Linux の `/dev/hidraw*`）、道具 `fidoctl`、target の build への登録。
- NFC（p005）。
- 規約: `plan/tools/style-check.py` の指摘 38 件（ctap2.c・hid.c・pin.c、条件の中の呼び出し・ブロックの後の空行・三項演算子など）。p007 の全文規約で直す。
