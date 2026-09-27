<!-- awesome-plan project=zedbsd record=ws073p005 -->

# ws073-p005: BUG-028 — 閉じた port への connect が ECONNREFUSED で返る、自分の address へ届く

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-028](../../bugs/BUG-028.md)

## 目的と受け入れ

誰も listen していない port への TCP の `connect()` が、blocking・non-blocking とも、`127.0.0.1` でも自分の interface の address（QEMU の
`10.0.2.15`）でも、待たずに `ECONNREFUSED` で終わる。

## 再現（修正前、QEMU。main の測定用 guest image の kernel）

[tests/connect-refused.c](../tests/connect-refused.c)（guest の clang で build）:

- `127.0.0.1`: 3 つの connect は待たずに返るが errno が `ECONNRESET`（ticket の「返らない」は、この kernel では 127.0.0.1 では再現しない。
  lldb の `gdb-remote 127.0.0.1:1234` も約 5 秒で `Failed to connect` で戻る）。
- `10.0.2.15`（自分の ue0 の address）: blocking の connect は返らず alarm の EINTR、non-blocking は poll の 10 秒の timeout。
  listener のある port（sshd の 22、試験の listener）へも connect できず、UDP の datagram も届かない。**返らない connect はこちらで再現**した。

## 原因

1. `tcp_input()`（`src/kern/net/tcp.c`）は RST をどの状態でも `ECONNRESET` にしていた。SYN_SENT の RST は相手の拒否で、connect(2) は
   `ECONNREFUSED`（POSIX・BSD）。RFC 793 の SYN_SENT の RST の受け入れ条件（ACK が SYN を確認する）も無かった。
2. `ipv4_output_common()`（`src/kern/net/ipv4.c`）は自分の address 宛ての packet も route の device（ue0）から ARP をかけて送ろうとし、
   誰も答えない ARP で落ちていた。`ipv4_input()` は受けた device 自身の address 宛てしか受けないので、lo0 を回しても受けられなかった。

## 修正

- `tcp.c`: `tcp_reset_error_locked()` が SYN_SENT の RST を、SYN を確認する ACK 付きなら `ECONNREFUSED`、そうでなければ無視（0）とし、
  他の状態は従来どおり `ECONNRESET`。
- `ipv4.c`: 宛先が自分の live な interface の address（`inet_address_is_local()`、`inet-socket.c`）なら loopback の device
  （`net_loopback_ref()`、`core.c`）へ回し、ARP をせず lo0 の hardware address を使う。source は元の device が与えたものを保つ（上の層の
  checksum が既に含む）。入力は `ipv4_accepts_destination()` が、loopback の device では自分の全ての address 宛てを受ける。
- `icmp.c`: echo の reply を、request の宛先が自分の address なら その address を source にして送る（`ipv4_output_from()`、新設）。
  これが無いと自分の `10.0.2.15` への ping の reply が `127.0.0.1` から来て ping が数えない（試験中に見つけて直した）。
- 宣言は `src/kern/net/internal.h`（network の内部）。HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j48 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-a vmunix` warning 0。
  i386 の `config/ci/config-pcat.mk` の vmunix も warning 0。
- guest（`tests/kernel-image.sh` で main の測定用 image の vmunix だけを差し替え）: `tests/connect-refused.c 127.0.0.1 10.0.2.15` の 10 件全て PASS
  （修正前の kernel では 8 件 FAIL）。追加の確認: `ping -c 2 10.0.2.15` 2/2、`ping 127.0.0.1`・`ping 10.0.2.2`（gateway）も応答、
  `ssh 10.0.2.15` が鍵の交換まで進む、UDP の `127.0.0.1`・`10.0.2.15` 宛てが届く、`10.0.2.2:1` への connect は従来どおり ECONNREFUSED、
  host からの SSH（guest の操作そのもの）は変わらず使える。lldb の `gdb-remote 127.0.0.1:1234` は約 5 秒で `Failed to connect`。
- boot test: `make -j48 ZEDBSD_CONFIG=plan/ws045/tests/config-amd64-base.mk BUILD=build/ws073-img disk-image`（warning 0）で
  `plan/tools/boot-test.sh` PASS（`build/ws073-img/boot-test/login.png`）。
- 規約: `tests/style-diff.py`（tcp.c・ipv4.c・icmp.c・core.c・inet-socket.c・internal.h）0。全文の規約で見直した。

## 残り・関連

- 試験の出力で `strerror(ECONNREFUSED)` が "Unknown error" になる。これは [BUG-050](../../bugs/BUG-050.md)（libc の strerror）で、
  この Phase の範囲外。
- `127.0.0.2` など 127/8 の他の address は従来どおり lo0 の ARP で届かない（lo0 は 127.0.0.1 だけを持つ）。必要になれば別に扱う。
