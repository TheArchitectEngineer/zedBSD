<!-- awesome-plan project=zedbsd record=ws161-p001 -->

# ws161-p001: YubiKey（USB の FIDO2）の要件と設計

Phase ID: `ws161-p001`
Parent: [WS161](../ws.md)
Status: planning（2026-10-05 P1 generation17、q734。設計の第 1 版。code は §7 の判断（UAPI・外部 package）の後）
Phase disposition: normal
Queue: q734（ベータ2 の P1 の列の 6 番目、WS162 の前提）

## 範囲

- ユーザー（2026-10-05、原文）:「YubiKeyサポート（USBのFIDO2が最初。NFCのCTAP2 が目標）」
- 入る（この WS の v1）: USB の HID の FIDO2（CTAPHID）で security key と話す。kernel の driver と device の node、userland の library と道具、鍵の情報・
  登録・認証の試験。WS162（FIDO2 の login）が使う口。
- 目標（v2、別の段）: NFC の CTAP2（NFC の reader の driver が要る、§6）。
- 範囲外: login への組み込み（WS162）。YubiKey の OTP（キーボードとして働く interface は今の `usb-hid` がそのまま扱う）・PIV・OpenPGP（CCID）。

## 1. 今の形（2026-10-05 の main を読んだ）

| 項目 | 今 | 場所 |
| --- | --- | --- |
| USB の host | xHCI・EHCI・UHCI。class の driver は `struct drv_usb_driver`（ids・`match`・`attach`…）、interface ごとに `match` の点数が一番高い driver が取る（同点は後に登録した物） | `include/drivers/usb/usb.h` 230・267、`src/drivers/usb/usb.c` 4129・6740 |
| 転送 | URB（`drv_usb_urb_alloc`・`_setup`・`_submit`…）と同期の `drv_usb_interrupt`。endpoint ごとに URB は 1 つ（IN を張ったまま OUT を送れる）。interrupt OUT は controller では扱えるが、使う class の driver が無く試されていない | `usb.c` 1865・2158・2329・2866、`pci-xhci.c` 2040 |
| HID | 汎用の `usb-hid`（class 0x03 を点数 100 で取る）。interrupt IN を 1 つだけ使い、OUT は見ない。report の出力（OUTPUT の item・SET_REPORT）は扱わない。evdev の node だけを作り、input の能力が無い interface（FIDO の usage page 0xF1D0）は `ENODEV` で使われないまま | `src/drivers/usb/usb-hid.c` 144・266・609・1430、`src/drivers/generic/hid-report.c` 1465 |
| userland の node | evdev（`/dev/input/eventN`）だけ。raw の HID・汎用の USB の node は無い。USB の一覧は `/dev/system` の `KERN_SYSTEM_GET_USB_DEVICE`、hotplug は `KERN_SYSTEM_EVENT_USB` | `include/uapi/input.h`・`system.h` 203〜350 |
| 権限 | devfs の既定は 0666 root:wheel（event は 0640）。sessiond が seat の利用者に `/dev/gpu*`・`/dev/input/event*`・`/dev/backlight*` を 0600 で渡す（毎秒あて直す） | `src/kern/devfs.c` 426〜470、`userland/desktop/sessiond/seat.c` |
| crypto | base に SHA-2（libc）と SHA-512 crypt。P-256 の ECDSA・ECDH、HMAC-SHA-256、HKDF、AES-256-CBC は base に無く、OpenSSL 3.5.8 の package（既定の image に入っている）にある。CBOR は無い | `include/libc/sha2.h`、`userland/packages/security/openssl/` |
| FIDO・CCID・NFC | 何も無い | — |
| QEMU | host の QEMU 10.0.11 に `u2f-emulated`・`canokey` が無い（`u2f-passthru` は U2F だけで実物の鍵が要る）。実物の USB の転送は `usb-host`（RTL8822BU の前例） | `plan/ws005/phase020/rtl-guest.sh` 39 |

