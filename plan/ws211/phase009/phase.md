<!-- awesome-plan project=zedbsd record=ws211-p009 -->

# ws211-p009: compositor の damage を、1 つの外接の四角でなく複数の四角のまま描く

Parent: [WS211](../ws.md)
Status: in-progress（設計・実装、2026-10-12 P3）
Phase disposition: normal
Queue: P3（2026-10-12 Q1 の割り当て）

## 由来

- 2026-10-12 ユーザー:「画面更新の分割って、もっと高度化できそうですか？無理ですか？」→ Q1 の案 1〜4 にユーザー「ぜんぶやりましょう。」（master.md の決定の記録）。案 1（1 damage を複数の四角のまま描く）がこの Phase。案 3（変わらない層の命令の使い回し）は p010（P4）、案 4 は WS210 p013、案 2 は WS214。
- 今（p008 の後）: `damage.c` の `damage_add` は変わった所を 1 つの外接の四角にまとめ、`compose.c` の `compose_region` は buffer age の数コマ分の四角も 1 つの外接の四角にまとめる。離れた 2 か所（時計と Calendar のボタン、2 つの窓）が変わると、間の全部の tile を描く。

## Pi の数字（place23 の kernel.log、800x480 = 13x8 tile、`v3d: perf`）

- 1 コマ = render pass 1 つ（compositor）＋ image pass 1 つ（present の写し、書いた箱の tile だけ）。
- render: 全面 104 tile で 7〜9 ms（約 35〜80 µs/tile）。時計だけのコマは 13 tile で 1〜2 ms。tile の数にほぼ比例し、pass ごとの固定の GPU の費用は 1 ms 未満で見分けられない。
- kernel の準備（prep）: draw あたり約 30〜50 µs（67 draw のコマで 2〜4 ms）。draw 1 つの pass で約 0.5 ms。
- present の写し: 約 6 µs/tile（161 tile で 1 ms）。render の 1/10 だが、1920x1080（510 tile）の外接の箱では 3 ms 前後になる。
- p008 の Calendar の hover の測り: compositor の窓の領域は約 133k cycles/tile（1 画素 32 cycle）で、費用は描く画素（fragment）にほぼ比例する。

## 設計

### D1 四角の list（damage.c、kwl.h）

- `server->damage` を四角の list（最大 `KWL_DAMAGE_RECTS` = 4）にする。`server->damaged` はそのまま「list が空でない」の印で、0 にする所（compose.c、scanout.c）は今のまま（次の `damage_add` が空から始める）。
- 合わせる規則（`kwl_damage_rects_add`、純粋な関数、compose.c の buffer age の和も同じ関数を使う）:
  1. 画素で重なる 2 つは必ず合わせる（外接の四角）。四角ごとに draw を出すので、重なりがあると重なりに同じ quad が 2 度描かれ、半透明の物（影・glass）が 2 度重なる。合わせた結果が別の四角と重なれば、それも合わせる（重なりが無くなるまで）。こうして list の四角は互いに素である。
  2. 合わせても費用がほとんど増えない 2 つは合わせる: 外接の四角の面積 ≤ 2 つの面積の和 ＋ `KWL_DAMAGE_JOIN_AREA`（64×64 画素）。根拠: 分けたままの費用は、2 つにまたがる draw（壁紙、窓の body、影）を四角ごとに出し直す分で、Pi で draw 1 つ約 40 µs、数 draw で約 0.2〜0.5 ms ≒ 1 tile（4096 画素 × 32 cycle ≒ 0.26 ms）の画素の費用。Pi の tile の大きさを知らなくても、画素の面積で決まる（解像度に依らない）。
  3. 数が上限を超えたら、外接の面積の増え（外接 − 和）が最小の組を合わせ、上限以内にする。
- `probes_damaged`: 変わった probe（光の reach 込み）が list のどれか 1 つの四角に入っていること。`kwl_hover_damage` が足す四角は合わせると大きくなるだけなので、どれか 1 つに必ず入る。

### D2 buffer age の和（compose.c `compose_region`）

