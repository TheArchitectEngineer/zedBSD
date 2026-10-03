<!-- awesome-plan project=zedbsd record=ws075p024 -->

# ws075-p024: C6 を判定できる物差し・select の flag の再利用・host の fixture の既存の失敗

Phase ID: `ws075-p024`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30。C6 の物差し（cursor が出る flip までを buffer の pixel で判定、40 試行 × 5 run をまとめる）: 10 app で中央値 121.5 ms・p90 173.0 ms、run の中央値 112.7〜131.4 ms で C6（50 ms）は満たさない。select の flag の再利用で 1 run 6.7〜7.0 → 6.4〜6.7 ms、C6 は変わらず。host の fixture の 4 つの既存の失敗を直し全 10 個 PASS。vkx・vke1・vke2・vkc PASS）
Phase disposition: normal
承認: 2026-09-30 main の指示（p023 の merge の後）。

## 目的と受け入れ条件（main の指示）

WS099 の基準 C6（窓 10 個で pointer の移動から表示まで中央値 50 ms 以内、実機）を確かに判定できる状態にする。

1. 物差し: 1 回の run の中央値が frame の刻みで揺れるので作り直す。run あたりの試行を増やし、複数の run をまとめた中央値・
   90 パーセンタイル・ばらつきを出し、同じ image で 5 run 以上回して安定を確かめる。`measure-apps.sh` を直し、Tools 節に載せる形に。
2. 最適化: ws.md の候補（同じ predicate の flag を select ごとに作り直さない）を実装し、1 run と C6 の物差しで前後を比べる。
3. 既存の失敗: host の試験 cmd・res・sync・cmdbuf の FAIL（p009 から）を直す。

制約: WS101 が同じ compiler で作業中。compiler の変更は小さく、区切りごとに `git merge main -m WIP`。実機は `flock /tmp/i915-hw.lock` の下。

## 3. host の fixture の既存の失敗（済み）

原因はどれも試験の側が古いこと（実装の変化に試験が追いついていない）。実装は変えていない。

| fixture | 原因 | 直し |
| --- | --- | --- |
| cmd | link の未定義: fence.c の query pool（p006）が呼ぶ GPU の object・batch の関数（`drv_i915_gfx_object_create` など）と `drv_i915_batch_emit` | `render/batch.c` を取り込み、`i915-vk-render-stubs.inc` と同じ stand-in（object を作れない、batch を取れない）を置いた |
| cmd・res・sync | 拒否の理由（`XXX ...`）の行の後に dispatch.c が「command refused at opcode N」の行を足した（a7d05290）ので、最後の 1 行しか持たない log では理由の行が消える | 試験の log を「流れの開始から全ての行」にした（cmd は fixture の kern_logf、ほかは `i915-vk-render-stubs.inc` の stub_log を stub_execute ごとに空に）。理由の行は先頭の行として調べる |
| sync | vkCreateQueryPool（opcode 47）は p006 で実装済みで、未実装の拒否を期待していた | 流れは受け付けられ、host では counters の GPU object が無いので pool は作られない、を調べる |
| cmdbuf | dynamic offset の持ち方が binding 番号の添字から（binding, offset）の組の表（`dynamic_count`・`dynamic_bindings`・`dynamic_offsets`）に変わった。log の文言の変更 3 つ（uniform → uniform or storage、dynamic uniform buffers → dynamic buffers） | 組の表で与え・調べる。文言を今のものに |

確認: `sh plan/ws031/tests/run-vk-host-tests.sh`（既定の全 10 個、各 plain と ASan/UBSan）PASS。

## 1. C6 の物差し（`h4-ctl.py c6`・`c6.py`）

今の `h4-ctl.py latency` は「pointer を動かしてから次に PLANE_SURFLIVE が変わるまで」で、10 回の中央値。問題:

- 10 app では Gears などが絶えず flip するので、pointer の動きを含まない flip でも止まる（「表示まで」ではなく「次の flip まで」）。
- 10 回では、frame の刻み（16.7 ms の倍数）に乗った値の中央値が run ごとに 48・65・82 ms と跳ぶ。

新しい `h4-ctl.py c6 PIPE COUNT X Y`:

- 試行ごとに 0.3〜0.8 s（乱数、frame の位相から外す）待ち、pointer を (X+40, Y) と (X-40, Y) へ交互に動かす。
- cursor は compositor が frame に合成する（`userland/desktop/wayland/compose.c` の compose_cursor）。動かす前に、行き先の
  すぐ下の 4 行 × 16 pixel を画面に出ている resident buffer から読み（`x`、kernel の仮想 address、shot の memsave と同じ）、
  PLANE_SURFLIVE が変わるたびに新しい buffer の同じ pixel を読む。変わっていた最初の flip（cursor が行き先に出た frame）までの
  時間を 1 試料とする（3 s で打ち切り、「出ない」として数える）。
- X, Y は窓の無い壁紙（動かない所）でなければならない。最初に (X-40, Y) で壁紙を 1 回 click する（下の経緯）。`measure-apps.sh` は
  (960, 100)（上の bar の下、窓の上端より上の空）を使う。
- 出力: 試料の列、中央値・90 パーセンタイル・最小・最大・cursor の前に来た flip の数・出ない数。試行ごとの行は c6.log（fetch で取る）。

`measure-apps.sh` は 10 app の latency の後に `c6 A ${C6_TRIALS:-40} 960 100` を行う。`summarize.py` は c6 の中央値・p90 の列を出す。
`c6.py OUTDIR...` は run ごとの中央値・p90 と、全 run の試料をまとめた中央値・p90・run の中央値の幅と標準偏差、C6 の判定
（まとめた中央値 ≤ 50 ms、5 run 未満は判定としない）を出す。

Tools 節への登録（main に依頼）: `plan/ws075/tests/hdmi/measure-apps.sh`（1 run の計測、C6 の試料を含む）と
`plan/ws075/tests/hdmi/c6.py`（5 run 以上をまとめた C6 の判定）。

## 2. select の flag の再利用

SELECT の lowering は毎回 `cmp.nz f0.0 cond, 0` → `(+f0.0) sel` を出していた。merge の vec4 の成分ごとの select のように、同じ
条件の SELECT が続く時は flag が同じなので cmp を出さない（`i915_compile_state.select_flag`）。SELECT 以外の IR 命令は f0.0 を
書きうるので、その前で覚えを捨てる（SELECT と SELECT の間に出る EU の命令は SELECT 自身の cmp・sel と fill・spill の send だけ、
実行の mask も同じ）。panel.frag: cmp.nz 38 → 20、囲いの外の命令 160 → 142、全体 425 → 407。

host: vk の全 fixture・guard/run.sh・gentool PASS。

## 経緯（2026-09-30）

- 最初の版（画面の中央 (960, 540)、click なし）は実機で 0/40（cursor が一度も出ない）。調べの session（base の image、10 app）で撮ると
  cursor が画面のどこにも無い: X client（X terminal）が空の cursor を設定すると compositor は `cursor_hidden` にし、keyboard の focus が
  変わるまで（seat.c の zwl_cursor_default）全画面で隠したまま（壁紙の上でも）。compositor の挙動で、この Phase では直さない（main へ
  報告）。壁紙を 1 回 click すると arrow が戻り、窓の配置は変わらない。また 10 app の配置は main の merge で p023 の時と変わり、
  (960, 540) は X terminal の上になっていた。
- 壁紙の (960, 100) と最初の click で、同じ session で 40/40 が出た: 中央値 131.2 ms、p90 176.6 ms（最小 58.0、最大 226.5）。同じ session の
  旧い `latency A 10` は中央値 82.0 ms、flip の率 9.5/s。40 試行のうち 32 回は、動かした後の最初の flip が cursor を含まない（旧い物差し
  はそれで止まるので小さく出る）。
- 新しい不具合: base・sel の最初の組の sel 1 で、10 app を開く途中（Model viewer の辺り）に compositor の描画が止まった。kernel log は
  `vk: draw refused: set 0 binding 0 has no image view and sampler` → command buffer の停止 → lease の解放。p023 の実験 (a) 2 の
  「X terminal の後の停止」も同じ行（2 回とも別の image、select の変更の前の版を含む）。executor（render/state.c）が sample する slot の
  view か sampler が NULL の draw を拒む。原因は未調査（p024 の範囲外、main へ報告、Bug と Phase は main の判断）。その run と、
  cursor の出なかった base 1 は `build/ws075-p024/old-m-*` に残し、計測をやり直した。

## 実機の passthrough の計測（2026-09-30、5330 の QEMU の VFIO passthrough、素の 5330 ではない）