## 2. kernel: `usb-fido` と `/dev/fidoN`

- **driver**: `src/drivers/usb/usb-fido.c`（新）。HID の interface（class 0x03）の report の記述を読み、usage page 0xF1D0・usage 0x01 の collection があれば
  点数 200 を返す（`usb-hid` の 100 より高いので取れる）。interrupt IN と interrupt OUT の 2 つの endpoint を使う（CTAPHID の 64 byte の report）。
  YubiKey の他の interface（OTP のキーボード、CCID）は今のまま `usb-hid` などが扱う。
- **node**: `/dev/fidoN`（N は 0 から）。1 つの device を同時に 1 つの open だけ（2 つ目は `EBUSY`。CTAPHID の channel の取り合いを避ける）。
  - `write`: ちょうど 64 byte（1 つの report）。interrupt OUT で送る。それ以外の長さは `EINVAL`。
  - `read`: 64 byte の report を 1 つ。IN の URB を張ったままにし、来た report を小さな ring（32 個）に貯める。無ければ待つ（`O_NONBLOCK` は `EAGAIN`）。
  - `poll`: 読める report がある時に読める。
  - `ioctl`: `FIDO_GET_INFO`（vendor・product・release・製品の名前・serial の文字列、report の大きさ 64）だけ。
  - 抜いた時: `read`・`write` は `ENODEV`、node は消える。
- **UAPI**: `include/uapi/fido.h`（新）に `struct fido_info` と `FIDO_GET_INFO`、report の大きさの定数（§7 の H1）。
- **hotplug**: 今の `KERN_SYSTEM_EVENT_USB` と、node の追加の事象（`KERN_SYSTEM_EVENT_INPUT` と同じ形の `KERN_SYSTEM_EVENT_FIDO` か、既存の事象の subject
  `fidoN` で足りるかは p002 で決める）。
- **権限**: 既定は 0600 root。sessiond の `seat.c` が seat の利用者に渡す一覧に `/dev/fido*` を足す（Linux の systemd の uaccess と同じ考え: 前に座っている人が使う）。
  login の画面（WS162）は root の sessiond が直接開く。
- HAL は変えない。USB の core の変更は無い見込み（interrupt OUT の URB は今の API で張れる）。

## 3. userland: library と道具

案は 2 つ（§7 の H2）。

| | (A) libfido2 を package にする（推奨） | (B) 自前の小さな library |
| --- | --- | --- |
| 中身 | Yubico の libfido2（BSD-2-Clause）と libcbor（MIT）を外部の package（tarball を取得・検証して patch）にし、zedBSD の HID の backend（`/dev/fidoN` の open・read・write・poll、libfido2 の `fido_dev_io_t`）を足す。crypto は OpenSSL の package | CTAPHID の枠（INIT・CBOR・PING・CANCEL・KEEPALIVE・WINK・ERROR）、CTAP2 の GetInfo・MakeCredential・GetAssertion・ClientPIN（PIN protocol 2）、CBOR を自前で書く。crypto は OpenSSL か自前の P-256 |
| 道具 | `fido2-token`（一覧・情報・PIN の設定・reset）・`fido2-cred`・`fido2-assert` がそのまま付く | 自前の小さな `fido` の道具 |
| 他との関係 | OpenSSH を libfido2 つきで build し直せば `ssh-keygen -t ed25519-sk` の security key も使える（別の段）。Linux・FreeBSD の Keiland は各 OS の libfido2（hidraw・uhid）をそのまま使える | 3 つの OS の HID の口を自前で書く |
| 工数 | package 2 つ（CMake の前例は zlib・libjpeg-turbo）と backend 1 つ | CBOR と CTAP2 と PIN protocol の暗号の扱いを全部書く。誤りが安全に効く |
| ライセンス | BSD-2・MIT（監査して記録） | Zlib |

