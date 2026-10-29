<!-- awesome-plan project=zedbsd record=ws235-p003 -->

# ws235-p003 MAC・PHY の初期化と link（firmware 無しから）

- 親: [WS235](../ws.md)。
- 状態: cleared（2026-10-29 09:1x P1、q1669-i03。下の「判定」）。
- 依存: [p002](../phase002/phase.md)。

## 範囲

open のたびに: OOB からの抜けと reset → PHY の電源 → MAC の設定（CLKREQ・UPS を切る、MAC の MCU を止めたまま、CSI 0x108）→ 割り込み・timer を閉じ reset →
EPHY の表（RTL8127）→ PHY の reset（広告を消す）→ PHY の microcode の版を読む → RTL8127 の PHY の表 → EEE・advanced EEE を切る → 全部の速さを広告して交渉。
link は kthread が 500 ms ごとに PHYSTAT を読み、carrier と `link_mbps` を出す。firmware（MAC・PHY の microcode の patch）は入れない（p001 の 3、判断待ち）。

## 記録

- 2026-10-29 06:4x P1: `src/drivers/ethernet/rtl8125.c` に足した（commit cad42f821）。RTL8127 の PHY の表は OpenBSD rge 1.48 の `rge_phy_config_mac_r27`
  （obsd-if_rge.c:2616-2899、`rge_phy_config_mcu` を除く）を scratchpad の変換の script で (write・modify・read, reg, mask, value) の 239 段の表にした。
  数の照合: 元の呼び出し 202（loop の本体 5 を引き、loop の展開 12・12・18 を足す）= 239 で一致。EPHY の表は `mac_r27_ephy` の 36 個。
- 疑い（未確認）: OpenBSD の EPHY の窓の address の mask は 0x7f（`RGE_EPHYAR_ADDR_MASK`）で、RTL8127 の表の offset（例 0x088・0x188）は 0x7f で切ると同じ 0x08 に
  なる。OpenBSD のまま写した。FreeBSD の hw の file（未取得）か実機の振る舞いで確かめる。
  - 2026-10-29 08:3x P1: FreeBSD の if_rgereg.h（`RGE_EPHYAR_ADDR_MASK 0x0000007f`）と if_rge_hw.c の EPHY の読み書きも 0x7f で切る（p1-ws235b の
    `build/tmp/ws235-fbsd-070331`、p002 の時に取った物）。両 BSD で同じ。0x80 以上の offset が窓のどこに当たるかは未確認のまま（2.5G の link は成立している）。
- build: `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127.mk BUILD=build/ws235/amd64 build/ws235/amd64/vmunix` → exit 0、warning 0（`build/tmp/ws235-build3.log`）。
- 実機の試験の script: `plan/ws235/tests/rtl-link.sh VMUNIX OUT`（NVMe の kernel を保存 → 試験の kernel → BootNext → ssh で `ifconfig rge0`・`ifconfig rge0 up`・
  5〜65 s の間の `ifconfig rge0`、centris の `ethtool enp94s0f1` → reboot で Debian → 保存した kernel を戻す → `OUT/summary.txt`）。未実行。

## 未実施

- 実機（RDNA 機の枠が要る）。OpenBSD と FreeBSD の差の表（p001 の残り、中 7）。

## 実機の試験（2026-10-29 07:20〜07:30 P1、RDNA 機 10.0.10.3、`plan/ws235/tests/rtl-link.sh`、lock「P1 q1669 07:20-08:05」、07:30 に返した）

| 回 | kernel | 命令 | 結果（summary.txt、worktree p1-ws235b） |
| --- | --- | --- | --- |
| 1 | p003（`build/ws235/amd64/vmunix`、cksum 3147484985） | `sh plan/ws235/tests/rtl-link.sh build/ws235/amd64/vmunix build/tmp/ws235-link-1` | rge0 が出た、ether 1c:86:0b:37:96:5d（Debian で読んだ値と同じ）、UP・RUNNING、media 2500Mbps。TX は p003 では捨てる（errors 12）。centris の enp94s0f1 は link 無し |
| 2 | p004（`build/ws235/amd64-p004/vmunix`、cksum 3648604055） | `IPTEST=1 sh ... build/tmp/ws235-link-2` | rge0 が LAN の DHCP で 10.0.30.6/16 と IPv6 の SLAAC を取った。RX 509→792、TX 13→19、errors 0。enp94s0f1 との ping・ssh は失敗（相手が違う）。centris の 192.168.127.1/24 は del し `ip -br addr` で無い事を確かめた |
| 3 | p004（同じ） | `LANTEST=1 sh ... build/tmp/ws235-link-3` | centris → 10.0.30.6 の ping 200/200、rtt avg 0.135 ms、rge0 の RX +239・TX +201。ssh 10.0.30.6 は Connection timed out（SYN-ACK が同じ subnet の ue0 から出た疑い、ue0 の TX も +61。stack の振る舞いと見る、未確認） |

