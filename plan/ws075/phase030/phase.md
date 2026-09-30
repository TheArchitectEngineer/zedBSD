<!-- awesome-plan project=zedbsd record=ws075p030 -->

# ws075-p030: L3 の計測と手の選択（p029 の後の frame の内訳）

Phase ID: `ws075-p030`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-30、P1。計測だけ。1 frame 約 45 ms のうち、executor の同期の run 約 40 ms（GPU 約 35 ms: draw の固定の費用 約 9 ms・画素の仕事 約 25 ms、同期の往復と queue 約 5 ms）、記録 約 3 ms、合成の CPU 約 3 ms。pacing の待ちは pointer が動く間 0。手は (1) 文字の draw をまとめる、(2) draw ごとの状態の書き直しを減らす、(3) 非同期の実行器、の順）
Phase disposition: normal
承認: 2026-09-30 Q1 の指示（全 WS の L2 がユーザーの確認と WS102 を残すのみになった後、WS075 は L3 へ）。

## 方法

- tree 07122e3f（main を取り込んだ後、p029 の blur の既定: Settings だけ有効）の demo の passthrough の image。5330 の QEMU の VFIO passthrough、
  1 boot で: 10 app（apps8.sh、Settings を含む）→ `rate A 10` → `c6 A 40 960 100` → `engine-gdb.sh`（session ごとの engine・queue・round）→ Terminal
  で session.log を home へ copy して sync（stop の後に image から読む: zdesktop の ZWL PERF）。executor の session ごとの frame の log
  （`i915: vk: session N: frames ...`、p026）は kernel log から。script `build/ws075-p030/run.sh`。
- 実験 1 run: `exp-double-small.patch`（compositor の glass の shape のうち 64×64 px 以下（ほぼ文字）を同じ draw でもう 1 回描く: 増えた draw の数と
  時間の差から、draw 1 つの固定の費用を出す。絵は変わる、計測だけ）。

## 1 frame の内訳（base、1 run）

| 物差し | 値 | 出どころ |
| --- | --- | --- |
| C6 | 中央値 58.2 ms・p90 126.2 ms（1 run。p029 の 5 run は 67.3 ms） | c6 |
| 10 app の flip の率 | 20.2/s | rate |
| 合成の frame（pointer が動く間） | 約 22 frame/s、frame の開始から fence まで 44〜45 ms、うち submit+present（executor の同期の実行を含む）39〜40 ms、acquire 0.4 ms、残りの合成の CPU（記録・窓の集め）約 3 ms | ZWL PERF |
| event loop（pointer が動く間） | poll 0.1%・work 99.8%（待ち無し: pacing の待ちは 0、p026 の手が効いている） | ZWL PERF |
| compositor の submit | frame あたり 2 つ（合成と present の copy）。合成は draw 473・run 4（うち slot が尽きて途中で走る 3）| executor の log |
| executor の中の時間（frame あたり） | 同期の run 39.5 ms、記録 2.7 ms | executor の log（10 app の 3 窓の平均、pointer の止まった時間を含む） |
| GPU（engine） | compositor の 1 run 6.1 ms、queue の待ち 0.5 ms、queue から終わりまで 7.3 ms。compositor の engine の占有 60.2%、frame あたり約 35 ms | engine-gdb |

読み: frame ≒ GPU 約 35 ms + 同期の往復と他の client の後ろの待ち 約 5 ms（run 5 回 × 約 1.2 ms）+ executor の記録 約 3 ms + 合成の CPU 約 3 ms
≒ 45 ms。pacing の待ちは pointer が動く間は無い。C6（中央値）は frame の約 1.2〜1.4 倍。

## draw の固定の費用（exp-double-small、1 run）

| | base | double |
| --- | --- | --- |
| 合成の frame の draw | 473 | 871（+398: 64×64 px 以下の shape、ほぼ文字） |
| frame あたりの同期の run・記録 | 39.5・2.7 ms | 47.3・5.0 ms |
| 合成の frame（ZWL PERF） | 44〜45 ms | 53〜55 ms |
| C6 中央値 | 58.2 ms | 81.7 ms |

- 小さな draw 1 つの費用: GPU と同期 約 20 µs、executor の記録 約 6 µs、合わせて 約 25 µs（frame の差 約 10 ms ÷ 398）。
- base の 473 draw のうち約 400 が小さな shape（文字）。draw の固定の費用は frame あたり 約 12 ms（473 × 25 µs）、そのうち文字の分が 約 10 ms。
- 残りの GPU 約 23〜25 ms は画素の仕事（壁紙の全画面、窓の本体と影、すりガラスの shader）。

## L3（C6 中央値 50 ms 以内）への手（効きの見積もりと危険の順）

C6 ≒ frame × 1.2〜1.4 なので、C6 50 ms には frame 約 36〜40 ms（今 45 ms から 5〜9 ms）が要る。

| 順 | 手 | 見積もり | 危険 | 確かめ方 |
| --- | --- | --- | --- | --- |
| 1 | **文字の draw をまとめる**（compositor: 1 つの文字列の glyph を 1 つの draw にする。glyph ごとの quad を instance か 1 つの vertex buffer にして 1 回の vkCmdDraw） | draw 473 → 約 100、frame で 約 8〜10 ms 減（25 µs × 約 370）→ frame 約 36 ms、C6 約 45〜50 ms | 中（glass.c の文字の描き方と panel.frag の入力を変える。見た目は同じにできる: 画素の比較で確かめる） | 実装して C6 の 5 run と画素の比較 |
| 2 | **draw ごとの状態の書き直しを減らす**（executor: 同じ batch の続く draw で STATE_BASE_ADDRESS・PIPELINE_SELECT・cache の invalidate・変わらない 3DSTATE を出さない。slot ごとの 16 KB の状態の書き直しを差分に） | 固定の費用 25 µs の一部（GPU の 20 µs の半分程度と記録の一部）→ frame 約 4〜6 ms 減 | 中〜大（executor の状態の追跡、全ての Vulkan の client に効く。p023 の (b) の PIPE_CONTROL だけを除いた実験は効かなかった） | 1 run の実験（SBA と PIPELINE_SELECT を batch の最初だけにする build）で効きを先に測る |
| 3 | **非同期の実行器**（p018 の再検討: compositor の submit を GPU の完了を待たずに返し、present の copy と次の frame の記録を GPU と重ねる） | 同期の往復と記録と CPU の重なり分、frame 約 5〜8 ms 減 | 大（executor の submit・fence・resource の寿命の扱い全体） | 1〜2 の後に残りを見て決める |

1 だけで L3 に届く見込み（見積もりの幅の上端）。1 と 2 で余裕を持つ。3 は 1・2 の後で要るときだけ。

## 未実施

- 各手の実装（p031 以降）。2 の 1 run の実験（状態の書き直しを除く build）は、この Phase では行っていない（1 の見積もりで L3 に届くため、次の Phase の最初に）。
- 素の 5330。
