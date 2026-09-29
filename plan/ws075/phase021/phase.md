<!-- awesome-plan project=zedbsd record=ws075p021 -->

# ws075-p021: 性能: compiler が どの channel も走らない block を飛ぶ（提案）

Phase ID: `ws075-p021`
Parent: [WS075](../ws.md)
Status: planning（2026-09-29 提案、[ws075-p018](../phase018/phase.md) の計測から。main の判断待ち）
Phase disposition: normal

## 動機

8 app の desktop で GPU の 90% が compositor の合成（1 batch 約 100 ms）。compositor の `panel.frag` は mode（push constant、draw の中で一様）で
7 つの分岐を持つが、i915 の compiler は分岐を if 変換して全ての分岐を全 pixel で実行する（texture の sample 1 pixel 8 回、image の draw でも）。

## 範囲（案）

1. 段 1: IR の texture の sample（SAMPLE・LD 等の send）に、その block の predicate を持たせ、EU では predicate に立つ channel が 1 つも無い
   thread は send を飛ぶ（flag の any で jmpi）。block の中の値は predicate の立たない channel では使われない（SELECT・phi・store は predicate で
   選ぶ）ので、飛んだ send の結果の register の中身は観測されない。
2. 段 2（要れば）: 算術の多い block も同じく飛ぶ（block 単位の jump。loop 変数・store の SELECT は飛ばさない所に置く）。
3. 受け入れ: host の lower・compile の試験（飛ぶ場合と飛ばない場合の結果の一致）、実機の vkx・vke1・vke2・zdesktop の capture、
   `apps8.sh` の 8 app の desktop の `rate`・`latency` と compositor の engine の占有（`engine_ns`）が変更の前より良くなる。

## 依存

なし（p008・p009 の後の tree）。p018 は p021 の後に計り直して要否を決める。
