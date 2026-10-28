<!-- awesome-plan project=zedbsd record=ws235-p004 -->

# ws235-p004 RX・TX の ring（v1 の descriptor）、1 queue・1 vector、net の worker への接続

- 親: [WS235](../ws.md)。
- 状態: in-progress（2026-10-29 07:1x P1、q1669-i02）。DHCP・ping は実機で成立（07:2x、下）、TCP（ssh・転送）は未確かめ。
- 依存: [p002](../phase002/phase.md)。実機は p003 の link と同じ枠で。

## 範囲（p001 の設計の段 3）

1. ring: v1 の descriptor（FreeBSD rge の形、RTL8127 も v1。fbsd-if_rge.c:1158-1200 の RXCFG・0xeb58 bit0・0xd8 bit1 を落とす・RTL8127 の 0x20e4 bit 0x04・
   MAC OCP 0xe00c・0xc0c2）、RX・TX とも coherent の DMA（256 byte 揃え）、RX の buffer 2 KiB と RXMAXSIZE をそれに、TX は `packet_buf_physical` →
   `drv_dma_physical_address(..., 64, ...)` で in place、届かなければ copy。
2. open の MAC の設定（rge_init の ring の address・RXCFG・TXCFG・CSI 0x70c・0x890・RSS を切る・RXQUEUE・CFG1・0xc140/0xc142・CMD の TX・RX の有効・IMR）。
3. 割り込み: ISR を返し IMR を閉じて `net_device_schedule_poll`、poll の最後に IMR を開ける。IMTYPE_NONE。link の kthread が worker を周期で起こす。
4. close: IMR を閉じ → TX・RX を止め → DMA の停止を待ち → packet を返す。
5. OpenBSD と FreeBSD の差（p001 の中 7）の採る側を記録する。

## 記録

- 2026-10-29 07:1x P1（q1669-i02）: `src/drivers/ethernet/rtl8125.c` に足した。v1 の 32 byte の descriptor（TX・RX とも 512 個、FreeBSD rge の形）、
  RX の buffer は 2 KiB の coherent を slot ごと（frame は packet に写す）、TX は `packet_buf_physical` → `drv_dma_physical_address(..., 64, ...)` で in place、
  届かない時と 60 byte 未満は slot の bounce buffer に写す（短い frame は 0 で埋める）。open は bring_up_link → ring → MAC の設定
  （FreeBSD rge_init_locked の順、station address の書き直し、RXMAXSIZE = 2048、filter は自分・broadcast・全 multicast）→ CMD の TX・RX → IMR。
  割り込みは ISR を返し、frame の bit なら IMR を閉じて poll を予約、poll の最後に IMR を開ける。service の thread は 500 ms ごとに link、
  20 ms ごとに poll を予約（割り込みの取りこぼしの保険）。close は IMR を閉じ → 受けを止め・割り込みと timer を消し・reset（DMA の停止）→ TX の packet を返す。
  ring は attach で取り、失敗の時は返す。
- OpenBSD と FreeBSD の差（p001 の中 7）の採った側:
  - MAC OCP 0xd40c: OpenBSD（MAC OCP で読み MAC OCP に書く）。FreeBSD は MAC OCP で読み PHY OCP に書く（fbsd-if_rge.c の rge_init_locked）。窓の取り違えと見て OpenBSD を採る（未確認）。
  - INT_CFG0（RTL8127、1 vector）: OpenBSD（EN・AUTO_CLEAR_IMR・AVOID_MISS を落とす）。FreeBSD は AVOID_MISS だけ。1 vector で EN を立てたままにしない。
  - 0x20e4 の bit 0x04（RTL8127）: FreeBSD（v1 の descriptor の時に落とす。OpenBSD は v2 で、この書き込みが無い）。
  - RSS_CTRL: OpenBSD の 32 bit の 0（FreeBSD は 8 bit の 0）。RADMFIFO_PROTECT: 両方とも 8 bit で 0x2001 を落とす → 下の byte の 0x01 だけ。
  - EEE の TX の idle: FreeBSD の値（JUMBO 9216 から、9240）。CSI 0x70c・0x890 は CSI の窓（FreeBSD）。
