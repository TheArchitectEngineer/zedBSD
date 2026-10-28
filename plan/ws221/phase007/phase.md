<!-- awesome-plan project=zedbsd record=ws221-p007 -->
# ws221-p007: 規約の全文の見直し

Status: cleared（2026-10-29。機械で見える違反を WS221 の hunk の 217 件から 52 件に減らし、残りは下の「直さなかった物」に理由を書いた。build は warning 0、host 試験は PASS。QEMU・実機の回帰は未実施で、書き方だけの変更）
Parent: [WS221](../ws.md)
Queue: q1664 / q1664-i04（P3）

## 範囲と方法

- 範囲: `git diff 61630bb0b` のうち依頼の file（src/kern/net/ 全部、include/kern/net/ 全部、sysctl.h・netif.h、kcrt.c、syscall.c、dw-eqos.c、cadence-gem.c、rpi4-ethernet.c、bcm43455.c、usb-cdc-ecm.c、usb-cdc-ncm-net.c、usb-cdc-ncm.c、usb-cdc-ncm.h、userland/base/{ifconfig,ping,net,networkd,sysctl}/、userland/tests/{tcp-perf,udp-hold}/、plan/ws220/tests/tcp-model/{model.c,model.h,shim.c}、plan/ws221/tests/*.c）の WS221 の hunk。
- 先に `plan/tools/style-check.py` を hunk の行だけに絞って走らせ（初回 217 件）、その後に目で、開発の管理情報、comment の形、古い comment、直接の return、引数の入れ子を確かめた。
- 動作・API・ABI・計算の順序・error の値は変えていない。直したのは書き方だけ。判定が純粋な関数（`tcp_sequence_before` など）を条件から出して変数に入れた所は、評価が増えるだけで結果は同じ。

## 見た file

kernel: src/kern/net/{tcp,packet-buf,socket,socket-file,net-device,core,checksum,route,ipv4,ipv6,icmp,icmp6,inet-socket,packet-socket,udp,internal.h,ipv6.h}.c/.h、src/kern/net/wifi/wlan.c、include/kern/net/*.h、include/uapi/{sysctl,netif}.h、src/kern/{kcrt,syscall}.c。
driver: dw-eqos.c、cadence-gem.c、rpi4-ethernet.c、bcm43455.c、usb-cdc-ecm.c、usb-cdc-ncm-net.c、usb-cdc-ncm.c、usb-cdc-ncm.h。
userland: ifconfig、ping、net（main.c・netconf.c・netconf.h・protocol.c・protocol.h・reconcile.c）、networkd（main.c・managed-lan.c・managed-lan.h）、sysctl、tests/tcp-perf、tests/udp-hold。
試験: plan/ws220/tests/tcp-model/{model.c,model.h,shim.c}、plan/ws221/tests/*.c。

## 直した項目

| file | 規則 | 要点 |
| --- | --- | --- |
| src/kern/net/tcp.c、include/kern/net/tcp-socket.h、src/kern/net/packet-buf.c | 開発の管理情報（AGENTS.md §10） | WS221 の hunk で新しく足された comment の `ws220-p00N`・`BUG-222`・`ws130`・`review-1 G1` の参照 9 箇所を外した（意味の文は残した） |
| userland/tests/tcp-perf/{main.c,Makefile} | 同上 | `plan/ws221/tests/perf-peer.py` への参照を外した |
| src/kern/net/tcp.c | 関数を条件の中で呼ばない（§6） | `tcp_sequence_before`・`tcp_sequence_at_or_before`・`tcp_segment_size_locked`・`tcp_receive_sws_step`・`tcp_nagle_holds_locked`・`tcp_unsent_fits_cwnd_locked`・`tcp_copy_is_jumbo`・`route_lookup_ref`・`inet_socket_speaks_ipv6`・`in6_equal` を変数に受けてから判定（約 22 箇所） |
| src/kern/net/tcp.c | 条件演算子（§6） | 入れ子でない二択の `?:` 9 箇所を `if`/`else` に（`return error == ENOBUFS ? ENOMEM : error;` は理由の comment 付きの 2 つの return に） |
| src/kern/net/tcp.c | 閉じ括弧の後の空行と comment（§5） | 閉じ括弧の直後に続いていた文 約 15 箇所に空行と目的の comment |
| src/kern/net/tcp.c | 複数行の文の波括弧（§8） | `ipv4_output_routed`/`ipv4_output_sourced` の分岐、`tcp_send_reliable` の分岐、`tcp_passive_syn` の呼び出しを囲む `if` に波括弧 |
| src/kern/net/tcp.c | 名前・宣言（§4） | 追加した変数は関数の先頭に宣言（`step`・`holds`・`fits`・`copy_flags`・`ipv6` など） |
| src/kern/net/{core,checksum,ipv4,ipv6,net-device,packet-buf,route,socket,socket-file}.c | 同上の規則 | 条件の中の関数（`net_device_poll_wanted`・`refcount_load`・`file_status_flags_get`・`KERN_TEST_FAULT`）、`?:`、閉じ括弧の後の文、`socket_poll_notify` の前の comment 計 約 22 箇所 |
| src/kern/net/wifi/wlan.c | 条件の中の関数 | `__atomic_load_n` を変数に受けて判定（4 箇所） |
| src/kern/syscall.c | 条件の中の関数 | `socket_datagram_max` を変数に |
| src/drivers/ethernet/dw-eqos.c | 条件の中の関数、閉じ括弧の後の文 | `packet_buf_physical`・`drv_dma_physical_address` を順に呼んで検査、`take_tx_done_locked` などの前に comment と空行（4 箇所） |
| src/drivers/usb/usb-cdc-ncm.c、usb-cdc-ncm-net.c | 条件の中の関数、`?:` | `datagram_fits` を変数に、`return error != 0 ? error : EIO;` を 2 つの `if` に |
| include/kern/net/net-device.h | comment の形（§10） | `/* 文字…` で始まり文字で閉じる複数行 comment 3 つを、`/*` と `*/` が単独の行の形に |
| userland/base/net/{main.c,protocol.c,reconcile.c} | 条件の中の関数、閉じ括弧の後の文、`return` の comment | 新しく足した MTU の枝・`networkd_lan_record_format/parse`・`lan_record_mtu`・`confirm_discard` の `&&`/`||` の組み立てを `if` に、`Returns the computed result.` を意味のある comment に（約 30 箇所） |
| userland/base/networkd/{main.c,managed-lan.c} | 同上 | 新しく足した `lan_policy_decode`・MTU の run・rollback の MTU・`networkd_lan_set_mtu` の条件の関数を変数に、`?:` を `if` に |
| userland/base/{ifconfig,ping}/main.c | 同上、名前（`function_result`） | `set_mtu` の `strcmp`、`-s` の枝の `function_result` を `status` に |
| userland/tests/{tcp-perf,udp-hold}/main.c | 同上、forward declaration（§3） | 複数行の forward declaration を 1 行に、`udp-hold` の `hold_number` に forward declaration、`strcmp`・`bind` を条件から出す、`?:`、空行と comment |
| plan/ws221/tests/{copy-sum,host-lan-policy-mtu,host-netconf-mtu,ncm-jumbo-wire,packet-buf}*.c、plan/ws220/tests/tcp-model/model.c | 同上（試験の harness） | 判定の関数を変数に、`?:` を `if` に、結果の comment（約 25 箇所）。plan/ 以下の試験の中の `ws221-p008` の参照は、試験が plan/ に属するので残した（packet-buf-host-test.c の文頭の 1 箇所だけ一般の言葉にした） |

## 直さなかった物（理由）

- 欠陥に見える所: 見つからなかった（動作の変更が要りそうな所は無し）。
- 他の WS の既存の違反（直さず記録だけ）:
  - src/kern/net/tcp.c の `ws220-p00N`・`review-1 M2` などの参照（WS220 の hunk で、WS221 が足した物ではない。約 40 箇所）、`ws130-p003`・`ws177-p045` の参照。
  - userland/base/net/main.c の `backend_payload` ほかの `strlen`/`networkd_field_write` の `||` の連鎖、`function_result` の変数（既存の関数の既存の形。WS221 は MTU の枝だけ足した）。
  - userland/base/net/netconf.c:1394 の `else if (strcmp(key, ...) == 0)` の連鎖、userland/base/networkd/main.c の `rollback` の `else if (strcmp(word[1], ...))` の連鎖と、field の decode の `else if` の連鎖（WS221 は `mtu` の枝を 1 つ足しただけで、隣の枝と同じ形。連鎖ごとの書き換えは WS221 の範囲を超える）。
  - userland/base/ifconfig/main.c:160 の `else if (strcmp(argv[2], "mtu") == 0)`、userland/base/ping/main.c:168 の `-i` の枝（既存の連鎖）。
  - src/drivers/usb/usb-cdc-ecm.c の `ecm_rearm` の宣言の `?:` の初期化（関数全体が宣言で関数を呼ぶ既存の形）。
  - src/kern/syscall.c の `goto out;`（既存の `goto out` と同じ、共通の後始末への前向きの 1 回の goto）。
  - src/kern/net/tcp.c の `(void)address_length;`（UNUSED の引数。style-check が段落の comment を要求するが、この tree の既存の書き方）。
- 試験の harness に残る物: plan/ws220/tests/tcp-model/model.c の option の `strcmp` の連鎖（既存の option の解析の形）と `printf` の複数行、plan/ws221/tests/ncm-jumbo-wire-test.c の `CHECK(...)` の連続（検査の macro を並べる harness の形）が残りの大半（52 件中 38 件）。

## 確かめた試験と結果

すべてこの worktree（`/home/awe/zedBSD-worktrees/p3-ws221d`）で実行。

| コマンド | 結果 |
| --- | --- |
| `OUT=build/p7-host plan/ws221/tests/run-packet-buf-host-test.sh` | `run-packet-buf-host-test: PASS`（amd64 系 jumbo 32・reserve 8、i386 系 jumbo 0） |
| `plan/ws221/tests/run-copy-sum-host-test.sh` | `run-copy-sum-host-test: PASS` |
| `plan/ws221/tests/host-lan-policy-mtu.sh`・`host-netconf-mtu.sh`・`run-ncm-jumbo-wire-test.sh` | それぞれ `host-lan-policy-mtu: PASS`・`host-netconf-mtu: PASS`・`ncm-jumbo-wire: PASS` |
| `make ZEDBSD_CONFIG=plan/ws221/tests/config-vf2-tcp.mk BUILD=build/vf2 -j16 build/vf2/vmunix` | rc=0、`grep -c 'warning:'` 0（build/p7-vf2.log・p7-vf2b.log） |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ci-amd64 -j16 build/ci-amd64/vmunix build/ci-amd64/bin/{net,networkd,ifconfig,ping}` | rc=0、warning 0、`amd64 vmunix check: PASS`（build/p7-amd64.log） |
| tcp-perf・udp-hold の build（`config-amd64-tcp.mk` で `bin/tcp-perf`、`config-5330-jumbo.mk` で `bin/udp-hold`） | rc=0、warning 0 |
| `OUT=build/p7-model plan/ws220/tests/tcp-model/run.sh` | 下の「tcp-model の結果」 |

