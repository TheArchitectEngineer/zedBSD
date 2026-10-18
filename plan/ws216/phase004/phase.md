<!-- awesome-plan project=zedbsd record=ws216-p004 -->

# ws216-p004: Notes の画面（形の列の描画、Normal・Multiply、粒の texture、遅延の pipeline と代わり、頂点の塊）

Parent: [WS216](../ws.md)
Status: cleared（2026-10-18 Q1: ws.md の表に記録済みの判定に合わせた。表: cleared（2026-10-13 Q1: T1-580 の QEMU の AAT で機械の判定 PASS・画を Q1 が見て合格。Pi 4 は p009、5330 の感触は p007 の A5））（旧: in-progress（2026-10-13 P1: 実装と host の受け入れ（C1〜C8）済み。test-wait: QEMU・実機の画の確かめは p007 で T1、Q1 の判定まで cleared にしない））
Phase disposition: normal
Queue: Q1 の依頼（2026-10-13、P1）。承認は ws.md の「ユーザーの決定」（D1〜D12）と review-2 の判定（p004 に当たる指摘はこの phase.md で解決してから実装）

## 範囲と入力

- 設計は [p001 第 2 版](../phase001/phase.md) の §2・§3.2・§3.3・§4.2・§6 と §8 の p004 の行。review は [review-1](../phase001/review-1.md)・[review-2](../phase001/review-2.md)。形の列・焼き込み・粒の tile は [p003](../phase003/phase.md)（main に入った）。
- 触る file: `userland/desktop/notes/`（`render.c`・`geometry.c`・`app.h`・`notes.h`（粒の平均の定数だけ）・`shape.c`（鉛筆の outline だけ）・`main.c`（stroke を描く 2 か所）・`shaders/` と生成の `shaders.h`）、`plan/ws216/`。
- 触らない: 塗りつぶしを作る所（p005）、UI（p006）、libpdf、編集 data・journal・PDF（p003 で済み）、HAL・toolchain。
- 試験の方針: build（amd64・rpi4、Linux の host rules）warning 0 と host 試験（plain・ASan・UBSan）。QEMU・実機は p007（T1）。

## 1. review-2 の指摘のうち p004 に当たる物

| # | 指摘 | この段の決定 |
| --- | --- | --- |
| M-1・L-1 | push constant は target の大きさ（vec4 の xy）だけで、頂点は CPU が `notes_view`（x・y・scale）で画素に置く（`geometry.c` の `view->x + p × view->scale`、`draw.vert` は大きさで割るだけ） | push constant では形を置かない（今どおり CPU が置く）。粒だけが「頁の点」を要るので、push constant の 2 つ目の vec4 に「その draw の頁の原点（画素）と 1 画素あたりの cell の数 `1 / (48 pt × scale)`」を入れ、`grain.vert` が画素の位置から粒の uv を出す（§3）。原点と倍率は draw ごとに geometry が draw に持たせる（頁の絵の frame は原点 (0, 0) と絵の倍率、window の frame は `view`）。帯の画面外の描画（p005）も同じ口（view を替えて頁の frame を組む）を使う |
| M-9 | 範囲の輪郭の継ぎ目の辺は「最初の角の index」で名指す（p002 §1.2、`seam_start`・`seam_end`、無ければ `PDF_OUTLINE_NO_SEAM`） | geometry は辺 i（角 i → 角 i+1、最後の角 → 最初の角が閉じる辺）が `seam_start` か `seam_end` の時、その辺の fringe を出さない（§2） |
| L-6 | 倍率が約 0.89 px/pt 未満では一番粗い段（64×64）でも 1 画素 1.5 texel を超える | 4 段目は作らない。制限として記録（小さな window の全体表示で粒が少しちらつく。段の切り替えの倍率で粒の見え方が跳ぶことも制限） |
| L-11 | descriptor の pool は `maxSets = NOTES_TEXTURES`。sampler は NEAREST・CLAMP | 粒の 3 段の set を `NOTES_TEXTURES` に足す（4 → 7、pool も 7）。粒は別の sampler（REPEAT・LINEAR）。頁の絵などの sampler は変えない |
| L-15 | 「頂点の塊（16 MiB の上限）」は 4 MiB の塊の読み違え | 塊は 4 MiB 以下。16 MiB は i915 の 1 確保の上限で、塊はそれに当たらない大きさ |
| L-17 | 描画は頁の絵の pass と window の pass の 2 つ | 1 つの draw（fan・fringe・cover のどれか）は 1 つの pass の中に収まる。塊をまたぐ draw は同じ pass の中で続けて記録するので、stencil の値は保たれる |
| S3（review-1） | 頂点の vertex buffer が 16 MiB を超える | §4 の塊 |
| C2（review-1） | Pi の driver は Multiply の係数と R8・mip を受けない、初期化ごと失敗した | §5 の遅延の pipeline と代わり。粒は `B8G8R8A8_UNORM`・mip 1 の別々の 3 つの texture |

