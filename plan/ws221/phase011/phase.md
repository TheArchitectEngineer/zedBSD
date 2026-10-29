<!-- awesome-plan project=zedbsd record=ws221-p011 -->
# ws221-p011: IPv6 の TCP の試験と直し

Status: cleared（2026-10-29 P3 q1664-i05: 0342c9d89 まで。IPv6 の TCP の 4 つの不具合を直し、VF2 実機で IPv6 の受け 936.3（理論 936.3）・送り 894〜932・jumbo 988.5/985.3・PTB の追従、QEMU inet,inet6 PASS、tcp-model PASS、5 platform の build warning 0。未実施は下の「未実施」）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i05（P3）
依存: p002〜p009

## 由来

2026-10-29 ユーザーの指示（原文）:「IPv6はほとんどテストされていない現実がありますので、TCP改善で一緒にテストして直してください。」→ Q1: IPv6 の tcp-perf、VF2 と 5330・centris の実機、QEMU、jumbo の IPv6、Packet Too Big（PTB）の追従の実地、見つけた不具合をこの WS で直す。Future Work の F-WS221-ipv6-ptb をここに取り込む。

## 道具（plan/ws221/tests/ と userland/tests/tcp-perf）

| 道具 | 足したこと |
| --- | --- |
| `userland/tests/tcp-perf` | IPv6 の address（link-local は `%IF`）。`serve ADDRESS PORT SECONDS`（受け身の側を測る: listen して 1 本を受け、相手の向きの byte で送るか受ける）。同じ source を Linux で build して相手に使う |
| `perf-peer.py` | IPv6 の address で listen（getaddrinfo） |
| `qemu-tcp.sh` | 3 番目の引数 FAMILIES（`inet,inet6`）。IPv6 は slirp の `fec0::2`（host の `::1`） |
| `vf2-tcp.sh` | `--address`（この host の IPv6 で測る）、`--ptb AT,MTU[/AT,MTU…]`（相手が板に PTB を送る、`ptb6.py`）、`serve-send`・`serve-recv` の run、run ごとの板の接続の send_mss・再送の行、電源の入れ直しの後に /tmp の印で本当に再起動したかを確かめる（2026-10-29: 最初の cycle で板が落ちず古い kernel のまま測っていた） |
| `ptb6.py`（新） | raw の ICMPv6 で PTB を 1 つ送る（中の packet は板 → 相手の TCP、ports つき）。root で |
| `vf2-jumbo.sh` | IPv6 の jumbo の ping（8952 の往復、8953 は送らない、5330 からの `ping -6 -M do -s 8952`） |

## 見つけた不具合と直し

| # | 症状（直す前） | 原因 | 直し | commit |
| --- | --- | --- | --- | --- |
| 1 | VF2 ↔ 5330（MTU 1500）の IPv6 の TCP が送り 613・受け 821 Mbps（IPv4 は 949、IPv6 の理論 1440/1538 × 1000 = 936） | segment ごとに `tcp6_route` が `ipv6_route_source`（自分の address の走査、route、RFC 6724 の source の選び）を、`ipv6_output` が自分の address の走査と route をもう一度。profiler で `ipv6_source_select`・`ipv6_next_hop`・`ipv6_address_state` が送りの tick の約 27 % | IPv4 と同じく TCP が IPv6 の way（interface・next hop・hop limit・path MTU）を `route_generation()` の世代つきで持つ（`tcp6_route_cached`・`tcp6_route_remember`、`ipv6_route_way`・`ipv6_output_way`）。IPv6 の route・address の追加と削除・DAD の結果・有効期限・link の hop limit と MTU の変化・PTB で `route_changed()` を数える。`nd6_output` は link address の分かった neighbor を `ipv6_link_get` なしで送る | db4090cae、e662e0abc |
| 2 | PTB の推定が後の大きい PTB で上がる（RFC 8201 §4 の MUST NOT）。他の host が送った packet の PTB も信じる | `icmp6_too_big` が entry を上書き、中の source を見ない | 中の source が自分の address の時だけ。持っている推定より大きい MTU は推定を変えない | d3f64be25 |
| 3 | 相手の RST の後の write が ENOTCONN（serve-send で相手が先に閉じると `tcp-perf: serve-send failed …: Transport endpoint is not connected`）。IPv4 も同じ | `tcp_send_bytes` の入口が state だけを見て、RST の `socket->error` を報せない | 接続の外の state では先に `socket->error`（ECONNRESET）を一度、RST で `write_shutdown` を立て、その後の write は EPIPE（BSD の socantsendmore と同じ） | e662e0abc |
| 4 | lo0 の IPv6 の address（networkd の link-local）が永久に tentative | loopback には DAD が走らない（tick が飛ばす）のに TENTATIVE で足す | loopback の device の address は NODAD（RFC 4862 §5.4） | 53080d387 |

## 結果（2026-10-29、VF2 実機、相手 5330 の Debian（同じ switch）、20 秒、MTU 1500 は理論 936.3、9000 は 8940/9038 × 1000 = 989.2）

