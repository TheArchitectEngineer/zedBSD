<!-- awesome-plan project=zedbsd record=ws221-p002 -->
# ws221-p002: kernel + ifconfig + ping（pool の 2 class、MTU の口）

Status: cleared（2026-10-29 05:52 P3 q1664: 設計の項目は全部済み（b0166efaf）。QEMU §8.2 (1)〜(4) PASS、VF2 で jumbo の ping・socket の jumbo の上限・送受の回帰 PASS）
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

| socket ごとの jumbo の受けの上限（`SOCKET_RECEIVE_JUMBO_MAX` 2、stream でない socket、enqueue で数え dequeue・requeue で戻す） | 82d181f4e | build warning 0、tcp-model PASS。実機で上限に当てる試験は未実施（下） |
| net.tcpstats の header に jumbo の欄（`jumbo_total`・`jumbo_used`・`jumbo_used_max`・`jumbo_reserve`・`jumbo_alloc_failures`、72 byte、version 1 のまま、`_Static_assert` 72）、sysctl が表示し `struct_size` を検める | b0166efaf | QEMU: `jumbo_total=32 … jumbo_reserve=8`（下の qemu-mtu.sh） |
| QEMU の試験 `plan/ws221/tests/qemu-mtu.sh` と `config-amd64-jumbo.mk`（p001 §8.2 (1)〜(4)。tap の代わりに 2 つ目の user network 10.0.3.0/24 の usb-net を ue1 に） | 8d6dcfe3e | 下 |
| VF2 の試験 `plan/ws221/tests/vf2-jumbo.sh` と probe `userland/tests/udp-hold`（config-vf2-tcp.mk に足した） | （この commit） | 下 |

## 試験（2026-10-29、b0166efaf の tree）

- `sh plan/ws221/tests/qemu-mtu.sh` → `qemu-mtu: PASS`（出力 build/tmp/qemu-mtu-1.txt）:
  (1) `ifconfig ue0 mtu 9000`・`mtu 1279` → 「outside 1280 to the interface's limit」rc=1、ue0 は 1500 のまま。
  (2) up のまま `ifconfig ue1 mtu 1400` → rc=0、`mtu 1400`、SSH は続く。`mtu default` → 1500。
  (3) 1400 で `ping -s 1372 10.0.3.2` → 1380 bytes × 2、`-s 1373` → `ping: sendto: Message too long`。IPv6（判定に入れない）: `ping -6 -s 1352 fec0:3::2` → 1360 bytes × 2、1353 → Message too long。
  (4) `su user -c 'ifconfig ue1 mtu 1400'` → Operation not permitted、ue1 は 1500。`sysctl net.tcpstats` の 1 行目に `jumbo_total=32 jumbo_used=0 jumbo_used_max=0 jumbo_reserve=8 jumbo_alloc_failures=0`。
- `sh plan/ws221/tests/qemu-tcp.sh build/ws221-jumbo/hdd-image.img 20` → `qemu-tcp: PASS`。
- `OUT=… sh plan/ws220/tests/tcp-model/run.sh` → `tcp-model: PASS`。
- 5 platform（`config/ci/config-{amd64,pcat,rpi4,rpi5,zybo}.mk`）の vmunix: 全部 rc=0・warning 0。

## 実機の確かめ（2026-10-29 05:52、VF2、8ea1d234c の tree の kernel。相手は 5330 の Debian、同じ switch）

- `plan/ws221/tests/vf2-jumbo.sh jumbo2` → `vf2-jumbo: PASS`（build/ws221-vf2/jumbo2/summary.txt）。board の en0 と 5330 の enx6c1ff706148a を一時 9000（終わりに両方 1500 に戻したことを script が読み戻した）:
  - `ping -s 8972 10.0.30.3` → 8980 bytes × 3。
  - 読まない UDP socket（`udp-hold`）へ 8000 byte × 10、続けて 1000 byte × 5 → `queued=7 bytes=21000 largest=8000`（jumbo は 2 つまで、小さい物は全部）。その後の pool は `jumbo_used=0 jumbo_used_max=3 jumbo_alloc_failures=0`。上限が無ければ 8000 byte が 8 つ（socket の 8 datagram の上限）入って小さい物が落ちるので、この値で上限が効いていると分かる。
  - 1 回目（jumbo1）は小さい物を 10 送って `queued=8` で FAIL にした。socket の datagram の上限 8（`SOCKET_RECEIVE_MESSAGES_MAX`）を試験が数えていなかった誤りで、試験の期待値を直した。
- `vf2-tcp.sh --kernel … --peer awe@10.0.30.3,10.0.30.3 --runs send,recv`（b0166efaf: 送り 949.1、最遅 948.1、受け 949.1/949.2、最遅 946.8。txdrop1 = 8ea1d234c: 送り 949.1、最遅 947.8、受け 948.9/949.0、最遅 945.4）。

## 残り

無し（p002 の範囲）。p001 §8.2 (5)〜(8) は p003〜p005。

## 記録

- 2026-10-29 P3: VF2 の dw-eqos で MTU 9000 の frame が実機で通った（一時の差分 build/tmp/jumbo-hack.patch で確かめた後、85a74b441 で正式に）。5330 の Debian の enx6c1ff706148a を一時 9000 にして `ping -M do -s 8972` と flood 3000/3000（終わりに 1500 に戻した、永続の設定は触っていない）。
- 5330 の RTL8156 の descriptor（Debian の `lsusb -v`）: config 2 NCM・config 3 ECM とも `wMaxSegmentSize` 1518、NCM の `bmNetworkCapabilities` 0x2b（bit 3 あり）。CDC の mode では 9000 は届かない見込み（p003 で SET_MAX_DATAGRAM_SIZE を試す）。
