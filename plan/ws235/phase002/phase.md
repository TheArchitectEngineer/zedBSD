<!-- awesome-plan project=zedbsd record=ws235-p002 -->

# ws235-p002 共通の骨格: RTL8125 系の PCI の attach・版の判定・OCP の窓・MAC address・割り込み・net device

- 親: [WS235](../ws.md)。
- 状態: in-progress（2026-10-29 06:3x P1、q1669-i01）。build まで済み、実機の attach は未実施。
- 依存: [p001](../phase001/phase.md)。

## 範囲

1. `src/drivers/ethernet/rtl8125.c`（と `include/drivers/ethernet/rtl8125.h`）: PCI の ID の表（10ec:8125・8126・8127）、BAR2 の MMIO（無ければ attach しない）、
   bus master、TXCFG の版の判定と版の表（RTL8125・8125B・8125D×2・8126×2・8127、RTL8127ATF は MAC OCP 0xd006 で）、MAC・PHY の OCP の窓の読み書き（CSI・EPHY は使う p003 で）、
   OOB からの抜け、reset、MAC address（MMIO 0x19e0・0x19e4 と 0x00）、1 vector の割り込み（MSI-X か MSI）、net device の登録（link は down のまま、送受は p004）。
2. build: `CONFIG_DRIVER_PCI_RTL8125`（`config/drivers/pci.drivers`・`menu.list` の Ethernet、Makefile の既定 n と `-D`、`platform/amd64/vmunix.mk`、`src/kern/platform/pcat.c` の登録）。
3. 試験の config: `plan/ws235/tests/config-amd64-rtl8127.mk`。
4. 確かめ: build（warning 0）。実機の attach の確かめ（版・MAC address の log ではなく、ssh で interface と MAC を読む）は p003 の実機の枠とまとめる（Q1 の割当）。

## 記録

- 2026-10-29 06:4x P1: `src/drivers/ethernet/rtl8125.c`・`include/drivers/ethernet/rtl8125.h` を作った。中身: PCI の ID（10ec:8125・8126・8127）、D0、BAR2 の claim・
  map（NOCACHE）・memory・bus master、TXCFG の版の表（OpenBSD rge 1.48 の 7 版）と RTL8127ATF の判定（MAC OCP 0xd006）、MAC・PHY の OCP の窓、PCIe の LCSR の
  ASPM・ECPM を切る、IMR を閉じ ISR を返す、OOB からの抜け（RealWoW → reset → OOB の bit → TWICMD → handoff → UPS の後始末）、永久の MAC address（0x19e0・0x19e4）を
  station の register へ、1 vector（MSI-X → MSI → pin）、handler は ISR を返すだけ、net device `rge<n>`（carrier 無し、transmit は捨てて ENETDOWN、poll は 0）、
  失敗の時の巻き戻し（unmap・enable の状態・BAR）。出典は block ごとに「Values referenced:」「Procedure referenced:」（OpenBSD rge）。
- 配線: `config/drivers/pci.drivers`（既定 n）・`config/drivers/menu.list` の Ethernet・Makefile の `?= n` と `-D`・`platform/amd64/vmunix.mk`・`src/kern/platform/pcat.c`。
  試験の config `plan/ws235/tests/config-amd64-rtl8127.mk`（config-amd64.mk ＋ `CONFIG_DRIVER_PCI_RTL8125 := y`）。
- build（2026-10-29 06:4x、`build/` の toolchain は main の checkout への symlink）:
  - `make ZEDBSD_CONFIG=plan/ws235/tests/config-amd64-rtl8127.mk BUILD=build/ws235/amd64 build/ws235/amd64/vmunix` → exit 0、`warning:` 0（log `build/tmp/ws235-build2.log`）。
  - `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws235/amd64-def build/ws235/amd64-def/vmunix`（driver 無し）→ exit 0、warning 0。
  - `make ZEDBSD_CONFIG=config/ci/config-pcat.mk BUILD=build/ws235/pcat build/ws235/pcat/vmunix` → exit 0、warning 0、「PC/AT vmunix contract: OK」。
- 未実施: 実機（10.0.10.3）での attach の確かめ（版・ATF・MAC address を ssh で読む。log では判定しない）。p003 の実機の枠とまとめる。host 試験は無い（hardware の窓だけの code）。

## 実機の試験（2026-10-29 07:20〜07:30 P1、RDNA 機 10.0.10.3、`plan/ws235/tests/rtl-link.sh`、lock「P1 q1669 07:20-08:05」、07:30 に返した）

| 回 | kernel | 命令 | 結果（summary.txt、worktree p1-ws235b） |
| --- | --- | --- | --- |
| 1 | p003（`build/ws235/amd64/vmunix`、cksum 3147484985） | `sh plan/ws235/tests/rtl-link.sh build/ws235/amd64/vmunix build/tmp/ws235-link-1` | rge0 が出た、ether 1c:86:0b:37:96:5d（Debian で読んだ値と同じ）、UP・RUNNING、media 2500Mbps。TX は p003 では捨てる（errors 12）。centris の enp94s0f1 は link 無し |
| 2 | p004（`build/ws235/amd64-p004/vmunix`、cksum 3648604055） | `IPTEST=1 sh ... build/tmp/ws235-link-2` | rge0 が LAN の DHCP で 10.0.30.6/16 と IPv6 の SLAAC を取った。RX 509→792、TX 13→19、errors 0。enp94s0f1 との ping・ssh は失敗（相手が違う）。centris の 192.168.127.1/24 は del し `ip -br addr` で無い事を確かめた |
| 3 | p004（同じ） | `LANTEST=1 sh ... build/tmp/ws235-link-3` | centris → 10.0.30.6 の ping 200/200、rtt avg 0.135 ms、rge0 の RX +239・TX +201。ssh 10.0.30.6 は Connection timed out（SYN-ACK が同じ subnet の ue0 から出た疑い、ue0 の TX も +61。stack の振る舞いと見る、未確認） |

- 各回の終わりに NVMe の kernel を保存した物に戻し、sha256 の照合が一致（00ddcb7045fbaca5…）、Debian に戻った。最後の状態: Debian、NVMe の kernel は P2 の物（cksum 204755418）。
- 判定は ssh の ifconfig と centris の ping・ethtool・ip で（console・serial の log は読んでいない）。
- 相手は 2.5G の switch だった（ws.md の訂正）。10G の link と RTL8127ATF・PHY の microcode の版は未確かめ。
