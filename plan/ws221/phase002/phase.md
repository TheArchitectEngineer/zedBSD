<!-- awesome-plan project=zedbsd record=ws221-p002 -->
# ws221-p002: kernel + ifconfig + ping（pool の 2 class、MTU の口）

Status: in-progress（2026-10-29 P3 q1664: 下の「済み」まで、77bd8ef04。残りは「残り」）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i01（P3）
設計: [p001](../phase001/phase.md) §3.1・§3.2・§3.3・§3.7（ifconfig・ping）、§9 は全部推し（2026-10-29 ユーザー「全部 推しで (推し)」）

## 済み（commit）

| 項目 | commit | 確かめ |
| --- | --- | --- |
| pool の 2 class（標準 2048・jumbo 9216 × 32・予約 8、64-bit。riscv64 を含む）、`packet_buf_alloc_for`（EMSGSIZE/ENOBUFS、`PACKET_BUF_ALLOC_RECEIVE`）、空きの stack で O(1) の alloc・free、usage の jumbo の欄、copy は元の大きさの class | 85a74b441 | `plan/ws221/tests/run-packet-buf-host-test.sh` PASS（64-bit・-m32） |
| 64-bit の標準を 1024 に（TCP の ring 128 と受けの 128 のため）、配列を起動の時に `kern_calloc` で（image の上限に数えない。足りなければ半分ずつ） | a0dc13377、6d45f0a87 | amd64 CI の `__kernel_phys_end` の余り 4 KiB → 2.54 MiB、QEMU・VF2 で起動 |
| `net_device.mtu_max`・`mtu_default`、ops の `set_mtu`、`net_device_set_mtu`（up でも可、ioctl と同じ admission、0 = 既定、CHANGE の event）、`net_device_frame_max`、受けの 18 + MTU 超えを `rx_errors` に、`NET_MTU_MIN/MAX` | 85a74b441 | VF2: `ifconfig en0 mtu 1279/9001` → EINVAL の文、9000 → 成功、非 root → EPERM、`mtu default` → 1500 |
| UAPI `SIOCSIFMTU` 0x8922（superuser、0 = 既定）、IPv6 の `ra_mtu` と `ipv6_device_mtu_changed`（path MTU を消す） | 85a74b441 | 同上。lo0 の `mtu default` が EINVAL になる誤りを直して（NET_MTU_MAX の検査を外す）VF2 で 65535 に戻ることを確かめた |
| `ifconfig IF mtu N|default` | 85a74b441 | 同上 |

| `socket_datagram_max`（syscall.c の 4 か所、AF_UNIX は 2048 のまま）、UDP・raw ICMP・ICMP の echo socket・ICMPv6 の echo の返事・packet socket の送りを `alloc_for` に | 52e468447 | VF2（en0 と 5330 を一時 9000）: `ping -s 8972 10.0.30.3` が 8980 bytes で返る |
| `ping -s SIZE`（8〜65000、既定 56）、sendto の失敗の表示、返事の長さが送りと違えば truncated で数えない、ping6 も | 77bd8ef04 | VF2: `-s 1473` を MTU 1500 で → `ping: sendto: Message too long`、`-s 7` → usage、`ping -6 -s 1000 ::1` が 1008 bytes。5330 の MTU は 1500 に戻した |

## 残り

- socket ごとの jumbo の上限（2）（p001 §3.1 S7 (b)）。
- net.tcpstats の header の jumbo の欄（72 byte、p001 §3.1 M6）。
- QEMU の §8.2 (1)〜(4)（usb-net の 1514 で `mtu 9000` → EINVAL、`mtu 1400` → 成功、ping の長さ）。
- i386 の build（pcat）は warning 0 を確かめた（6d45f0a87 の tree）。

## 記録

- 2026-10-29 P3: VF2 の dw-eqos で MTU 9000 の frame が実機で通った（一時の差分 build/tmp/jumbo-hack.patch で確かめた後、85a74b441 で正式に）。5330 の Debian の enx6c1ff706148a を一時 9000 にして `ping -M do -s 8972` と flood 3000/3000（終わりに 1500 に戻した、永続の設定は触っていない）。
- 5330 の RTL8156 の descriptor（Debian の `lsusb -v`）: config 2 NCM・config 3 ECM とも `wMaxSegmentSize` 1518、NCM の `bmNetworkCapabilities` 0x2b（bit 3 あり）。CDC の mode では 9000 は届かない見込み（p003 で SET_MAX_DATAGRAM_SIZE を試す）。