## 2. 形の描き方（geometry.c）

- 新しい口 `notes_frame_stroke(frame, stroke, view)`: stroke の形の列（`shapes`）を順に描く。色は stroke の色、各形の alpha は「色の alpha × 形の alpha」。
- 1 つの形は今の 3 pass（fan → fringe → cover）で塗る:
  - fan: 輪郭ごとに、その輪郭の最初の角から fan（複数の輪郭の winding は足し合わされる、nonzero）。
  - fringe: 全部の輪郭の全部の辺。継ぎ目の辺（§1 M-9）は出さない。
  - cover: 全部の輪郭の箱を 1 回。
- ペン・蛍光ペン（SOLID・NORMAL、alpha 1、輪郭 1 つ、継ぎ目なし）の頂点は今の `notes_frame_polygon(outline)` と bit 同一（host 試験で比べる）。
- 頂点の値（8 float: x・y・u・v・r・g・b・a）:

| 形 | pipe（fringe・cover） | 色 (r, g, b, a) | v（今は 0） |
| --- | --- | --- | --- |
| SOLID・NORMAL | FRINGE・COVER | (cs, a) | 0 |
| GRAIN（鉛筆） | FRINGE_GRAIN・COVER_GRAIN | (cs, ā)。ā = 1 − (1 − a·g_min)(1 − a·h̄) は代わりの平らな濃さ | a |
| MULTIPLY（水彩） | FRINGE_MULTIPLY・COVER_MULTIPLY | (cs, a) | 0 |

  - h̄ は粒の tile（level 0）の平均（`NOTES_GRAIN_MEAN`、notes.h の定数。host 試験が tile から計算した値と一致を確かめる）。
  - 代わり（§5）は pipe を FRINGE・COVER に替えるだけで頂点を変えない: 粒の代わりは色 (cs, ā) の source-over、Multiply の代わりは (cs, a) の source-over（p001 §3.3 の Normal の近似）。
  - GRAIN と MULTIPLY が両方の形は無い（鉛筆は NORMAL、水彩は SOLID）。両方が立った形は GRAIN として描く。
- 粒の draw は頁の置き場所（原点 x・y の画素と倍率 px/pt）を `struct notes_draw` に持つ（§1 M-1）。
- 鉛筆の `outline` は最初の区間の polygon にする（p003 では画面のためだけに全体の輪郭をもう 1 つ作っていた。画面は形の列を描くので要らない。`bounds` は全部の形の角の箱のまま、当たりの判定は点の列と見える半幅なので影響しない）。

## 3. shader と push constant