image は `plan/ws075/demo/build-demo-image.sh build/ws075-p008/pt passthrough ...`（p023 と同じ）で、compile.c だけを変えた 2 つ:
base（29f2e10b~1 = p023 の最終の compiler）と sel（select の flag の再利用）。`build/ws075-p024/{base,sel}.img`。
順は base 1 → sel 1 → base 2 → sel 2 → base 3 → sel 3 → sel 4 → sel 5。表は `summarize.py`、C6 は `c6.py`（中央値は `c6.py` が正、
summarize の列は h4-ctl の nearest-rank の値）。

| run | 10 app: flip の率 | compositor: 占有・1 run | C6: 中央値・p90（40 試行） | 旧 latency 中央値（10 試行） |
| --- | --- | --- | --- | --- |
| base 1 | 9.5/s | 46.2%・6.74 ms | 126.2・192.5 ms | 65.6 ms |
| base 2 | 9.9/s | 46.8%・6.95 ms | 125.1・184.8 ms | 82.0 ms |
| base 3 | 9.6/s | 46.2%・6.78 ms | 122.4・179.4 ms | 48.4 ms |
| sel 1 | 9.4/s | 45.0%・6.60 ms | 131.4・180.4 ms | 49.3 ms |
| sel 2 | 9.6/s | 45.2%・6.47 ms | 130.9・195.9 ms | 32.1 ms |
| sel 3 | 9.6/s | 44.1%・6.39 ms | 129.0・167.4 ms | 48.1 ms |
| sel 4 | 10.8/s | 45.5%・6.69 ms | 114.3・151.5 ms | 48.6 ms |
| sel 5 | 9.8/s | 45.5%・6.61 ms | 112.7・170.7 ms | 48.5 ms |

`c6.py` のまとめ:

- base（3 run、120 試料）: 中央値 123.8 ms、p90 184.8 ms、run の中央値 122.4〜126.2 ms（標準偏差 2.0 ms）。
- sel（5 run、200 試料）: 中央値 121.5 ms、p90 173.0 ms、run の中央値 112.7〜131.4 ms（標準偏差 9.3 ms）。C6（中央値 50 ms 以下）: 満たさない。

読み:

- 物差しの安定: 旧い latency（10 試行、次の flip まで）は同じ image で 32〜82 ms と跳ぶ。新しい C6 は run の中央値の幅が 3 run で 4 ms、
  5 run で 19 ms（標準偏差 2〜9 ms）で、50 ms の基準に対して判定に十分に安定。全ての試行（320）で cursor が出た（出ない 0）。
- 本当の値は約 120 ms（p90 約 170〜190 ms）で、C6 の 50 ms から遠い。compositor の frame は約 10/s（1 frame ≒ 100 ms）で、
  pointer の動きは次かその次の frame に乗る。旧い物差しの 48〜82 ms は pointer を含まない flip で止まった値（上の経緯）。
- select の flag の再利用: compositor の 1 run は約 -3%（base 6.74〜6.95 → sel 6.39〜6.69 ms）、C6 は差を言えない（123.8 → 121.5 ms、
  run の幅の内）。
- 画面: base 1 と sel 1 の各 app の撮影は上の bar の下で画素まで同じ（Notes の file 名の時刻と Gears の animation を除く）。
- BUG-117（描画の停止）は、やり直しの 8 run では起きなかった（最初の組の sel 1 で 1 回）。

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws031/tests/run-vk-host-tests.sh`（全 10 個） | PASS |
| `sh plan/ws075/tests/guard/run.sh`、`run-vk-gentool-test.sh`（BRW_TOOLS=Mesa 25.0） | PASS |
| `plan/tools/style-check.py`（compile.c、前との差分） | 新しい指摘 0 |
| `plan/ws075/tests/test-hw.sh`（sel の code、`I915_HOST=solaris10-man`） | vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 PASS（`build/ws075-p024/hw-*/run.log`） |
| QEMU の boot test | 未実施（compiler の 1 file の変更で、demo の image の boot は実機の passthrough の 8 run で通っている。QEMU の GPU の無い boot は compiler を使わない） |

## 未実施・残り

- 素の 5330 での C6: 未実施。
- C6 を満たすには frame を約 2 倍以上速くする必要がある。frame の長さの主因（1 frame あたりの約 7 run の直列の実行か、client を待つのか）
  の分析は ws.md の候補（BUG-117 の後）。
- BUG-117（p025）、BUG-118（X client の空の cursor が全画面に残る、WS099）。
