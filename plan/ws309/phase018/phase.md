<!-- awesome-plan project=zedbsd record=ws309-p018 -->

# ws309-p018 規約の全文の見直し

- 親: [WS309](../ws.md)。Queue: q1662-i10（P1、見直しは P1 が起動した conformance-reviewer）。
- 状態: in-progress（2026-10-30）。
- 依存: p001〜p016（p015 は判断待ちで、その source は無い）。

## 範囲と方法

- WS309 の commit（`agent/p1-ws309`〜`agent/p1-ws309i` の各 branch の自分の commit と、`plan/ws309` を触った commit、70 個）が触った C の 92 file のうち、
  WS309 の行（`git blame` で WS309 の commit の行）だけを [coding-style.md](../../coding-style.md) の全文と guardrail の「開発の管理情報」に照らした。書き方だけを直し、動作は変えない。
- `plan/tools/style-check.py` の WS309 の行の指摘 88 → 9（残りは printf の引数の短い二択の条件演算子 7 と、宣言の直後の `(void)context;` 2）。加えて、管理情報・空の
  comment・`return f(...)`・裸の `return error;`・関数の頭の comment・条件の中の関数の呼び出し・Boolean の式・if/else の括弧・分けた呼び出しの 1 行 1 引数を探した。
  comment の意味の良し悪しは一行ずつは読んでいない（直した所の周りだけ）。

## 結果（WIP b7cec4b72・f9b071881、31 file、+313/-67）

- WS309 が書いた comment から WS・Phase・Queue の番号と design の節の番号を除いた（output-switch.c・scanout.c・spawn.c・compose.h・display-thread.c・
  keyboard-hand.c・cursor-plane.c・input-thread.h・input.c・seat-answer.h）。前からある参照（ws213-p004 など）は WS309 の行でないので残した。
- 閉じ括弧の後の空行と目的の comment（約 68 か所）、条件の中の関数の呼び出しを変数へ（9 か所、短絡の順を保つ。glass.c の `blur_done` の hold、scanout.c の
  top の判定は 2 つの if に）、成功の return を最後に（bluetooth-zedbsd.c・display-thread.c・glass.c・output-switch.c）、1 行 1 引数、公開の関数の頭の comment。
- P1 の点検: glass.c の `else if (!glass->worker_running && !writable)` の分け方は連鎖の最後の枝で同じ意味、`blur_done` の hold、scanout の 2 つの if、
  `wallpaper_target` の `*renewed` は前と同じ動作であることを diff で確かめた。
