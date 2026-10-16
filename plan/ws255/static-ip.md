# 試験機の zedBSD に静的な IPv4 を入れる手引き（WS255）

2026-10-16 P4（ws255-p001、q1227-i01）。起点（ユーザー）:「さらに、テスト機ではstatic IPにすることで、自律ループを維持しやすくできると考えます。」

試験機の zedBSD の試験の image だけを静的な IPv4 にする。通常の image と release の image は DHCP のまま（`userland/base/etc/net.conf` は変えない）。
Linux・Debian の側の設定は変えない。

## 1. 仕組み

`/etc/net.conf` を機械ごとの file で置き換えるだけ（AGENTS.md「試験の image の作り方」: config.mk の build と個別の file の複写）。
起動の `net startup`（`networking` の service）が interface ごとの policy を networkd に渡し、networkd は static の interface に address を付け、
DHCP を走らせない（ws254 の 1 分おきの DHCP の取り直しも static には働かない）。

**制限（2026-10-16 の QEMU の確認で分かった、未解決）**: 起動の時に入るのは **address と prefix だけ**。net.conf の `routes:`（default route）と
`dns: mode: static` の `servers:` は起動では入らない。`net startup`（`userland/base/net/main.c` の `startup_command` → `lan_send_policy`）は
interface の address だけを networkd に送り、route と DNS を入れるのは `net` の console の `commit`（`reconcile.c`）だけだから。
QEMU の guest で `route show` に default が無く、`/etc/resolv.conf` が無かった（[phase001](phase001/phase.md)）。
試験機への ssh には影響しない: build の host（10.0.10.2/16）と試験機（10.0.30.x/16）は同じ 10.0.0.0/16 の上にあり、gateway を通らない。
試験機から外（10.0.0.0/16 の外、名前の解決）へは出られない。直すのは `net startup` の側（WS255 の範囲の外、Q1 に報告済み）。
直るまで、外へ出る必要がある時は起動の後に `net defaultroute 10.0.0.1` と `net dns 1.1.1.1` を root で流す。

| 機械 | file | interface | address | gateway（起動では未適用） | DNS（起動では未適用） |
| --- | --- | --- | --- | --- | --- |
| Latitude 5330 | [tests/net-5330.conf](tests/net-5330.conf) | ue0（USB の 2.5GbE、usb-cdc-ncm） | 10.0.30.3/16 | 10.0.0.1 | 1.1.1.1 |
| RDNA 機（i5-13400F、RX 5600 XT。Debian は 10.0.10.3） | [tests/net-rdna.conf](tests/net-rdna.conf) | ue0（USB の 2.5GbE、usb-cdc-ncm） | 10.0.30.5/16 | 10.0.0.1 | 1.1.1.1 |
| QEMU の確認 | [tests/net-qemu.conf](tests/net-qemu.conf) | ue0（QEMU の usb-net） | 10.0.2.15/24 | 10.0.2.2 | 10.0.2.3 |

値の出所:

- address: 今 DHCP が渡している物（5330 は `plan/ws245/temp/5330/q1collect-p017.txt` の `wired-link name=ue0 ... address=10.0.30.3 router=10.0.0.1`（P1 の worktree）、
  RDNA 機は `plan/ws039/phase002〜006/phase.md` の「USB の zedBSD は DHCP で 10.0.30.5」（P3 の worktree））。
- interface: 両機の dmesg の `usb-cdc-ncm: ue0 link 2500 Mb/s`（5330 は P1 の `plan/ws245/temp/5330/*-dmesg.txt`、RDNA 機は P3 の `build/tmp/amdrdna-hw-*/dmesg.txt`）。
- gateway: 5330 の記録の `router=10.0.0.1`。
- prefix /16: gateway 10.0.0.1 と address 10.0.30.x が同じ subnet にあることと、同じ LAN の build の host（10.0.10.2）の `10.0.0.0/16`（`ip -4 route`）。
  **未確認**: 機械の DHCP の lease の subnet mask そのものの記録は無い。
- DNS: **未確認**。機械の lease の DNS の記録が無いので、同じ LAN の host の `/etc/resolv.conf` の `nameserver 1.1.1.1` を使った。ssh は address で入るので DNS が違っても自律の loop は止まらない。
  正しい値が分かったら file の `servers:` を直す。
