<!-- awesome-plan project=zedbsd record=ws235-p005 -->

# ws235-p005 実機（共通の段）: link・IPv4・ping・ssh・TCP の転送の計測と、同じ機械の Linux の基準

- 親: [WS235](../ws.md)。
- 状態: cleared（2026-10-29 09:1x P1、q1669-i03。下の「判定」）。
- 依存: [p003](../phase003/phase.md)・[p004](../phase004/phase.md)。

## 範囲

RDNA 機（10.0.10.3）の RTL8127 を 10GbE の switch に繋いだ経路で、zedBSD の rge0 だけを通る link・IPv4・ping・ssh・TCP の転送（tcp-perf の両向き）を測る。
同じ機械・同じ port・同じ tcp-perf と相手（centris の perf-peer.py、10.0.10.2、enp94s0f0 10G）で Linux の基準を取る（p001 の判断 5: trixie-backports の kernel と
firmware-realtek、起動の既定は今の kernel のまま、計測の時だけ backports で起動）。

## 道具

- `plan/ws235/tests/rtl-link.sh`: `LANTEST=2`（ue0 を down・その connected route を消し、rge0 の address で ping・ssh・SSH の 100 MB 両向き・reboot）、
  `PERF=<zedBSD 用の tcp-perf>`（ssh で /tmp に写し、`SECONDS_PERF` 秒ずつ send・recv）、`POWERCYCLE=1`（Debian が戻らない時に plug を一度だけ入れ直す。lock を持つ時だけ）。
- `plan/ws235/tests/config-amd64-rtl8127-perf.mk`: tcp-perf の build（`make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127-perf.mk BUILD=build/ws235/amd64-c build/ws235/amd64-c/bin/tcp-perf`、exit 0・warning 0）。
- `plan/ws235/tests/linux-base.sh survey|install|measure OUT`: Linux の基準（survey は読みだけ、install は backports の導入と GRUB の既定の固定、
  measure は grub-reboot で一度だけ最新の kernel、RTL8127 の port に DHCP の lease の 10.0.30.6/16 を一時に付け、arp_ignore=1・arp_announce=2、centris への host route、
  tcp-perf の両向き、既定の kernel に戻す。Debian に ethtool・dhclient が無いので sysfs と静的な一時の address）。
- host 試験: Linux の tcp-perf と perf-peer.py の loopback で両向き（2026-10-29 08:2x、send 31817・recv 33152 Mbit/s、道具の確かめだけ）。

## 記録（2026-10-29 08:57〜09:10 P1、lock「P1 q1669-i03 start 08:57 end 09:42」、09:10 に返した）

| 回 | 対象 | 命令（出力は worktree p1-ws235c の `build/tmp/`） | 結果 |
| --- | --- | --- | --- |
| zedBSD 4 | `build/ws235/amd64-c/vmunix`（cksum 4251467738） | `POWERCYCLE=1 LANTEST=2 PERF=build/ws235/amd64-c/bin/tcp-perf sh plan/ws235/tests/rtl-link.sh build/ws235/amd64-c/vmunix build/tmp/ws235-link-4` | media 10000Mbps、ping 200/200、ssh・100 MB 両向き成立、tcp-perf 10 s: send 2983・recv 3284 Mbit/s、errors 0 |
| zedBSD 5 | 同じ | 同じ＋`SECONDS_PERF=20`（`ws235-link-5`） | tcp-perf 20 s: **send 3539・recv 3373 Mbit/s**（毎秒 3228〜3980・3213〜3553）、errors 0 |
| Linux 1 | Debian 6.12.111（読みだけ） | `sh plan/ws235/tests/linux-base.sh survey build/tmp/ws235-linux-1` | backports の source 無し、GRUB_DEFAULT=0、6.12 に RTL8127 の driver 無し、PCIe の LnkCap 16GT/s x1、**LnkSta 8GT/s（downgraded）x1** |
| Linux 2 | 導入 | `sh plan/ws235/tests/linux-base.sh install build/tmp/ws235-linux-2` | `/etc/apt/sources.list.d/trixie-backports.list` を足し、linux-image-7.2.11+deb13-amd64（7.2.11-1~bpo13+1）と firmware-realtek 20260916-1~bpo13+1 を導入。GRUB_DEFAULT=saved、saved_entry は 6.12.111 |
| Linux 3 | backports 7.2.11 | `sh plan/ws235/tests/linux-base.sh measure build/tmp/ws235-linux-3` | 7.2.11 で起動し enp3s0（r8169）が出た。ethtool・dhclient が無く address 無しで計測せず（script を直した、01f425b07） |
| Linux 4 | backports 7.2.11 | `sh plan/ws235/tests/linux-base.sh measure build/tmp/ws235-linux-4` | r8169「Link is Up - 10Gbps/Full」、tcp-perf 10 s: **send 6808・recv 5458 Mbit/s**（毎秒 6793〜6824・5341〜5554）。enp3s0 の counter が RX 7.16 GB・TX 8.54 GB 増え、USB の port は増えず。RX の missed 44405。既定の 6.12.111 に戻った |

- 最後の状態: Debian 6.12.111（既定、`grub-editenv list` の saved_entry が 6.12.111・next_entry 空）、RTX は vfio-pci、NVMe の zedBSD の kernel は P2 の物（sha256 6a06e796a7b9b632…、照合一致）。
  機械に残した変更: trixie-backports の source、backports の kernel と firmware-realtek、`GRUB_DEFAULT=saved`。電源の入れ直しは無し。
- 判定は ssh の ifconfig・route show・ip・sysfs と、centris の ping・ssh・perf-peer.py で（console・serial の log は読んでいない）。

## 判定（2026-10-29 09:1x P1）

- cleared。共通の段（1 queue・1 vector・offload 無し）で、10G の link・IPv4（DHCP）・ping・ssh・TCP の両向きが rge0 だけで通り、tcp-perf の値と同じ機械の Linux の基準を取った。
- zedBSD は Linux の約 52%（send）・62%（recv）。Linux は TSO・GSO（TX の packet の数が 309381 で 8.5 GB、大きな segment）と GRO・NAPI を使う。差を driver と stack に分ける
  profile（判断 3 の「driver が律速でない事」）は未実施で、p006（多 vector の MSI-X・RSS・割り込みの抑え・RX の zero copy）の最初に取る。
- PCIe の link は 8GT/s x1（downgraded）で、Linux の send 6.8 Gbit/s がユーザーの「7Gbit/s くらい」に合う。
- 未実施: 相手の側（switch）の port の速さの読み、jumbo frame、長時間の転送、close の後の再 open。
