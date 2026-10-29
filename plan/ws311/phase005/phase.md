<!-- awesome-plan project=zedbsd record=ws311-p005 -->
# ws311-p005: 規約の全文の見直し

Status: cleared（2026-10-29 P3 q1673-i01: 9d4bad11a）
Parent: [WS311](../ws.md)
Queue: q1673 / q1673-i01（P3）
依存: p003（p004 の測りと並んで行った: 測りの kernel は板に置いた後で、書き方だけの変更）

## 1. 範囲と方法

WS311 が 78cf0206c から変えた C の hunk と新しい file（src/kern/net/tcp-cubic.c・.h、plan/ws311/tests/cubic-host-test.c）を plan/coding-style.md の全文（§14）と plan/guardrail.md の「出典」「開発の管理情報」で読み、書き方だけを直した（conformance-reviewer に頼み、P3 が差分を見て commit）。動作・評価の順・値・error の値は変えていない。

## 2. 直した所（9d4bad11a）

- src/kern/net/tcp.c: `tcp_cc_lookup` を「失敗の return で終わらない」形に（ESRCH を初めに置いて一致で 0）、3 条件の while・`&&` を 1 条件 1 行に、TCP_CONGESTION の set・get の block、tcpstats の cwnd_state、`tcp_cwnd_grow_locked`・`tcp_cwnd_limited_locked`・`tcp_cubic_grow_locked`・`tcp_cubic_loss_locked`（`Value referenced:` を該当の if の直前へ）・`tcp_cc_set_locked` と round を始め直す呼び出しに目的の comment と空行。
- src/kern/sysctl.c: `tcp_cc_set_default` の呼び出しに comment。
- userland/base/sysctl/main.c: `net.tcp.congestion` の set の block の関数呼び出しを条件から出し、三項演算子をやめ、`Succeeded` で終わる形に。
- userland/tests/tcp-perf/main.c: `--cc` の `strcmp` を条件から出した。include/libc/netinet/tcp.h: comment の折り返し。
- plan/ws311/tests/cubic-host-test.c・plan/ws220/tests/tcp-model/model.c（WS311 の hunk）: 複合条件の分け、関数呼び出しと三項演算子を条件・引数から出す、ループと if の comment。

## 3. 直さなかった所と理由

- model.c の option の `strcmp(argv[index], "--…") == 0` の連鎖と、sysctl の `show_name` の `strcmp` の連鎖への 1 項目の追加: 既存の列の形に合わせた。
- model.c の `report()` の既存の三項演算子（WS311 は引数を足しただけ）。
- style-check.py の残りの `blank-after-brace`（model.c 約 10、cubic-host-test.c 2）: 続く行が複数行の文の継続で誤検出と判断。
- 既存の source の comment にある ws220-p00N などの開発の管理情報（WS311 が書いた物ではない、guardrail の「作業の対象になった時」の file の全体の見直しは WS251）。

## 4. 確かめ（2026-10-29）

| コマンド | 結果 |
| --- | --- |
| `OUT=build/ws311/host-p005 plan/ws220/tests/tcp-model/run.sh` | `tcp-model: PASS` |
| `plan/ws311/tests/run-cubic-host-test.sh` | 125424 点、差 0、PASS |
| `build/ws311/build5.sh p005`（vf2 の vmunix・sysctl・tcp-perf・ifconfig、ci-amd64・ci-pcat・ci-rpi4・ci-zybo の vmunix） | 5 つとも rc=0、warning 0 |
| `plan/ws221/tests/qemu-tcp.sh "" 20 inet,inet6`（QEMU） | `qemu-tcp: PASS` |
| `git diff --check` | 出力なし |

- 実機の VF2 は 128e60640 の tree の kernel のまま（書き方だけの差なので p005 の kernel は板に置いていない、未実施）。
