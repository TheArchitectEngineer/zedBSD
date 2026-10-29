<!-- awesome-plan project=zedbsd record=ws311-p003 -->
# ws311-p003: CUBIC と選び方の口（TCP_CONGESTION・net.tcp.congestion・net.tcpstats）

Status: cleared（2026-10-29 P3 q1673-i01: 128e60640。模型・host・QEMU・5 platform の build、VF2 の LAN で非退行）
Parent: [WS311](../ws.md)
Queue: q1673 / q1673-i01（P3）
依存: [p002](../phase002/phase.md)

## 1. 目標

p001 §4.2〜4.4・§5.1（第 3 版）の実装: CUBIC（RFC 9438）、接続ごとの `TCP_CONGESTION`、system の既定 `net.tcp.congestion`、net.tcpstats の 328 byte と `TCP_STATS_HAVE_CC`（読み手は同じ commit で）、模型の cc-*・fair-* の scenario。既定は reno のまま（判定は p004）。

## 2. 調べ（2026-10-29）

FreeBSD releng/14.1 の sys/netinet/tcp_usrreq.c（raw.githubusercontent.com の GET で読んだ）:
- set: `tcp_set_cc_mod` が `sooptcopyin(sopt, buf, TCP_CA_NAME_MAX - 1, 1)`（1 byte 未満は EINVAL、長い物は 15 byte に切って読む）、`buf[sopt_valsize] = '\0'`、`strncmp` で一覧を探し、無ければ `ESRCH`。
- get: `len = strlcpy(buf, CC_ALGO(tp)->name, TCP_CA_NAME_MAX)`、`sooptcopyout(sopt, buf, len + 1)`（NUL まで、相手の長さが足りなければ切る、その時 NUL は無い）。
- zedBSD はこの形に揃えた（p001 §4.2、review-2 L5）。syscall の層が 128 byte を越える値を EINVAL で断るのは今のまま。

## 3. 変えた所（128e60640）

| file | 要点 |
| --- | --- |
| `src/kern/net/tcp-cubic.c`・`.h`（新） | W_cubic(t) と K の整数の計算（byte と ms、C = 0.4 を 2.5e9 にまとめた、掛けてから割る）、64 bit の立方根（範囲 2642246 で溢れない）。Makefile の `KERN_NET_SOURCES` に 1 行（Q1 の可） |
| `src/kern/net/tcp.c` | `tcp_cubic_grow_locked`（epoch の始まり: after_timeout なら K = 0・origin = W_max = cwnd（回復の外で消す）、cwnd < W_max なら K と origin = W_max、他は K = 0・origin = cwnd。門が断った区間で epoch をずらす。target = W_cubic(t + srtt) を [cwnd, 1.5 cwnd]、`cubic_grow_rem` を持つ。W_est は α = 0.529（W_est ≥ cwnd_prior で 1）、`cubic_est_rem`。W_cubic(t) < W_est なら cwnd ≥ W_est）。`tcp_cubic_loss_locked`（fast convergence 17/20、ssthresh = 0.7 × flight ≥ 2 MSS、timeout は cwnd = 1 MSS と after_timeout、2 回目以降の timeout は p002 の共通の枝で ssthresh・W_max を保つ）。`tcp_cc_set_locked`（切り替えで cwnd_acked・W_max・cwnd_prior・after_timeout・round・epoch を空に）。`tcp_cwnd_round_reset_locked` が epoch も空にする。`tcp_cc_name`・`tcp_cc_lookup`・`tcp_cc_default`・`tcp_cc_set_default`（public、sysctl から）。`CONFIG_TCP_CC_DEFAULT`（既定 `TCP_CC_RENO`）。socket は作る時に既定を読み、listen の子は親を継ぐ。net.tcpstats に cc・cubic_w_max_kb・cubic_k_ms・cwnd_state |
| `include/uapi/netinet.h`・`include/libc/netinet/tcp.h` | `TCP_CONGESTION` 64、`TCP_CA_NAME_MAX` 16 |
| `include/uapi/sysctl.h` | `NET_TCP` 2・`NET_TCP_CONGESTION` 1、`TCP_STATS_VERSION` 2、entry に uint32 × 4（312 → 328、static assert）、`TCP_STATS_HAVE_CC`、`TCP_STATS_CC_*`、`TCP_STATS_CWND_*` |
| `src/kern/sysctl.c` | `net.tcp.congestion`（読み: 名前と NUL、書き: superuser、`tcp_cc_lookup` の EINVAL・ESRCH） |
| `include/kern/net/tcp-socket.h` | `cc`・CUBIC の状態、`TCP_CC_RENO`・`TCP_CC_CUBIC`、public の宣言 |
| `include/kern/net/packet-buf.h` | `CONFIG_PACKET_BUF_POOL_COUNT`（未定義なら 1024、模型の big の build だけが 4096U: 2 host が 1 つの pool を分け、1 MB の窓で pool が尽きて接続が死んだ） |
| `userland/base/sysctl/main.c` | net.tcpstats の行に `cc= cubic_w_max_kb= cubic_k_ms= cwnd_state=`、`net.tcp.congestion` の表示（文字列）と `sysctl net.tcp.congestion=cubic` |
| `userland/tests/tcp-perf/main.c` | `--cc NAME`（接続、serve では listener） |
| `plan/ws220/tests/tcp-model/` | `--cc`・`--second-flow-cc`（setsockopt と getsockopt の読み戻し）、`--measure-from-s`（`steady:` 行）、tcpstats の check に HAVE_CC と A の cc、big の build に pool 4096U、tcp-cubic.c |
| `plan/ws311/tests/` | `run-cubic-host-test.sh`、`cc-model.sh`（模型で reno と cubic を 5 つの種、300 秒、20 秒から）、`vf2-cc.sh`（VF2 → 5330、5330 の ingress の ifb・netem、GRO off、trap で戻して読み戻す） |