### tcp-model の結果

`OUT=build/p7-model plan/ws220/tests/tcp-model/run.sh` の最後の行は `tcp-model: PASS`、rc=0、FAIL の行 0（log は build/p7-model2.log）。最初の 1 回は、同じ OUT を別の host 試験と同時に使って binary が消え全 scenario が FAIL したので、単独で走らせ直した。

## 未実施

- QEMU・実機の回帰（書き方だけの変更で、動作は変えていない。Q1 の判断に委ねる）。
- style-check の `paragraph-comment` が示す plan/ws221/tests/ncm-jumbo-wire-test.c の `CHECK` の列は直していない。
- 他の platform（arm64・armv7・i386 など）の kernel の build は走らせていない（amd64 と riscv64 の vmunix のみ）。

## 消したい物

なし（build/p7-*.log・build/p7-* の使い捨てがあるが build/ は git に入らない）。

## P3 の後の確かめ（2026-10-29 09:1x、d62b3924f の tree）

- build: amd64・pcat・rpi4・rpi5・zybo の vmunix・bin/net・bin/networkd が全部 rc=0・warning 0（rpi4・rpi5 は -j 無し）。VF2 の kernel（`config-vf2-tcp.mk`）rc=0・warning 0（sha256 fd1c4fd1f241165c…）。
- VF2 実機（相手 5330、flock /tmp/i915-hw.lock）: 1500 で送り 949.0・936.1（最遅 922.7、その run の再送 0・dupack 0、損失でなく run ごとの揺れ）、受け 949.3。9000 で受け 990.6、送り 990.8。終わりに VF2 en0・5330 とも 1500。
- 書き方だけの変更の非退行として、上と tcp-model の PASS で足りると P3 は判断した。