| 試験 | kernel（tree） | 値 |
| --- | --- | --- |
| IPv6 送り・受け（直す前） | d62b3924f | 送り 613.0（最遅 570.5）、受け 821.5、再送 0（CPU が天井） |
| IPv6 送り・受け | db4090cae | 送り 932.2、受け 936.3 |
| IPv6 の 4 方向 | e662e0abc | 受け 936.3、送り 906.9（cpu0 96 %）、serve-send 898.2、serve-recv 936.4（e662e0abc の前の a 版で）。送りは 894〜932 で揺れる: 書き手が eqos の割り込みと同じ CPU に載ると CPU が天井（IPv4 の送りでも ws.md の「旧:」の行と同じ形） |
| IPv6 jumbo 9000（VF2・5330 とも 9000） | db4090cae | 送り 988.5（最遅 987.6）、受け 985.3、send_mss・receive_mss 8940、再送 0 |
| PTB の注入（9000、送りの 8 秒目に 1500） | db4090cae | send_mss 8940 → 1440、前 988.1・後 936.0、止まらない |
| PTB の列（6 秒目 4000・12 秒目 9000・18 秒目 1000） | d3f64be25 | send_mss 8940 → 3940 → （9000 は上げない）→ 1220（1280 に丸め）。3940 の間 約 915（3940 は jumbo の copy、接続ごとに 8 個 = 31.5 KB の窓）、1220 の間 約 748（送りの CPU が packet の数で天井、約 7.7 万 pps） |
| centris（この host、IPv6） | e662e0abc | 送り 898.7（fast retransmit 3140、入れ替わり）、受け 935.4。同じ時の IPv4 の送り 928.6・931.2（ws.md の既知の入れ替わり） |
| 混在（VF2 9000、centris 1500、IPv6） | e662e0abc | 送り 909.3（send_mss 1440）、受け 935.9（receive_mss 8940） |
| link-local（`fe80::…%en0`） | 53080d387 | 送り 894.6、受け 936.3（source は板の link-local） |
| 板の中（`::1`、自分の global、`::` に IPv4 の 127.0.0.1） | 53080d387 | 1247・1232・1128・1233 Mbps、全部完了（`::` の listener は IPv4 も受ける） |
| ping6 jumbo（`vf2-jumbo.sh`） | e662e0abc | 8952 の 3 往復、8953 は `sendto: Message too long`、5330 の `ping -6 -M do -s 8952` 3 往復、IPv4 の既存の確かめも PASS |
| IPv4 の非退行 | e662e0abc | 送り 945.1・949.1、受け 949.3、serve-send 949.3、serve-recv 949.4 |
| QEMU（`qemu-tcp.sh IMAGE 20 inet,inet6`） | e662e0abc の image | PASS（inet 送り 52.2・受け 49.3、inet6 送り 64.7・受け 47.1、emulator の値） |
| tcp-model | e662e0abc | PASS（shim に IPv6 の stub 2 つを足した） |

| 最後の tree の確かめ | 0342c9d89 | IPv6: 送り 901.0、受け 936.3、serve-send 930.9。IPv4: 送り 930.3（再送 1）、受け 949.3 |
| QEMU（最後） | 26b5d7785 の image（0342c9d89 は書き方だけの差） | PASS（inet 送り 66.3・受け 50.7、inet6 送り 63.5・受け 50.3） |
| build | 53080d387（amd64・pcat・rpi4・rpi5・zybo の vmunix・bin/net・bin/networkd）、0342c9d89（vf2・amd64 の vmunix） | 全部 rc=0・warning 0 |

終わりに VF2 en0・5330 enx6c1ff706148a とも 1500 を読み戻した（vf2-tcp.sh・vf2-jumbo.sh の restore）。VF2 の /boot/boot0/vmunix は 0342c9d89 の tree（sha256 の先頭 e10a1f3300a98ffb）、vmunix.old は 53080d387 の tree。5330 に置いた物は vf2-tcp.sh が run ごとに作る `/tmp/vf2-tcp.XXXXXX`（summary の「peer's files」の行、Q1 が消す）。

## 未実施・制限・Future Work の候補

- 5330 を zedBSD で動かす側の IPv6 は未実施（5330 は Linux の相手としてだけ）。Pi 4 は未実施。
- 本物の router が送る PTB は無い（この LAN は全部 on-link）。PTB は `ptb6.py` の注入だけ。注入では経路が大きい segment を落としていないので、切った再送が相手で重複になり dupack から fast retransmit が 1〜2 回出る（本物の経路では起きない形）。
- 中の source が自分でない PTB を捨てる確かめは code を読んだだけ（実機では送っていない）。
- IPv6 の fragment の再組立てが無い（`ipv6_extensions` が fragment を捨てる）。TCP には効かないが、大きい UDP（DNS など）が fragment で来ると落ちる。Future Work の候補。
- 送りの CPU の配置（書き手と eqos の割り込みが同じ CPU）で IPv6 の送りが 894〜932 に揺れる。IPv6 の packet ごとの残りの費用は neighbor の線形の探し・input の `ipv6_accepts` の lock。
- cwnd が送りの窓で抑えられない時も増え続ける（samples で 9 億 byte 台）。WS311（CUBIC）の p001 で扱う（RFC 7661 の考え方）。
- PTB の後の中間の大きさ（1460 より大きく jumbo の MTU より小さい segment）は jumbo の copy の上限 8 で窓が 31.5 KB になる。
- sshd は `AddressFamily inet`（`userland/packages/network/openssh/files/sshd_config`）で IPv6 を listen しない。設計の選択で、stack の不具合ではない。
- 2026-10-29 の作業の初めに VF2 の en0 に link-local が無く、lo0 の link-local が tentative だった。lo0 は #4 で直した。en0 の link-local の欠けは、その後の再起動と MTU の変更を繰り返しても再現しなかった（原因は未確かめ）。