- 各回の終わりに NVMe の kernel を保存した物に戻し、sha256 の照合が一致（00ddcb7045fbaca5…）、Debian に戻った。最後の状態: Debian、NVMe の kernel は P2 の物（cksum 204755418）。
- 判定は ssh の ifconfig と centris の ping・ethtool・ip で（console・serial の log は読んでいない）。
- 相手は 2.5G の switch だった（ws.md の訂正）。10G の link と RTL8127ATF・PHY の microcode の版は未確かめ。

## 実機の試験（2026-10-29 08:57〜09:10 P1、q1669-i03、RDNA 機 10.0.10.3、RTL8127 は 10GbE の switch、lock「P1 q1669-i03 start 08:57 end 09:42」、09:10 に返した）

| 回 | kernel | 命令 | 結果（summary.txt、worktree p1-ws235c） |
| --- | --- | --- | --- |
| 4 | `build/ws235/amd64-c/vmunix`（main 7b297b46a、cksum 4251467738、sha256 8715652c0d0fc359…） | `POWERCYCLE=1 LANTEST=2 PERF=build/ws235/amd64-c/bin/tcp-perf sh plan/ws235/tests/rtl-link.sh build/ws235/amd64-c/vmunix build/tmp/ws235-link-4` | rge0 media **10000Mbps**、DHCP 10.0.30.6/16。ue0 を down・その connected route を消した後（route show で rge0 の 10.0.0.0/16 だけ）、centris → 10.0.30.6 の ping 200/200（avg 0.114 ms）、ssh 成立、SSH で 100 MB 両向き（zedBSD→ 0.90 s、→zedBSD 1.34 s）、tcp-perf 10 s: send 2983・recv 3284 Mbit/s（perf-peer の側も同じ）。rge0 の errors 0・dropped 0（RX 3018025・TX 4551366 packets）。reboot も rge0 の address で通った |
| 5 | 同じ | 同じ＋`SECONDS_PERF=20`、出力 `build/tmp/ws235-link-5` | media 10000Mbps、ping 200/200（avg 0.096 ms）、ssh・100 MB 両向き成立、tcp-perf 20 s: **send 3539・recv 3373 Mbit/s**（毎秒の値 send 3228〜3980、recv 3213〜3553）、errors 0 |

- 各回の終わりに NVMe の kernel を保存した物（P2 の vmunix-va1、sha256 6a06e796a7b9b632…）に戻し照合が一致、Debian に戻った。電源の入れ直しは無し。
- 判定は ssh の ifconfig・route show、centris の ping・ssh・perf-peer.py で（console・serial の log は読んでいない）。
- 前の回 3 の ssh の失敗は stack の出口の選び（閉じた device の route が残り、route の device とその address で送る）で、`rtl-link.sh` の LANTEST=2 は ue0 の connected route も消す（1918d909f）。

## 判定（2026-10-29 09:1x P1、q1669-i03）

- cleared。firmware（MAC・PHY の microcode の patch）無しで、2.5G の switch と 2500Mbps（回 1〜3）、10GbE の switch と **10000Mbps**（回 4・5）の link が auto-negotiation で上がり、
  carrier と media を ifconfig で読んだ。同じ port で Linux（backports 7.2.11、r8169）も「Link is Up - 10Gbps/Full」（[p005](../phase005/phase.md)）。
- 相手の側の報告: 相手は switch で、その port の速さは読めない（未確かめ）。centris の enp94s0f1 との直結は今の配線に無い。
- 未確かめ: PHY の microcode の版の値、EPHY の窓の 0x80 以上の offset（上の疑い）。