- push constant は 32 byte、stage は VERTEX、全部の pipeline が同じ layout: `size`（target の大きさ、xy）と `grain`（頁の原点の画素 x・y、1 画素あたりの cell の数 `1 / (NOTES_GRAIN_CELL × scale)`、未使用）。今の `draw.vert` は 1 つ目の vec4 だけを読む（変えない）。
- `grain.vert`（新）: 位置と色は `draw.vert` と同じ。`extra` を vec4 にして (u, v, 粒の uv) を出す。粒の uv = (画素の位置 − 原点) × cell の数。頁の左上が tile の (0, 0)、y は下向き（p003 B9 の PDF の向きと同じ）。varying の数は今と同じ 2 つ（vec4 と vec4）。
- `grain.frag`（新）: coverage = `1 − smoothstep(0, 1, |u|)`（`fill.frag` と同じ）、a = v、h = 粒の texture の r、α = coverage × (1 − (1 − a·g_min)(1 − a·h))、出力は premultiplied (cs·α, α)。
- `multiply.frag`（新）: A = color.a × coverage、出力の色は `1 − A + A·cs`。blend は色 `src × ZERO + dst × SRC_COLOR`、alpha `src × ZERO + dst × ONE`。頁の絵は不透明の白から始まるので PDF の `Cb × (1 − ca + ca·Cs)` と同じ式（p001 §6.1）。
- 使う命令は今の `fill.frag`・`texture.frag` と同じ範囲（texture の sample、smoothstep、abs、四則）。SPIR-V は `shaders/regenerate.py` で作り直す（今の 3 つの shader の bytes は変わらないことを確かめた: shaderc 2025.2 で生成し直して `shaders.h` に差分なし）。

## 4. 頂点の塊（S3）

- 今の 1 つの vertex buffer（足りなければ倍にして作り直す、上限なし）を「塊の列」にする。1 つの塊は host-visible の buffer で、頂点の数は 3 の倍数（4 MiB なら 131,070 頂点）。全部の塊は同じ大きさ。
- frame の間で使い回し、足りない時だけ塊を足す（一番多かった frame の数まで増える）。頁の frame の頂点、続けて window の frame の頂点を、一続きの列として塊に詰める（頂点の通し番号 k は塊 k / C の k mod C、C は塊の頂点の数）。
- 全部の draw は三角形の列（3 頂点の倍数）で、最初の頂点も 3 の倍数から始まる（geometry の全部の口がそうする）。C も 3 の倍数なので、塊の境は必ず三角形の境。render は draw を塊の境で切り、切れごとに塊を bind して `vkCmdDraw(切れの数, 1, 塊の中の最初, 0)` を記録する（同じ pass の中で続けて記録、stencil の値は保たれる）。
- 塊の確保（`vkAllocateMemory` など）が `VK_ERROR_OUT_OF_DEVICE_MEMORY`・`VK_ERROR_OUT_OF_HOST_MEMORY` で断られたら、全部の塊を捨てて塊を半分の大きさにし、作り直す（256 KiB まで）。それでも断られたら今と同じく描画の失敗（Notes は終わる）。log: `NOTES FRAME chunk refused bytes=N error=E`。
- log（p007 の 5330 の測り）: frame の頂点の数がそれまでの最大を超え、最後に出した数の 1.25 倍以上の時に `NOTES FRAME vertices=N chunks=M bytes=B`（B は塊 1 つの bytes）。

## 5. 遅延の pipeline と代わり（C2）

