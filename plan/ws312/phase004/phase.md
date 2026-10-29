<!-- awesome-plan project=zedbsd record=ws312-p004 -->

# ws312-p004 規約の全文の見直し

- 親: [WS312](../ws.md)。Queue: q1672 / q1672-i01（担当 P6、2026-10-29。見直しは conformance-reviewer に頼んだ）。
- Status: cleared（2026-10-29。書き方だけを直した 4cdc4f19f、動作は変えていない）

## 範囲

基点 e4b28311e からこの WS が変えた C: `src/kern/net/ipv6-frag.c`（全部）、`src/kern/net/ipv6.c`・`ipv6.h`・`wire.h`・`packet-buf.c` の変更部分、
`userland/tests/udp6-recv/main.c`、`plan/ws312/tests/frag-host-test.c`。plan/coding-style.md の全文と guardrail の「出典」「開発の管理情報」と照らした。

## 結果

- 直した所（4cdc4f19f）: ipv6-frag.c の閉じ括弧の後の空行・段落と if・loop・呼び出しの comment・lock の区間の段落の分け方、ipv6.c の ND の型の判定の comment、
  udp6-recv の poll の後の if の comment、frag-host-test.c の条件の中の呼び出し（変数に受けてから判定、短絡は保つ）・条件演算子・空行。`plan/tools/style-check.py` の指摘は 3 file で 0。
- 残した所: ipv6.c の既存の blank-after-brace 4 件（424・499・701・1218 行、この WS の変更ではない）、packet-buf.c の既存の 11 件（この WS が変えた関数の外）、
  ipv6_timer_next_deadline の既存の形。frag-host-test.c の `check()` の連なりは試験の形として残した。clang-format は host に無く未実施。
- P6 の確かめ: 差分は comment・空行と、lock の前の 3 つの代入を同じ順のまま段落に分けただけ（読んで確かめた）。tip 4cdc4f19f で host 試験・3 つの vmunix・image・QEMU を流し直して PASS（[p003](../phase003/phase.md)）。
