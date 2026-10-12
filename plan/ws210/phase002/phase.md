<!-- awesome-plan project=zedbsd record=ws210-p002 -->

# ws210-p002: spike — bcm2711 の shader の大きさと Pi の draw・頂点・fragment の費用

Parent: [WS210](../ws.md)
Status: in-progress（P1。host の部分を実施、Pi の部分は未着手）
Phase disposition: normal
Queue: [P1 の lane](../../agents/P1/queue.md)

## 由来と範囲

- [p001](../phase001/phase.md) の設計 1.2 の「未確認」と review-1 の H3・M11。2026-10-12 Q1:「p002 の spike の host の部分（plan/ws210/tests/ の中、source は変えない）は進めてよい。」
- (1) host: B2 の候補の shader を bcm2711 の compiler（kernel の source を host で link）に通して数える。(2) Pi: draw あたりの kernel の費用・頂点の scalar あたりの写し・pipeline ごとの fragment の時間・compile の時間（Pi の操作は Q1 経由、未着手）。
- 道具は spike 専用（`plan/ws210/tests/spike/`）。WS の終わりに回帰として残すか消すかを決める（試験の整理の基準）。

## (1) host の数（2026-10-12、agent/p1 = main 9f1e501c1 の compiler、P3 の NOP の削減の前）

道具: `sh plan/ws210/tests/spike/count.sh [出力]`（既定 `build/ws210-spike`）。GLSL（`plan/ws210/tests/spike/*.vert`・`*.frag`、glslc `-O`＋spirv-val）を `count.c` が `drv_gpu_shader_parse` と `bcm2711_shader_compile`（`spirv.c`・`shader*.c`・`qpu.c` を host で link、Mesa の oracle は使わない）に通し、命令の数、その中の何もしない命令（両 ALU が idle で signal 無しの word、`bcm2711_qpu_encode` の零の命令）の数、残り（work）を出す。

結果: **10 の候補（9 つ＋round の単色の版）は全部 compile が通った**（varying 4〜15、register の不足・命令の上限の拒否は無い）。`registers_used` と `threads` は全 program で 64 と 2（compiler が固定の値を出す。register の余裕は数では見えないので「compile が通る」で判定する）。

| pipeline | varying | fragment（blend 無し） | fragment＋shader の blend | vertex | coordinate |
| --- | --- | --- | --- | --- | --- |
| fill | 9 | 186（work 63） | 231（work 78） | 270（90） | 213（71） |
| round | 15 | 324（109） | 369（124） | 342（114） | 249（83） |
| round（単色） | 10 | 213（72） | 258（87） | 255（85） | 192（64） |
| shadow | 10 | 225（76） | 270（91） | 255（85） | 192（64） |
| ring | 11 | 249（84） | 294（99） | 267（89） | 198（66） |
| capsule | 10 | 189（64） | 234（79） | 255（85） | 192（64） |
| atlas | 7 | 135（46） | 180（61） | 252（84） | 207（69） |
| image | 8 | 204（69） | 249（84） | 204（68） | 153（51） |
| replace | 4 | 54（19） | 99（34） | 216（72） | 189（63） |
| triangle | 7 | 99（34） | 144（49） | 252（84） | 207（69） |

（命令の数、括弧は NOP を除いた work。約 66% が NOP、P3 の所見の「全命令の後に 2 つの NOP」と合う。shader の中の blend は fragment に約 45 命令（work 15）を足す。）

比べ（同じ道具、compositor の `panel.frag`）: mode で分けない版 1680 命令（work 561）、mode ごとの variant 174〜684（例: 角丸の mode 1 は 291）。→ 第 1 版の「1 本の shader」（H3）は panel の generic と同じく 1600 命令級になり、形ごとに分ける 1.1 の設計で 1/5〜1/10 になる。

### 見積（理論の値、Pi で未確認）

V3D 4.2（Pi 4）を 8 QPU × 4 lane/cycle × 500 MHz ≈ 16 G lane-命令/s と置く（公開の仕様からの推定。clock は P3 の log の 500 MHz）。NOP も 1 cycle を使う。

| 場面 | 今の compiler（blend 込み） | NOP が無くなれば（work だけ） |
| --- | --- | --- |
| 1904×1020（1.94 M px）を fill で 1 層 | 約 28 ms | 約 9.5 ms |
| 同じ面積を replace で 1 層 | 約 6.5 ms（blend 無し 54 命令） | 約 2.3 ms |
| 同じ面積を round で 1 層 | 約 45 ms | 約 15 ms |
| 2000 形の頂点（12000 頂点 × coordinate＋vertex 約 450 命令） | 約 0.3 ms | — |

→ 分かったこと:
1. **fragment の命令の数が Pi の時間を決める**。窓の全面の 1 層で数〜数十 ms。角丸の内側を fill に分ける設計（B1）は要る。さらに、**不透明の単色の塗り（alpha 255 の fill・角丸の内側）は replace の pipeline（blend 無し、54 命令）で描く**（結果の画素は同じ: 不透明の source-over は置き換え）。設計 1.2 の B2 に足す。
2. P3 の hardware blend と NOP の削減は WS210 の shader 全部に 3 倍前後効く（work の列）。WS210 の Pi の時間の目標はこの改善の後の数で判断する（両方の数を p006 に書く）。
3. 頂点の shader の費用は小さい。頂点の kernel の CPU の写し（scalar ごと）は (2) の Pi で測る。

## (2) Pi（未着手）

- 測る物: (a) 1 draw に 2000 quad・200 draw に 10 quad・2000 draw に 1 quad の vkQueueSubmit〜fence の時間と kernel の perf の行（draw あたりと頂点の scalar あたりの費用を分ける）、(b) pipeline ごとに 1024×768 の全面を 10 層描く時間（fragment の費用、上の見積と比べる）、(c) 10 の pipeline の作成の時間（kernel の compile）、(d) 色を float の bit で渡す属性が bit のまま届くか、(e) 同じ場面を CPU の canvas と比べる readback の一致。
- 必要な物: 上の shader で offscreen に描く小さな Vulkan の program（arm64 の build、tree の libvulkan に link）。作り方（`userland/tests/` に置くか、plan の中の source を named build で作るか）と Pi の上での実行（P3 の Pi、Q1 経由）を Q1 と決める。

## 結果の記録

- 2026-10-12 P1: (1) の道具と GLSL を作り、count.sh を実行（上の表）。arm64 の build・Pi・QEMU は未実施。
