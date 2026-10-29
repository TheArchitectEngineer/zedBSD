<!-- awesome-plan project=zedbsd record=ws311-p003 -->
# ws311-p003: CUBIC と選び方の口（TCP_CONGESTION・net.tcp.congestion・net.tcpstats）

Status: in-progress（2026-10-29 P3 q1673-i01）
Parent: [WS311](../ws.md)
Queue: q1673 / q1673-i01（P3）
依存: [p002](../phase002/phase.md)

## 1. 目標

p001 §4.2〜4.4・§5.1（第 3 版）の実装: CUBIC（RFC 9438）、接続ごとの `TCP_CONGESTION`、system の既定 `net.tcp.congestion`、net.tcpstats の 328 byte と `TCP_STATS_HAVE_CC`（読み手は同じ commit で）、模型の cc-*・fair-* の scenario。既定は reno のまま（判定は p004）。

## 2. 調べ（2026-10-29）

FreeBSD releng/14.1 の sys/netinet/tcp_usrreq.c（raw.githubusercontent.com の GET で読んだ）:
- set: `tcp_set_cc_mod` が `sooptcopyin(sopt, buf, TCP_CA_NAME_MAX - 1, 1)`（1 byte 未満は EINVAL、長い物は 15 byte に切って読む）、`buf[sopt_valsize] = '\0'`、`strncmp` で一覧を探し、無ければ `ESRCH`。
- get: `len = strlcpy(buf, CC_ALGO(tp)->name, TCP_CA_NAME_MAX)`、`sooptcopyout(sopt, buf, len + 1)`（NUL まで、相手の長さが足りなければ切る、その時 NUL は無い）。
- zedBSD はこの形に揃える（p001 §4.2、review-2 L5）。

## 3. 進め方

1. `src/kern/net/tcp-cubic.c`・`tcp-cubic.h`（W_cubic と K の整数の計算、TCP の状態から離した）と host の単体試験 `plan/ws311/tests/run-cubic-host-test.sh`（double と格子で比べる。2026-10-29 に先に書いた: 125424 点、差 0 で PASS。立方根の範囲を 2642246 に抑えて 64 bit の溢れを避けた）。
2. tcp.c の CUBIC の状態の機械（§4.3）と空にする所（§4.4）、口（§4.2）、net.tcpstats、sysctl、userland の sysctl、libc の header、tcp-perf の `--cc`。
3. 模型の `--cc`・`--second-flow-cc` と scenario（期待の数値は p001 §5.1 の review-2 S3 の方針で、ここで見積りから書く）。