## 4. 確かめ（2026-10-29）

| コマンド | 結果 |
| --- | --- |
| `plan/ws311/tests/run-cubic-host-test.sh` | 125424 点、差 0（K ≤ 1 ms、W ≤ 1 MSS）、立方根の端も PASS |
| `OUT=build/ws311/host-p003 plan/ws220/tests/tcp-model/run.sh` | `tcp-model: PASS`（全 scenario、既定 reno） |
| `tcp-model --cc cubic --bytes 16777216` | getsockopt の読み戻し・tcpstats（length 1056 = 72 + 3 × 328）PASS |
| 5 platform の vmunix と VF2 の sysctl・tcp-perf（build5.sh p003） | rc=0、warning 0 |
| `cc -std=c11 -Wall -Wextra -Werror userland/tests/tcp-perf/main.c`（Linux） | build できる |
| `plan/ws311/tests/cc-model.sh`（模型、各 5 つの種の中央値、300 秒、20 秒から） | `cc-model: PASS`: cc-lan 比 1.000、cc-20ms-loss 8.257 → 8.788 MB/s 比 1.064（≥ 0.97）、cc-80ms-loss 3.133 → 2.480 比 0.792（記録）、fair-cubic-cubic Jain 0.998、fair-cubic-reno Reno の取り分 0.473（≥ 0.25） |
| `plan/ws221/tests/qemu-tcp.sh "" 20 inet,inet6`（QEMU） | `qemu-tcp: PASS` |
| 実機 VF2 ↔ 5330 の LAN（vf2-tcp.sh、128e60640 の kernel） | 送り 949.2、受け 949.3 Mbps |

未実施: p001 §5.1 の cc-100ms-step（t = K の軌跡の check）と書き手の休み → 再開の scenario は作っていない（模型の判定は上の 5 つ）。CUBIC の伸びは host 単体試験と cc-* の中央値で見た。

## 5. 模型の所見（種 1 つ、2026-10-29）

80 ms・35 Mbps・queue = BDP・loss 0.004 % で Reno 2.93、CUBIC 2.35 MB/s（link 4.17）。原因は今の stack の 3 つ（HyStart が無い slow start の行き過ぎ、SACK の無い NewReno の 1 RTT に 1 穴、回復の終わりの line rate の burst）。CUBIC の β = 0.7 は行き過ぎの後の ssthresh を経路の容量より大きく残し、もう一度落とす。RFC 6582 §3.2 step 3 の選択肢 (1) は入れない（Q1 の判断 2026-10-29、Future Work の F-WS311-sack に PRR・HyStart++ と並べる）。
