<!-- awesome-plan project=zedbsd record=ws211-p008 -->

# ws211-p008: 窓の小さな更新（Calendar の hover）を、client が damage に書いた所だけ描く

Parent: [WS211](../ws.md)
Status: in-progress（実装 agent/p3 db55254d4、Pi の place23 で確かめ待ち。2026-10-12 P3）
Phase disposition: normal
Queue: P3（2026-10-12 Q1 の割り当て）

## 由来

- 2026-10-12 ユーザー（Pi、place19 の wayland と place22 の Calendar）:「Calendarですが、150msくらい遅れてポイントされますね。」「ボタンはサイズが一緒なので、ポイント時の画像を1つ作っておいて、ポイントされたらそれを背景に描画するだけであって、100msも遅延するはずがないのですが。」
- Q1: KWL LAT（571 コマ）の submit→shown は p50 61ms・p90 87ms、draw→shown は p50 92ms・p90 125ms。app の hover の部分の描画は 1ms 未満（P1）。小さな damage の時の compositor の 1 コマの GPU の仕事を、damage の大きさに比例させる。

## 測った事（place22 の log: session.log・kwl.log・kernel.log）

- compositor の damage: `KWL DAMAGE` の 102 回が `x=418 y=236 width=1280 height=800`（Calendar の窓の全体）。Calendar は hover の時に parts（ボタン）だけを present し、libvulkan の WSI が `wl_surface.damage_buffer` で送る。それなのに `damage.c` の `kwl_damage_commit` は、client の damage を見ずに body の全体を damage にしていた。また、GPU の絵の `committed_damage` は誰も消さない（消すのは shm の `surface_upload` だけ）ので、damage の和は一度だけ大きくなり、その後はずっとその大きさだった。
- driver: render pass の tile は renderArea の中だけを回している（時計だけのコマは `tiles=30/30`、全画面は `510`）。copy pass も同じ箱。つまり GPU の費用はもう damage に比例しており、damage が窓全体なのが原因。
- Calendar の hover の窓（kernel.log、`ms=2203 subs=44 passes=22/0/22 tiles=6094/6094`）: 平均 277 tile は (294+260)/2 と一致する。
  - compositor の render pass: 294 tile（窓の箱 21×14）。
  - client の present の quad: 260 tile（1280×800）。
  - `gpu tiles draw cycles/tile=75918` は 2 つの平均。client の 1 draw の全面の quad（同じ log の全面 1 draw のコマで 11.1k cycles/tile）を引くと、compositor は約 133k cycles/tile、1 コマで約 39M cycles（500MHz で約 78ms）。
  - copy は 3.2k cycles/tile。TFU は 1 コマに約 2ms（`tfu jobs=22 avg_us=1954`、client の絵の全体の変換）。
  - render の平均は 1 submission あたり 21.9ms × 44 で、2.2s のうち 963ms。KWL LAT の submit→shown と合う。draw→acquired の 29ms（frame 181）は、前のコマで GPU が詰まって swapchain の image が空かない待ち。

## 直し（agent/p3 db55254d4）

- `damage.c`: `damage_client_part` を足した。commit の damage（buffer の画素、`wl_surface.damage`・`damage_buffer` の箱）を、窓の矩形の左上へ移して矩形の中に切る。glass の look（`kwl_glass_body_damage` の body）と plain の look の両方。
  - 次の時は今どおり窓の全体: damage の無い commit、絵の外だけの damage、絵を別の大きさで描く窓（viewport・buffer scale）。
  - 大きさの変わる commit・sub-surface・popup などは、今どおり出力の全体。
- `display.c` の `adopt_commit`: shm でない絵の `committed_damaged` を毎回消す。こうして、次の commit の damage は新しく始まる。shm は今どおり `surface_upload` が行を写した後に消す。
- 見込み: hover のセル（104×109）は 9 tile。buffer age で直前のセルと合わせても 12〜18 tile。compositor の GPU は約 78ms から 2〜5ms へ。

## 確かめ

- build: rpi4 の wayland（`make build/arm64/bin/wayland -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete`）で warning 0（-Werror）。amd64・Linux・FreeBSD の build は未実施。
- host 試験: `plan/ws211/tests/damage-host-test.sh` PASS。9 件: hover のセル、damage 無し、INT32_MAX の全体、端をはみ出す damage の切り、絵の外の damage、viewport の窓、plain の look、大きさの変わる commit。
- 実行していない確かめ: `display.c` の消し方は host 試験なし（review だけ）。
- Pi: place23（scratchpad/place23、`p23-wayland` cksum 1006250640、main 24d5c6c93 の render thread を含む）で確かめる。見ること:
  - Calendar の hover で `KWL DAMAGE` がセルの大きさであること。
  - kernel の `v3d: perf` の `tiles` と render の時間。
  - KWL LAT の submit→shown と draw→shown。
  - ユーザーの目で、遅れが無いこと。
- T1（i915・Venus）は WS の最後にまとめて依頼する。

## 残り（範囲外、提案）

- client（libkeiland `present.c` の `present_record`）は、毎コマ窓の全体に quad を描く（renderArea も全体）。Pi の GPU で約 6〜7ms。renderArea を「parts と、この swapchain image が最後に描かれてから後の parts の和」に絞れば、client の render も driver の copy も部分になる。WS210 の範囲なので Q1 に提案する。
- TFU による client の絵の全体の変換（コマに約 2ms）。WS141 の候補。
- compositor の窓の領域の tile あたりの費用（約 133k cycles/tile、1 画素に 32 cycle）は高い。damage を絞った後にまだ効くなら、別の Phase で調べる。