- build（2026-10-29 07:1x）: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127.mk BUILD=build/ws235/amd64-p004 build/ws235/amd64-p004/vmunix`
  → exit 0、warning 0、「amd64 vmunix check: PASS」、cksum 3648604055。`git diff --check` 0。
- 試験: `plan/ws235/tests/rtl-link.sh` に `IPTEST=1`（centris の enp94s0f1 に 192.168.127.1/24 を枠の間だけ足して消す、Q1 の OK 2026-10-29、
  zedBSD の rge0 に 192.168.127.2/24、両向きの ping、link 越しの ssh）。未実行。
- 未実施: 実機。8125・8125B・8125D・8126 の分岐は実機が無く未確かめ。

## 実機の試験（2026-10-29 07:20〜07:30 P1、RDNA 機 10.0.10.3、`plan/ws235/tests/rtl-link.sh`、lock「P1 q1669 07:20-08:05」、07:30 に返した）

| 回 | kernel | 命令 | 結果（summary.txt、worktree p1-ws235b） |
| --- | --- | --- | --- |
| 1 | p003（`build/ws235/amd64/vmunix`、cksum 3147484985） | `sh plan/ws235/tests/rtl-link.sh build/ws235/amd64/vmunix build/tmp/ws235-link-1` | rge0 が出た、ether 1c:86:0b:37:96:5d（Debian で読んだ値と同じ）、UP・RUNNING、media 2500Mbps。TX は p003 では捨てる（errors 12）。centris の enp94s0f1 は link 無し |
| 2 | p004（`build/ws235/amd64-p004/vmunix`、cksum 3648604055） | `IPTEST=1 sh ... build/tmp/ws235-link-2` | rge0 が LAN の DHCP で 10.0.30.6/16 と IPv6 の SLAAC を取った。RX 509→792、TX 13→19、errors 0。enp94s0f1 との ping・ssh は失敗（相手が違う）。centris の 192.168.127.1/24 は del し `ip -br addr` で無い事を確かめた |
| 3 | p004（同じ） | `LANTEST=1 sh ... build/tmp/ws235-link-3` | centris → 10.0.30.6 の ping 200/200、rtt avg 0.135 ms、rge0 の RX +239・TX +201。ssh 10.0.30.6 は Connection timed out（SYN-ACK が同じ subnet の ue0 から出た疑い、ue0 の TX も +61。stack の振る舞いと見る、未確認） |

- 各回の終わりに NVMe の kernel を保存した物に戻し、sha256 の照合が一致（00ddcb7045fbaca5…）、Debian に戻った。最後の状態: Debian、NVMe の kernel は P2 の物（cksum 204755418）。
- 判定は ssh の ifconfig と centris の ping・ethtool・ip で（console・serial の log は読んでいない）。
- 相手は 2.5G の switch だった（ws.md の訂正）。10G の link と RTL8127ATF・PHY の microcode の版は未確かめ。
- 回 3 の ssh の失敗の読み（2026-10-29 07:4x P1、code の読みだけ）: `src/kern/net/tcp.c` の出力の device の選び（1612〜1640 行の付近）は、bind の無い
  接続で `route_lookup_ref(remote)` の device を使い、その device の address を source にする。ue0（10.0.30.5）と rge0（10.0.30.6）が同じ 10.0.0.0/16 にあると、
  rge0 に着いた SYN の返事は route の先の device（ue0）から出うる。stack（kern/net）の multi-homing の振る舞いで、この WS の範囲の外。試験は `LANTEST=2`
  （ue0 を落とし rge0 だけ）で避ける。driver の TCP は未確かめのまま。
