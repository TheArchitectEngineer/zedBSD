# WS033: networking サービスと有線インタフェースの管理

## 監視と通知の方法

**新しいカーネルインタフェースは要らなかった。** 既にある。

カーネルは装置ごとに carrier を持ち（`net_device_set_carrier`）、変化を route socket に
`RTM_IFINFO` として流す。遷移は `RTM_IFINFO_CARRIER_UP` / `CARRIER_DOWN` / `REMOVAL`、
取りこぼしたときは `RTM_IFINFO_F_OVERFLOW` が立ち、読み手に全部読み直せと言う。
networkd は**既にこの socket を開いて poll している**——managed-wlan のために。有線側は
同じイベントの二人目の読み手になるだけである。

ただし carrier の意味はドライバで違う:

| ドライバ | carrier が意味するもの |
| --- | --- |
| CDC ECM / NCM | 本物のリンク（`NETWORK_CONNECTION` 通知） |
| rtl8822bu, intel-ax211 | association |
| dp8390 (NE2000) | **open したかどうか**。この石にリンク検出は無い |

つまり「ケーブルが抜けた」は、ハードウェアが言える装置でだけ言える。言えない装置は
開いている間ずっと繋がって見える。これは実装の限界ではなく装置の限界である。

## 決めたこと

判断と実行を分けた。`managed-lan.c` は**装置を開かないしコマンドも起動しない**。
設定と carrier を持ち、次に何をすべきかを答えるだけ。managed-wlan と同じ作りで、
そうすると判断そのものをホストで試せる。

| 状態 | 意味 |
| --- | --- |
| IDLE | ケーブルが無いか、管理していない |
| PENDING | ケーブルがあり、まだ構成していない |
| CONFIGURED | 構成済みで、到達可能なアドレスを持つ |
| UNCONFIGURED | ケーブルはあるが構成できなかった。リンクローカルを持つ |

UNCONFIGURED は**ケーブルが動くまで再試行しない**。応答しなかったサーバは、繰り返し
聞いても早く応答しはしない。

## net.conf を読むのは誰か

networkd は net.conf を開かない（元からそういう作りで、ソースにもそう書いてある)。
そこで **`net lan enable` が設定を読んで渡す**。インタフェースごとに一行:

```
eth0 dhcp 10
eth1 static 192.0.2.10 255.255.255.0
eth2 disabled
```

ファイルの読み手と書き手が一つずつのまま保たれる。

ループバックだけは別扱いで、`net lan enable` がその場で構成する。**抜き差しできない
ものに監視するものは無い**。`net boot` を消したとき、ここを忘れて lo0 が構成されなく
なり、`ssh root@127.0.0.1` が落ちた。実機で見つけて直した。

## DHCP が取れなかったとき

MAC の下位2バイトから 169.254.x.y を導く。`169.254.0.0/24` と `169.254.255.0/24` は
予約なので、第3バイトが 0 なら 1 に、255 なら 254 に寄せる。ARP probe は行わない。

同じ機械は毎回同じアドレスになる。**構成できなかった機械が、少なくとも二度同じ場所に
居る**ということである。

そして**リンクローカルは有効なアドレスではない**。`--wait` はこれでは終わらない。

## コマンド

| コマンド | 動き |
| --- | --- |
| `net lan enable` | 設定を渡して管理を始める。すぐ返る |
| `net lan disable` | 管理をやめる。インタフェースはそのまま |
| `net startup` | 起動時の一式。ループバック、有線、無線、必要なら待つ |

`disable` はインタフェースを落とさない。**判断をやめるだけ**で、通信しているものは
通信し続ける。落とすのは `net down` の仕事である。

`net wifi` には手を入れていない。

### 待つのは startup であって lan でも wifi でもない

最初 `net lan enable --wait` にしたが、これは間違いだった。`net lan` と `net wifi` が
それぞれ `--wait` を持つ形になり、**同じ問いを二箇所で答えることになる**。

起動が待っているのは「有線が構成されたか」でも「無線が繋がったか」でもなく、
**どれか一つが到達可能なアドレスを持ったか**である。どちらか片方の命令には答えられない
問いなので、両方を呼ぶ `net startup` に置いた。rc.conf の `networking.wait` を読むのも
そこである。

## サービス

```
/etc/service.d/networking   oneshot
  command=/sbin/net
  arguments=startup
  after=syslogd,networkd
  requires=networkd
```

最初はシェルスクリプト `/usr/libexec/networking-start` を置いて二つの命令を並べていたが、
`net startup` が全部やるようになったのでスクリプトは何も持たなくなり、削除した。
そのために入れたパッケージ DATA のモード欄も**元に戻した**——使う人が居なくなった
一般化を残しておく理由が無い。

`/etc/service.d/net`（`net boot`）は削除し、`ntpdate`, `cron`, `getty_console`, `sshd`
の依存を `networking` に付け替えた。