- 履歴（`KWL_DAMAGE_HISTORY` コマ）は各コマの四角の list のまま持つ（`history[slot][KWL_DAMAGE_RECTS][4]`、`history_count[slot]`、`history_whole[slot]`）。
- 描く image が逃したコマの list を `kwl_damage_rects_add` で 1 つの list（≤ 4、互いに素）にし、出力に切る。
- 全体を描く条件: 今の「外接の箱が出力の 70% を超える」を「四角の面積の和が 70% を超える」に変える（外接の箱が大きくても、描く画素が少なければ部分で描く）。
- 結果: 外接の箱（renderArea と基本の scissor）と、四角の list（2 つ以上の時だけ「clip」として使う、1 つなら今と同じ）。`KWL DAMAGE` の log に `rects=N` と各四角を足す（x・y・width・height は外接の箱のまま）。

### D3 1 つの render pass、四角ごとの scissor（compose.c、compose-bind.c、glass.c、batch.c）

選んだ形: **1 つの pass（renderArea = 外接の箱）で、各 draw を、それが掛かる四角ごとに scissor を変えて出す。** 走査（walk）は 1 回。

- pass を四角ごとに分ける形は採らない: (a) 四角ごとに walk し直すと、glass の描画の状態（hover の probe、App Home の層、glyph の upload、時間の凍結）を 2 度動かし、CPU も N 倍。(b) list を四角ごとに replay し直すと、glass の backdrop（pass の途中の blur の小さな pass）も N 回になる。(c) Pi では pass を分けると kernel の job（bin＋render＋present の写しの書いた箱）も分かれる。
- draw の出口は 3 つだけ（`compose_quad_part`、`glass_shape_draw`、`kwl_batch_flush`）。どれも quad の出力の画素の箱を知っているので、`kwl_list_draw` の代わりに `kwl_compose_clip_draw(server, command, x, y, w, h, vertices)` を呼ぶ:
  - clip が無い時（全体のコマ、四角が 1 つ、backdrop の小さな pass、層の offscreen の pass、head の pass、anchor の pass の外）: 今と同じ 1 draw。
  - clip が有る時: 各四角と今の scissor（`scissor_now`、sheet の cut などで狭まる）の共通部分が quad（1 画素の余裕）に掛かれば、その scissor を設定して draw。四角は互いに素なので、どの画素も 1 度しか描かれない。
- 見えない quad を落とす判定（今の `scissor_now` との比較、2 か所）は `kwl_compose_clip_visible` にまとめ、clip が有る時はどれかの四角に掛かる quad だけを通す（bind・push constants も出さない）。
- batch（床の線を 1 draw にまとめる）は、まとめた quad の箱の和を持ち、flush で同じく四角ごとに出す。
- scissor の命令を無駄に出さないため、helper は最後に設定した scissor と、draw-list の scissor の通し番号（`kwl_list` の `scissor_serial`、`kwl_list_set_scissor`・`begin_render_pass`・`reset` が進める）を覚え、間に誰も scissor を設定していなければ同じ scissor を出し直さない。誰かが設定した後は必ず出し直す（cut の scissor を戻した後に古い clip の scissor が残ったと誤って思うと、外接の箱の中の四角の外に quad を重ねて描き、半透明の物が 2 度重なる）。
- clip が効くのは anchor の pass の間だけ: `compose_record` が pass を始めた後に設定し、cursor を描いて pass を閉じたら外す（shot・heads の記録には効かない）。cursor も四角で切る（今の 1 つの箱でも切っている。動いていない cursor は image に残っている）。
- glass の backdrop（`kwl_backdrop_end`）は pass を出力の全体で再開していた。外接の箱（`compose->pass_area`）で再開する（scissor は今どおり）。

### D4 周りを読む物（blur・影・光）

- compositor は描いている出力の画素を読まない: glass の blur は事前の blurred の壁紙か、backdrop（窓の下の場面を小さな image に描き直して blur）を sample する。影・光は quad の形で描く。なので四角ごとの scissor でも、四角の中の画素は全体を描いた時と同じになる。backdrop の小さな pass は `backdrop_recording` で clip も見えない quad の判定も外れ、場面の全部を描く（今どおり）。
- 周りに及ぶ変化の広さは damage を作る側が持つ（hover の `DAMAGE_HOVER` 96 画素が光と blur の reach、`kwl_glass_body_damage` は近くに glass が無い時だけ body に絞る）。四角を合わせても広がるだけなので、reach は失われない。
- cursor の無い場面の写し（`kwl_scene_copy`、cursor の plane が無い時だけ）は、外接の箱ではなく四角ごとに写す（`VkBufferImageCopy` を四角の数だけ）。外接の箱で写すと、四角の外に残る前の image の cursor を場面に取り込む。

