<!-- awesome-plan project=zedbsd record=ws075p028 -->

# ws075-p028: L2 の計測（backdrop の割合、slot 512・backdrop の使い回し・blur を切った場合の 1 run ずつ）

Phase ID: `ws075-p028`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30、P1。backdrop は 1 frame の draw の 39%。1 run ずつの C6 中央値: base 82.5 ms、slot 512 80.9 ms（効かない）、backdrop の使い回し 65.4 ms（画面は画素まで同じ）、blur を切る 65.9 ms（絵が変わる）。p029 は「使い回し」を推す。blur をやめるかはユーザーの判断）
Phase disposition: normal
承認: 2026-09-30 Q1 の指示（全 WS が L1 にそろった後の L2）。blur を切った場合はユーザーの以前の発言「コンポジターが遅いのは、あきらかに、すりガラスエフェクトのせいでは。おもいきってやめてみるのもいいかも。」を受けて Q1 が追加（やめるかはユーザーが判断）。

## 方法

- 同じ tree（b136af3b、main を取り込んだ後）から 4 つの image を作り、それぞれ 5330 の passthrough で measure-apps を 1 run（C6 は 40 試行）。
  実験の source は main に出さない（patch は `plan/ws075/phase028/exp/`、build の後に戻した、tree の未 commit の変更 0）。
  - base: 変更なし。
  - slots: `exp-slots.patch`: executor の state の slot 128 → 512（state object 2 → 8 MB）、batch 1 → 4 MB。
  - reuse: `exp-reuse.patch`（compositor）: 窓の list の場所ごとに blur 済みの backdrop を持ち、下の窓の「場所・大きさ・今の image・その client の
    commit の数」と desktop の位置の hash が前と同じなら、backdrop（下の scene の描き直し、blur の 4 pass、output の pass の切り替え）を丸ごと
    飛ばして前の結果を使う。最後の blur の pass を場所ごとの image へ直接書く。場所は 16 まで。
  - noblur: `exp-noblur.patch`（= p023 の exp-c1）: backdrop を作らない。glass は blur 済みの壁紙を見せる（すりガラスを止めた場合の上限の目安）。
- 1 frame の draw の数と run の数は executor の session の frame の log（p026、`i915: vk: session N: frames ...`）、reuse の使い回しの率は
  compositor の ZWL PERF（`build/ws075-p026/zwl.sh` で 1 回余分に起動）。

## 結果（2026-09-30、各 1 run、5330 の passthrough）

| 手 | 1 frame の draw（最多） | frame の submit の中 | 10 app の flip | compositor の占有・1 run | C6 中央値・p90（40 試行） | 画面（base と比べて） |
| --- | --- | --- | --- | --- | --- | --- |
| base | 739 | 28〜30 ms（run 4〜7） | 14.5/s | 65.4%・6.39 ms | 82.5・116.0 ms | — |
| slot 512 | 739 | 26.6 ms（run 2） | 15.5/s | 65.7%・14.4 ms | 80.9・131.5 ms | 画素まで同じ |
| backdrop の使い回し | 498 | 21.1〜21.4 ms（run 3） | 18.1/s | 62.5%・6.52 ms | **65.4**・124.3 ms | 画素まで同じ |
| blur を切る | 453 | 20.2 ms（run 3） | 19.6/s | 60.9%・6.37 ms | **65.9**・115.0 ms | 変わる（下） |

（画面の比較は各 app を開いた後の撮影、上の bar の下。違いは Notes の file 名の時刻と Gears の animation だけ。draw の拒否は全 run で 0）

- backdrop の割合: 1 frame の draw 739 のうち 286（39%）が backdrop（下の scene の描き直しと blur）。
- slot 512: run の数は 4〜7 → 2 に減るが、frame の時間はほぼ同じ（26.6 vs 28〜30 ms）、C6 も同じ。run ごとの固定の費用は小さい。
  Model viewer の session の submit が遅くなった（9.5 → 17.2 ms、大きい state の初期化）。採らない。
- backdrop の使い回し: 使い回しの率 約 89%（ZWL PERF の 5 秒ごと: reused 624〜801、drawn 38〜120、frame あたり約 1 回の描き直し = 動く
  Gears の上の X terminal の backdrop）。draw 739 → 498、C6 82.5 → 65.4 ms で、blur を切った上限（65.9 ms）とほぼ同じ。画面は画素まで同じ。
- blur を切る: C6 65.9 ms（使い回しと同じ）。絵が変わる: 窓の sidebar・title bar のすりガラスが、下の窓ではなく blur 済みの壁紙を透かす
  （並べた図 `build/ws075-shots/ws075-p028-blur.png`: 左 base、右 blur を切った場合。Settings を開いた時と 10 窓）。
- 注意: 各 1 run で、C6 の run ごとの揺れは ±5 ms 程度（p027 の 5 run で 87〜95 ms）。この base の 82.5 ms は p027 の 91.3 ms より低い。
  判定は p029 で 5 run。

## 読みと p029 への提案

- L2（C6 中央値 75 ms 以内）には、backdrop の使い回しで届く見込み（1 run で 65 ms、画面は同じ）。blur を切る手と効きは同じで、絵を変えない。
- p029: `exp-reuse.patch` を製品の形に直して実装する（場所の数の上限、窓の入れ替え・閉じた時の cache の扱い、signature の中身の確認（desktop の
  icon・Home の層・Wiseview）、試験（画素の比較と C6 の 5 run、stress-117 の 100 回））。compositor の file（backdrop.c・shell.c・compose.h・
  protocol.c・zwl.h・main.c）を変える。IME の file と seat.c には触れない。
- blur をやめるかはユーザーの判断（C6 の効きは使い回しと同じ、絵は上の図）。

## 未実施

- 各手の 5 run（1 run ずつの実験の Phase）。素の 5330。