- 今の 5 つ（STENCIL・FRINGE・COVER・PLAIN・TEXTURE）は今どおり初期化で作る。初期化で失敗すれば今どおり Notes は終わる。
- 新しい 4 つは 2 組: grain（FRINGE_GRAIN・COVER_GRAIN と粒の 3 つの texture・REPEAT の sampler）、multiply（FRINGE_MULTIPLY・COVER_MULTIPLY）。各組は、その組の pipe を使う draw がある最初の frame の記録の前に作る（`notes_renderer_draw` の初め、頁の frame と window の frame の draw を見る）。ペンだけの人には作られない（粒の texture も確保されない）。
- 組のどれかの作成が失敗したら、その組の作った物を全部捨てて「使えない」と覚え、一度だけ `NOTES PIPE fallback name=grain|multiply error=E` を出す。以後その組の pipe の draw は FRINGE・COVER で描く（§2 の頂点のまま、粒は平らな濃さ ā、水彩は Normal の近似）。Notes は終わらない。作れた時は一度 `NOTES PIPE made name=grain|multiply`、粒は `NOTES GRAIN hash=%08x`（level 0 の tile の FNV-1a、p003 の `2a644409`、p009 で amd64 と比べる）。
- 粒の texture: 256・128・64 の正方形、`B8G8R8A8_UNORM`、linear・host-visible（今の toolbar・背景の絵と同じ作り）、mip 1、4 channel に同じ値。level 0 は `notes_grain_tile()`、1・2 は `notes_grain_level()`（p003、整数だけなので amd64 と arm64 で同じ bytes）。作るのは組を作る時の一度だけ（Guardrail の「一度だけの用意」）。
- 段の選び方: draw の倍率 s（px/pt）で、1 画素あたりの texel `(256 >> L) / (48 × s)` が 1.5 以下になる一番細かい L（0〜2）。どれも超えれば L = 2（L-6 の制限）。

## 6. 受け入れ

| # | 条件 |
| --- | --- |
| C1 | build（amd64 `config/current-uat.mk`・rpi4 `config/ci/config-rpi4.mk` の `bin/notes`、Linux の host rules の `bin/notes`）warning 0。SPIR-V を `regenerate.py` で作り直せ、`spirv-val` を通る |
| C2 | 粒の texture の level 0 の bytes が PDF の SMask と同じ tile（FNV-1a `2a644409`）、level 1・2 が `notes_grain_level()`、4 channel に同じ値。`NOTES_GRAIN_MEAN` が tile の平均と一致 |
| C3 | **i915 の模型**（1 確保 16 MiB を超えると `VK_ERROR_OUT_OF_DEVICE_MEMORY`）で、60 万頂点を超える frame が塊（各 4 MiB 以下）に分かれ、全部の確保が 16 MiB 以下、記録した draw は全部 3 の倍数で塊の中に収まり、draw の切れを並べ直すと元の draw の頂点の列と同じ（数も値も）、1 つの draw の切れは同じ pass の中で続けて同じ pipeline で記録される |
| C4 | 確保の上限を小さくした模型（1 MiB）で塊が半分ずつ小さくなって描け、256 KiB 未満しか取れない模型では描画が失敗を返す（今と同じ） |
| C5 | 遅延: ペンだけの frame では新しい pipeline も粒の texture も作られない。鉛筆の frame で grain の組が一度だけ作られ、水彩の frame で multiply の組が一度だけ作られる |
| C6 | 代わり: grain・multiply の pipeline の作成が失敗する模型で、初期化は成功し、描画も成功し、`NOTES PIPE fallback` が組ごとに一度だけ出て、その draw は FRINGE・COVER の pipeline で記録される |
| C7 | geometry: ペン・蛍光ペンの頂点が `notes_frame_polygon(outline)` と bit 同一。鉛筆の継ぎ目の辺に fringe が無い（fringe の頂点の数 = (角の数 − 継ぎ目の数) × 6）。2 つの輪郭の塗りは fan・fringe が輪郭ごと、cover が 1 回。粒の頂点は (cs, ā) と v = a、水彩は (cs, a)。粒の draw の push constant が置き場所から計算した値、段の選び方 |
| C8 | 既存の host 試験 `run-notes-tools.sh`（p003）・`run-notes-host.sh`・`run-host-notes-edit.sh`・`run-host-clean.sh` が通る。新しい試験が plain・ASan・UBSan で通る |