### D5 WS213（draw-list・描画の thread）・p010 との関係

- 変えるのは記録（main thread の walk）の側だけ。四角ごとの scissor と draw は list の普通の命令で、render thread の replay は変えない。
- p010（P4、変わらない層の命令の使い回し）への注意: 部分のコマの命令はそのコマの四角の scissor を含む。使い回す命令は四角の組で区別するか、四角の無い（全体の）記録だけを使い回す必要がある（Q1 に伝えた）。

### D6 driver（WS141 の src/drivers/gpu/bcm2711/、HAL・UAPI は変えない）

1 つの pass で四角ごとに scissor を変えても、v3d の render は renderArea（外接の箱）の全部の supertile を回す（tile の load と store）。そこで:

- **render pass の tile の選び（native-pass.c、vulkan-native-pass.c、vulkan-native-bin.c）**: draw ごとの clip window（drawable ∩ renderArea ∩ viewport ∩ scissor、bin の準備で計算済み）を draw に残し、pass の draw の clip window の tile の箱（最大 `BCM2711_NATIVE_PASS_BOXES` = 8、同じ・含まれる箱は 1 つ、超えたら今どおり全体）を `struct bcm2711_native_pass` に入れる。render の list（RCL）は、箱のどれかに掛かる supertile だけを並べる（measure と encode が同じ判定を使い、list の長さは正確）。
  - 正しさ: tile の load が出力そのものを読む pass（`state.load != 0`、`source == 0`）では、どの draw も触れない tile は load した bytes をそのまま store するだけなので、回さなくても image は同じ（Vulkan の意味も同じ）。native の clear（`load == 0`）の pass、copy の pass、draw の無い pass は今どおり全部を回す。
- **書いた箱（vulkan-native-execute.c、v3d-memory.c、buffer.h）**: render pass は書いた箱を箱の数だけ記録する（4 つまで、超えたら外接の箱 1 つ）。記録の ring を 8 から 16 に増やす（1 コマで pass が分かれても present の写しが差分のままでいられるように）。
- **present の写し（vulkan-native-image.c）**: 前の写しからの変化を、外接の箱 1 つではなく箱の list（`bcm2711_v3d_memory_changes_boxes`、≤ 8、同じ・含まれる箱は 1 つ、超えたら外接 1 つ）で受け、copy pass の tile もその箱の supertile だけにする（area は外接の箱）。snapshot（native-storage.c）は今どおり外接の箱。
- 1920x1080 以上は supertile が 2×2 tile（128 画素）なので、選びは 128 画素の粒になる。800x480 は 1 tile。

## 見込み

- 時計（右上）と Calendar の hover のセル（中央）: 今は外接の箱で 800x480 のほぼ全部（約 80 tile）、分けると 2〜3 tile ＋ 時計の 2〜3 tile。1920x1080 では外接 300 tile 前後が 10 tile 前後。
- 増える物: 2 つの四角にまたがる draw（壁紙・窓の body・影）の出し直し。Pi の kernel の準備で 1 つ約 40 µs。

## 確かめ

- build: rpi4 の wayland と vmunix（`-o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete`）で warning 0。
- host 試験: `plan/ws211/tests/damage-host-test.sh`（四角の list: 重なりの合わせ、近い 2 つの合わせ、離れた 2 つ、上限、互いに素、p008 の 9 件）、`plan/ws211/tests/native-pass-boxes-host-test.sh`（supertile の選び、list の長さ、全体への戻り）。
- Pi（Q1 が転送、ユーザーの操作）: 時計の分と Calendar の hover が同時の時の `KWL DAMAGE rects=2`、`v3d: perf` の `tiles` と render・copy の時間、見た目（四角の境目に跡が無いこと、半透明の物が濃くならないこと）。
- T1（i915・Venus）: WS の最後にまとめて依頼する。

## 結果

（実装の後に書く）