## 途中で直したもの

| 症状 | 原因 |
| --- | --- |
| `init: cannot load /etc/rc.conf: Invalid argument` | `setting_allowed()` が `ntpdate.servers` だけを許す固定表だった。表にして `networking.wait` を足した |
| `networking-start` が 0644 | パッケージの DATA は全て 0644 固定だった。スクリプトを消したので、この件ごと無くなった |
| lo0 unconfigured | 上記のとおり |

## 確認

`make managed-lan-host-test` — 31項目。ケーブルの抜き差し、取りこぼし、装置の消失、
DHCP 失敗、無効化、リンクローカルの導出。判断だけの部品なので、ホストで全部試せる。

実機 `plan/ws033/tests/networking-target.sh`:

```
service status networking → completed
lo0 static online / LOOPBACK-OK
LAN-DISABLE-OK / LAN-ENABLE-OK / STARTUP-OK
net lan enable --wait → usage   （もう無い）
```

## 残っているもの

- QEMU の試験機に有線 NIC が無いので、**ケーブルの抜き差しは実機では試せていない**。
  判断の側はホストで試してある。CDC ECM の試験装置（ws004）があるので、そこで
  carrier を動かす試験は書ける。
- dp8390 はリンクを検出しないので、この装置では抜線を検出できない。

## ホストからゲストへの SSH（USB 起動・USB LAN）

`plan/ws033/tests/ssh-host-to-guest.sh`

既存の OpenSSH 試験はゲストがループバックで自分自身に繋ぐので、**プロトコルは試すが
ネットワークは試さない**——機械から何も出ていかない。これは USB メモリからイメージを
起動し、同じ xHCI のもう一方のポートに USB Ethernet を挿して、**外から**繋ぐ。

```
-machine q35 -cpu max
-device qemu-xhci,id=xhci
-device usb-storage,bus=xhci.0,port=1,...,bootindex=1
-device usb-net,bus=xhci.0,port=2,...,msos-desc=on
-netdev user,...,hostfwd=tcp:127.0.0.1:<空き>-:22
```

QEMU の `usb-net` は RNDIS を第一、CDC Ethernet を第二の構成として出す。ゲストに RNDIS は
無いので第二を選ぶ——ws004 の HW-T22 が押さえている振る舞いで、`ue0` が生える。

```
usb-storage: sda blocks=624640 block-size=512 cache=write-back
usb0: device 2 port 6 0525:a4a2 class 02 configuration=1 configured
usb-cdc-ecm: ue0 mac=52:54:00:33:00:01 segment=1514
guest interface: ue0 static online
guest said: zedBSD zedbsd 0.0.1 zedBSD 0.0.1 x86_64
SSH-HOST-TO-GUEST-VERIFIED
```

`ssh -tt` なので端末が付き、ゲストは擬似端末を割り当てる。**ログインが通る経路**であって、
素のコマンド実行の経路ではない。二回走らせて二回とも通った。

鍵はホストで作り、公開鍵をコンソール越しにゲストへ打ち込む。打ち込んだ先で
`wc -l` して**一行あることを確かめてから**繋ぐ。壊れた鍵は拒否されたログインに見え、
原因追及を別の方向へ送り込むからである。

### この試験で見つかったこと

**1. DHCP クライアントがイメージに入っていない。**

パッケージ名は `dhcpc` だが、`config.mk` の `ZEDBSD_USER_PROGRAMS` は **`dhcpcd`** と
書いてある。そんなパッケージは無いので**黙って何も選ばれず**、`/sbin/dhcpc` が作られない。
networkd は `/sbin/dhcpc` を起動しようとするので、`net dhcp` はどのイメージでも失敗する。

```
$ make list-user-programs | grep ^dhcp
dhcpc|dhcpc|*|y|base||
$ ls build/amd64/rootfs/sbin/ | grep dhcp
（何も無い）
```

`config.mk` は git 管理外の生成物なので、この試験用の config では `dhcpc` に直した。
**追跡されているファイルは直していない**——menuconfig が書くのは登録名なので、この
`dhcpcd` は手で書かれたか、改名前の名残である。どう直すかは決めてほしい。

**2. 乱数が RDRAND に依存している。**

`-cpu max` を付けないと `ssh-keygen` が `PRNG is not seeded` で鍵を作れず、sshd が
`restart=on-failure` で回り続ける。`/dev/urandom` も `/dev/random` も無いので、
OpenSSL には他に種が無い。既存のゲスト試験は `-cpu max` を付けているので表面化して
いなかった。**RDRAND の無い実機では同じことが起きる。**

### 試験自身の直し

`exec python3` にしていたので**シェルごと置き換わって trap が消え**、QEMU が誰にも
止められずに残っていた。`exec` をやめた。ポートも固定をやめ、空きを取るようにした——
前の実行が終わりきっていないだけで落ちる試験は、対象について何も語らない。