試験: `plan/ws216/tests/run-notes-render.sh`（`host-notes-render.c`: render.c・geometry.c と Notes の model を直に link し、Vulkan の関数を模型で置く）。出力は worktree の `build/ws216-p004-host/`。画面の画（粒・Multiply・継ぎ目・塊の境に跡が無い）は p007 で T1。

## 7. 実行の記録（2026-10-13 P1、agent/p1、base main 0047ea2a5）

前の世代の書きかけ（設計 §1〜§6、実装、host 試験の source）は Q1 が `6b09ad269` に救った。この世代で main を merge（`7cf2cf1ac`、衝突なし）し、review して次を足した。

### 7.1 変えた所

| file | 内容 |
| --- | --- |
| `app.h` | pipe 9 つ（新しい 4 つ）と組（grain・multiply）と組の状態、塊の大きさ（4 MiB・最小 256 KiB）、粒の texture 3 つ（`NOTES_TEXTURES` 7）、`struct notes_draw` の `place`（頁の原点と倍率）、`struct notes_chunk`・`struct notes_grain_picture`、renderer の組・粒・塊の欄、`notes_frame_shape()`・`notes_frame_stroke()`・`notes_frame_grain_level()` |
| `notes.h` | `NOTES_GRAIN_MEAN`（tile の平均、host 試験が tile から計算して一致を確かめる） |
| `geometry.c` | `frame_fill()`（複数の輪郭の fan、継ぎ目の辺の fringe なし、cover 1 回）。`notes_frame_polygon()` はその 1 輪郭の形（ペンの頂点は前と bit 同一）。`notes_frame_shape()`（§2 の表の頂点と pipe）、`notes_frame_stroke()`、`notes_frame_grain_level()`（§5 の段）。**この世代で足した**: 輪郭の角の数の和が角の数と違う形は描かない（fringe の数と書く頂点の数がずれて未初期化の頂点を出すのを防ぐ） |
| `render.c` | 塊の列（§4、拒まれたら半分、log）、遅延の組（§5、作る・代わり・log、粒の 3 つの texture と REPEAT・LINEAR の sampler、`NOTES GRAIN hash=`）、push constant 32 byte、粒の draw の段の set と頁の置き場所の push、draw を塊の境で切る記録、Multiply の blend。**この世代で直した**: 最小の塊が拒まれた時も `NOTES FRAME chunk refused` を出す（log を先に）、関数の呼び出しを条件の中から出す（`memcmp`）、組の pipeline の作成を 1 つずつ確かめる形に |
| `shape.c` | 鉛筆の `outline` は最初の区間（全体の輪郭を作らない） |
| `main.c` | stroke を描く 2 か所を `notes_frame_stroke()` に |
| `shaders/grain.vert`・`grain.frag`・`multiply.frag`（新）、`regenerate.py`、`shaders.h` | §3 |
| `plan/ws216/tests/host-vulkan-mock.[ch]`・`host-notes-render.c` | Vulkan の模型と §6 の受け入れ。**この世代で直した**: 穴の fan の最初の角の期待値（160 pt、誤って 130）、塗りの stroke の解放（ASan の leak） |
| `plan/ws216/tests/run-notes-render.sh`（新、この世代） | plain・ASan・UBSan の build と実行、SPIR-V の作り直しと比較 |

### 7.2 受け入れの結果（host、gcc 14.2.0、shaderc 2025.2、SPIRV-Tools v2025.1）

`sh plan/ws216/tests/run-notes-render.sh` → `run-notes-render: ok`（plain・ASan・UBSan とも `244 checks, 0 failed`、出力 `build/ws216-p004-host/`）。

