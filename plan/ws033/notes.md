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