- DHCP の server の pool の外か: **未確認**。今 DHCP が渡している address をそのまま固定にするので、DHCP の server がその address を別の機械に渡すと衝突する。
  router（10.0.0.1）で両機の address を予約（または pool の外に）しておくのが安全（ユーザーの操作）。

## 2. 新しく image を書く時（USB に dd する時）

image の build の command に、その機械の net.conf の複写を 1 つ足す。他は今の作り方のまま。

### 2.1 RDNA 機（P3、`plan/ws039/tests/hw-run.sh write`）

P3 の image は `plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=...`（`plan/ws039/tests/config.mk` の注記）。
`--file` を足す（後に書いた `/etc/net.conf` が test-image.sh の harness の net.conf に勝つ: Makefile の `ZEDBSD_ROOTFS_TREE_RULE` は順に `cp -f` する）:

```sh
plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=A1v \
	--file /etc/net.conf=plan/ws255/tests/net-rdna.conf
sh plan/ws039/tests/hw-run.sh write build/p3/usb/hdd-image.img /dev/sdX
```

その後の `hw-run.sh collect` は `AMDRDNA_ZED=root@10.0.30.5` のまま使える（address は DHCP の時と同じ）。
`hw-run.sh` の既定 `root@10.0.10.3` を `root@10.0.30.5` に直すのは P3 の WS の変更なので、Q1 が P3 に頼む。

### 2.2 Latitude 5330（P1、`plan/ws245/tests/5330-bootcycle.sh`）

P1 の 5330 の USB は `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk disk-image` の image を 5330 の Linux から dd した物（`plan/ws245/phase047/phase.md`）。
image を作り直す時は、同じ make に複写を足す（`ZEDBSD_TEST_IMAGE_TAG` は複写を足した image の rootfs の cache を分けるために要る、`platform/amd64/vmunix.mk` の E-127 の注記）:

```sh
make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk \
	'ZEDBSD_TEST_EXTRA_FILES=--file /etc/net.conf=plan/ws255/tests/net-5330.conf' \
	ZEDBSD_TEST_IMAGE_TAG=static5330 disk-image
```

`5330-bootcycle.sh` は `kei@10.0.30.3` に ssh するので変更は要らない。5330 の zedBSD が DHCP を取れずに止まる（exit 2）ことが無くなる。

## 3. 今の USB をそのまま静的にする時（書き直さない）

USB の root（UFS）は Linux から書けないので、zedBSD が DHCP で起きている時に一度だけ zedBSD の中で `/etc/net.conf` を置き換える。
次の起動から静的になる（今の起動の address は変わらない）。

```sh
# RDNA 機（root で入れる、harness の鍵）
scp -i plan/tmp/guest/id_ed25519 plan/ws255/tests/net-rdna.conf root@10.0.30.5:/etc/net.conf
# 5330（kei、sudo）
sshpass -p kei scp plan/ws255/tests/net-5330.conf kei@10.0.30.3:/tmp/net.conf
sshpass -p kei ssh kei@10.0.30.3 'echo kei | sudo -S cp /tmp/net.conf /etc/net.conf'
```

`5330-bootcycle.sh` は vmunix だけを差し替えるので、置き換えた `/etc/net.conf` は以後の起動でも残る。USB を dd し直したら 2 の方法か、もう一度 3 を行う。
**未実施**: 実機では試していない（5330 は T1、RDNA 機は P3 が使うので触らなかった）。

## 4. QEMU の確認

`sh plan/ws255/tests/static-qemu.sh [--no-build] [OUTDIR]`: [tests/config.mk](tests/config.mk)（SSH の guest の image）に net-qemu.conf を複写した image を build し、
ue0 の全 frame を `filter-dump` で pcap に取りながら起動する。SSH が応えること、ue0 が 10.0.2.15 であること、guest が DHCP（UDP 67 番）を 1 つも送らないこと
（[tests/pcap-dhcp.py](tests/pcap-dhcp.py)）で判定する。console・serial の log は使わない。結果は [phase001](phase001/phase.md)。