- 推奨は (A): 4 LW の見積もりの中で、成熟した実装（YubiKey の癖の扱い、CTAP2.1 の細部）を使える。暗号の細部を自前で書く危険を避ける。
- WS162 の login は root の sessiond（base の daemon）が assertion の検証をする。(A) なら sessiond が libfido2（package）を link するか、検証だけを
  小さな helper（package の側の program）に出すかを WS162 の設計で決める（§7 の H3）。

## 4. 試験

- host: (A) の backend の部分（`/dev/fidoN` の代わりに pipe を使う）と libfido2 の自前の試験。(B) なら CTAPHID の枠と CBOR の host 試験。
- kernel: QEMU には CTAP2 の emulator が無いので、試験の kernel の build に「loopback の FIDO の device」（試験用の小さな CTAPHID の応答器、`usb-hid-checkpoint` の
  前例のような試験だけの driver）を足し、`/dev/fidoN` の read・write・poll・EBUSY・抜いた時を T1 で確かめる（§7 の H4）。
- 実機（UAT）: 実物の YubiKey（5 系、USB-A か USB-C）を 5330 に挿し、`fido2-token -L`・`-I`、PIN の設定、`fido2-cred -M`（登録）・`fido2-assert -G`（認証）、
  抜き差し。QEMU の `usb-host` で実物を渡す試験も可能（host に鍵が要る）。

## 5. 段（案）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p002 | kernel: `usb-fido`・`/dev/fidoN`・UAPI・試験の loopback の device・sessiond の seat の一覧。T1 | H1・H4 |
| p003 | userland: (A) libcbor・libfido2 の package と zedBSD の backend、道具。host 試験 | p002、H2 |
| p004 | 実機の UAT（YubiKey）、Linux・FreeBSD の Keiland での libfido2 の確かめ | p003 |
| p005 | 全文規約の見直し | p004 |
| （v2） | NFC の CTAP2（§6） | p004 |

## 6. NFC（目標、v2）

- NFC の CTAP2 は ISO 7816-4 の APDU を NFC（ISO 14443-4）で運ぶ。必要な物: NFC の reader の driver と、APDU を送る口。
- 道筋の候補: (1) USB の CCID の class の reader（例 ACR122U、ACR1252U）: CCID の class の driver と PC/SC に当たる口（`/dev/ccidN` か pcsc-lite の package）、
  libfido2 の PC/SC の backend（libfido2 は pcsc に対応している）。(2) 機種に内蔵の NFC の controller（5330 にあれば、I2C の NXP の controller）: 専用の driver。
- reader の機種と 5330 の内蔵の有無を確かめてから、v2 の WS（か段）を立てる（§7 の H5）。

## 7. 人間の判断が要る点

| ID | 問い | 案 |
| --- | --- | --- |
| H1 | **UAPI**: `include/uapi/fido.h`（`/dev/fidoN` の read・write 64 byte、`FIDO_GET_INFO`）を足してよいか | 足す。FIDO の interface だけの raw の node（キーボードなどの raw の HID は出さない） |
| H2 | **外部 package**: libfido2（BSD-2）と libcbor（MIT）を package にし、OpenSSL の package に依る形でよいか（推奨 (A)）。自前で書くか（(B)） | (A) |
| H3 | WS162 で、root の sessiond が package の libfido2 を使ってよいか（使わないなら検証の helper を分ける） | WS162 の設計で決める（この WS では口だけ） |
| H4 | 試験だけの loopback の FIDO の device を試験の kernel に足す（QEMU に CTAP2 の emulator が無いため） | 足す（`CONFIG_` の試験の build だけ） |
| H5 | NFC の reader の機種（CCID の USB の reader を買うか、5330 の内蔵を使うか） | 5330 の内蔵の有無を UAT で確かめてから |
| H6 | `/dev/fidoN` を seat の利用者に渡す（前に座っている人が鍵を使える） | 渡す |

## 結果

（設計の第 1 版。判断 H1〜H6 待ち）