| # | 結果 |
| --- | --- |
| C1 | amd64（`ZEDBSD_CONFIG=config/current-uat.mk make build/amd64/bin/notes`）・rpi4（`ZEDBSD_CONFIG=config/ci/config-rpi4.mk make build/arm64/bin/notes`）・Linux の host rules（`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws216-linux build/ws216-linux/bin/notes`）とも exit 0、warning 0（-Werror）。`regenerate.py`（各 shader に `spirv-val --target-env vulkan1.0`）の出力が tree の `shaders.h` と同じ bytes。FreeBSD の native の build は未実施 |
| C2 | 粒の 3 つの texture（256・128・64、LINEAR tiling、`B8G8R8A8`）が tile と `notes_grain_level()` の値を 4 channel に持ち、各 set が REPEAT・LINEAR の sampler で名指す。tile の FNV-1a `2a644409`（log `NOTES GRAIN hash=2a644409`）。`NOTES_GRAIN_MEAN` が tile の平均と 1e-7 以内 |
| C3 | i915 の模型（buffer・image とも 16 MiB 超を拒む）で 646,938 頂点の frame が 5 つの 4 MiB の塊（131,070 頂点）で描け、最大の確保 16 MiB 以下・拒否 0。記録した draw は全部 3 の倍数で塊の中、切れを並べると元の頂点の列と同じ bytes、2 つの draw が塊の境で切れ、切れは同じ pass・同じ pipeline で続く。`NOTES FRAME vertices=646938 chunks=5 bytes=4194304` |
| C4 | 1 MiB の模型: 開く時に 4・2 MiB が拒まれ（log 2 行）1 MiB（32,766 頂点）の塊、633,084 頂点の frame が 20 の塊で描ける。続けて 128 KiB に下げた模型では 512・256 KiB も拒まれ描画が `VK_ERROR_OUT_OF_DEVICE_MEMORY`、128 KiB の模型では開けない。どちらも close で漏れなし |
| C5 | ペン・蛍光ペンだけの frame で pipeline は初期化の 5 つのまま、粒の texture・sampler なし。鉛筆の frame で grain の組だけ（7 つ）、水彩の frame で multiply の組（9 つ）、次の frame で何も作らない。`NOTES PIPE made` が組ごとに 1 回 |
| C6 | grain・multiply・両方の pipeline を拒む模型で、開ける・描ける、`NOTES PIPE fallback name=grain|multiply error=-3` が組ごとに 1 回、拒まれた grain は texture・sampler・pipeline を残さず、その draw は FRINGE・COVER で記録、2 つ目の frame で作り直さない |
| C7 | ペン・蛍光ペン 20 本の頂点と draw が `notes_frame_polygon(outline)` と bit 同一。鉛筆の区間の fringe の頂点の数 = (角の数 − 継ぎ目の数) × 6、頂点は (cs, ā) と v = a、fringe と cover の draw が置き場所（原点と倍率）を持つ。水彩は 3 つの形が MULTIPLY の pipe で (cs, a)。2 つの輪郭の塗りは fan 2 つ・fringe 8 辺・cover 1 回、穴の fan は穴の最初の角から。粒の draw の push constant が (原点, 1/(48 × 倍率))、段は 4・3.56 px/pt で 0、3.5・2 で 1、1.5・1・0.5 で 2 |
| C8 | `run-notes-tools.sh`（p003）ok、`plan/ws079/tests/run-notes-host.sh` ok、`plan/ws175/tests/run-host-notes-edit.sh` PASS、`plan/ws175/tests/run-host-clean.sh` PASS |

`git diff --check 569d8e952 -- userland/desktop/notes plan/ws216` は空。

### 7.3 未実施・残り

- 画面の画（粒・Multiply・継ぎ目・塊の境に跡が無い、代わりの見え方）は QEMU・実機で未確認（p007 で T1、Pi 4 は p009）。この Phase は T1 の結果を Q1 が判定するまで cleared にしない。
- 道具は p006 まで UI から選べない。T1 の画の確かめは p005・p006 の後の p007 にまとめるか、先に試験の image（編集 data に新しい道具の stroke を持つ PDF を `--file` で置き、Notes で開く）で見るかは Q1 の判断。
- FreeBSD の native の build は未実施。
- 規約の全文の見直しは p008。
