# ws255-p001 試験の image の静的な IP

Status: in-progress（2026-10-18 Q1: Status の行が無かったので写した。元: in-progress（q1227、QEMU PASS、gateway・DNS の依存で Q1 の判定待ち））

- 親: [WS255](../ws.md)、Queue: q1227-i01（P4）、状態: in-progress（条件 2・3 は済み。条件 1 の gateway・DNS が起動で入らない。計画に無い依存として Q1 に報告、判定待ち）
- 範囲: WS255 の完了の条件 1〜3。
- ユーザー（2026-10-16）:「さらに、テスト機ではstatic IPにすることで、自律ループを維持しやすくできると考えます。」

## 結果（2026-10-16 P4）

- 手段: 試験の image の build に `--file /etc/net.conf=plan/ws255/tests/net-<machine>.conf` を足すだけ（config.mk の build ＋個別の file の複写）。通常・release の image（`userland/base/etc/net.conf`）は DHCP のまま、変えていない。
- 値と手引き: [static-ip.md](../static-ip.md)。5330 は `ue0` 10.0.30.3/16、RDNA 機は `ue0` 10.0.30.5/16。gateway は 10.0.0.1、DNS は 1.1.1.1。DNS の値と /16 の prefix は記録が無く、**未確認**（出所は手引き）。
  P1 の `5330-bootcycle.sh`・P3 の `hw-run.sh` への入れ方（image を書く時の build の引数と、今の USB の `/etc/net.conf` を一度だけ置き換える方法）は手引きの 2・3。他の WS の script は変えていない。
- 実機には触っていない（5330 は T1、RDNA 機は P3）。

### 計画に無い依存（Q1 に報告）

起動で入るのは address と prefix だけで、net.conf の `routes:`（default route）と `dns: mode: static` の `servers:` は入らない。
`net startup`（`userland/base/net/main.c` の `startup_command` → `lan_send_policy`）は interface の address だけを networkd に送り、
route と DNS は `net` の console の `commit`（`userland/base/net/reconcile.c`）でしか入らない。これは試験の image に限らず、static の route・DNS を
`commit` した利用者の機械でも再起動の後に default route と resolver が無くなる、ということ（**推測**: 実機では未確認、QEMU の起動では観測した）。
試験機への ssh には影響しない（host 10.0.10.2/16 と試験機 10.0.30.x/16 は同じ subnet）。外へ出るには起動の後に `net defaultroute 10.0.0.1`・`net dns 1.1.1.1`（QEMU で効くことを確かめた）。
直すには `net startup`（または networkd が static の interface を configure した後）で route と DNS を入れる変更が要る。USB の NIC は起動の後に現れるので、
address が付く前の `route add default` は失敗しうる。どこで入れるかは設計の判断なので、WS255 の範囲では直していない。

## 確認

| 確認 | 結果 |
| --- | --- |
| **QEMU**: `sh plan/ws255/tests/static-qemu.sh`（image は `plan/tools/guest/test-image.sh plan/ws255/tests/config.mk build/amd64 --file /etc/net.conf=plan/ws255/tests/net-qemu.conf ZEDBSD_TEST_IMAGE_TAG=ws255`、tree は 877005852 と同じ source） | **PASS**（`WS255-Q001 static-qemu: PASS`、ssh=y、static=y、dhcp-frames=0）。2 回目（`--no-build`、script の route の読み方を直した後）も PASS、`default-route=n resolver=n` |
| DHCP を送らないこと | ue0 の全 frame を QEMU の `filter-dump` で pcap に取り（400 frame）、UDP 67 番宛ては 0（`plan/ws255/tests/pcap-dhcp.py`）。QEMU の user network は DHCP の server を常に持つので、「guest が DHCP を使わない」で確かめた |
| guest の記録（ssh） | `ifconfig ue0`: `inet 10.0.2.15 netmask 255.255.255.0`。`net show`: `ue0 static online`。`route show`: default 無し。`/etc/resolv.conf` 無し。`/var/log/messages` に networkd・dhcpc の行無し |
| 回避の確認（ssh、手で） | `net defaultroute 10.0.2.2`・`net dns 10.0.2.3` は rc=0、`route show` に `default 10.0.2.2 UG ue0`、resolv.conf に `nameserver 10.0.2.3` |

証拠: worktree `/home/awe/zedBSD-worktrees/p4/build/ws255/static-qemu.run.Oo28hU/`（1 回目）・`static-qemu.run.evySpX/`（2 回目）（result.txt・ifconfig-ue0.txt・routes.txt・net.conf.txt・ue0.pcap・screen.png。commit しない）。
console・serial の log は使っていない。

未実施: 実機（5330・RDNA 機）での静的な起動。DHCP の server（router 10.0.0.1）での address の予約。

## 試験の file

`plan/ws255/tests/`: `net-5330.conf`・`net-rdna.conf`（機械）、`net-qemu.conf`・`config.mk`・`static-qemu.sh`・`pcap-dhcp.py`（QEMU）。

## 残り

- 条件 1 の gateway・DNS: 上の依存の修正（Q1 が Queue を作るか判断）。
- 手引きの P1・P3 の script への反映は Q1 から各担当へ。
