<!-- awesome-plan project=zedbsd record=ws291-rdna-next -->

# WS291 RDNA: 次の改善点の調査（2026-10-27 16:55 P2、q1623-i01）

- 親: [WS291](ws.md)。出典: 2026-10-27 ユーザー「RDNAでの高速化を進めます。さらに改善できそうなポイントについて調べてください。計測と改善ポイントの発見まででいいです。」
- 範囲: 計測と候補の列挙だけ。実装はしない。kernel は vmunix-compact1（p006、`amdrdna.vk.ngg=2`）。

## 計測（実機 10.0.10.3、mview qs40、`--spin=8 --content=game --present=mailbox`）

| run | frame ms | acquire | record | submit | present | GPU（fence） | 証拠 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| zedBSD scene-6500（fullscreen、1896x960） | 34.14 | 0.56 | 2.67 | 0.36 | 0.29 | 30.25 | build/ws291/measure/rdna-next/z-6500 |
| zedBSD scene-6500 `--windowed --size=480x270`（画素 1/14） | 32.84 | 0.42 | 2.15 | 0.10 | 1.47 | 28.65 | rdna-next/z-6500-small |
| Linux RADV scene-6500（既定、plane-only） | 20.31 | 0.04 | 0.15 | 0.10 | 0.07 | 19.92 | rdna-next/l-6500 |
| zedBSD scene-1500 / scene-1（p006） | 15.40 / 9.49 | 5.27 / 7.65 | 0.71 / 0.26 | | | 9.06 / 1.23 | p006/compact-scene-1500・1b |
| Linux scene-1500 / scene-1（p004） | 5.01 / 0.11 | 0.04 | 0.11 | | | 4.67 / 0.06 | p004-linux/c1500_・c1_ |

shader（我々は host で gfx10-graphics-test `--trials 0`、Linux は `RADV_DEBUG=shaders,shaderstats,nocache`、build/ws291/rdna-next/・rdna-next/l-6500/mview.log）:

| | 我々（ngg=2、wave32） | RADV（ACO、NGG culling、wave64） |
| --- | --- | --- |
| VS（primitive shader） | 約 404 命令（592 語）、35 VGPR、104 SGPR、LDS 9216 B（68 B/頂点 ×128 + 16） | 302 命令（VALU 116・SALU 85）、24 VGPR、LDS 3584 B |
| culling の前の頂点の fetch | position・normal・uv・instance の 4 つ全部、法線の正規化と lambert も全頂点 | position と instance の 2 つだけ（normal・uv の fetch と lighting は compaction の後の生き残りだけ） |
| LDS の命令 | ds_write_b32 16、ds_read_b32 24（出力を全部 LDS で運ぶ） | write2/read2/b8 で約 20、position と頂点の番号だけ運ぶ |
| barrier | 5 | 4 |
| PS | 約 25 命令、8 VGPR、wave32 | 25 命令、12 VGPR、wave64 |

読み方: 画素を 1/14 にしても GPU は 30.25 → 28.65 ms（−1.6 ms）なので、scene-6500 は画素でなく頂点・primitive の側で決まる（約 28.6 ms 対 Linux の全体 19.9 ms）。
PS は RADV と同じ大きさ。CPU 側は record（2.2〜2.7 ms 対 0.15）と acquire が Linux より長く、mview は frame ごとに fence を待つので frame にそのまま足される。

## 候補（scene-6500 への効く見込みの大きい順）

1. **属性の後回しと LDS の縮小（Mesa の deferred attribute culling の残り）**。根拠: 上の shader の表（我々は culling の前に全属性を fetch・計算し、
   68 B/頂点を b32 の LDS 命令で運ぶ。RADV は position だけを先に計算し、生き残った頂点だけ normal・uv を fetch して lighting）。scene-6500 の三角形は
   ほぼ全部が小さい三角形で落ちるので、属性の仕事は RADV では殆ど走らない。LDS 9216 B は WGP あたりの subgroup の数を RADV の約 4 割に下げる。
   直す所: src/drivers/gpu/amdrdna/compiler/gfx10-compile.c（cull_place_bytes・cull_channels と culling・compaction の emit、約 3280〜4300 行）。
   VS の IR を position だけの写しと全体に分け（optimize.c の不要な命令の除去で position の slice）、前半 → 判定 → compaction で頂点の番号・instance を
   詰め直し → 後半は生き残りだけ全体を走らせて export。LDS は x/w・y/w・w・印・番号に（約 24 B/頂点）、ds_write2/read2 に束ねる。
   見込み: GPU −5〜8 ms（30.3 → 22〜25 ms、推測）。手間: 8〜12 LW。危険: 中〜高（GFX10 の culling は止まりやすい。模型は 1 wave なので wave をまたぐ
   番号の詰め直しは実機でしか試せない）。
2. **host-visible の VRAM（BAR の窓）を write-combining で map**。根拠: record が copies に比例して伸びる（scene-1 0.26・1500 0.71・6500 2.2〜2.7 ms、
   Linux は 0.11〜0.15 で一定）。mview は frame ごとに 6500 個の instance（104 KB）を HOST_VISIBLE|COHERENT の buffer に書き直す。amdrdna の host-visible の
   型は BAR の窓の VRAM だけ（vk-device.c の memory types）で、その map は DRV_GPU_MAPPING_DEVICE → VM_DEVICE_MMIO → HAL_SPACE_DEVICE|NOCACHE（UC）。
   RADV は WC か cached の GTT。原因の帰属は推測（record の内訳は未計測、直す前に profile か小さな計測で確かめる）。
   直す所: src/drivers/gpu/amdrdna/node.c（blob の mapping->attributes）、src/drivers/gpu/gpu.c（gpu_mmap の属性）、include/kern/vm-device.h・
   src/kern/vm/device.c（WC の属性を足し HAL_SPACE_WC へ。hal.h に HAL_SPACE_WC はあり amd64 の HAL が実装済み、HAL の API は変えない）。
   見込み: frame −2〜2.5 ms（record → 約 0.2 ms）、host から書く全 app に効く。手間: 2〜3 LW。危険: 低〜中（WC の書き込みが submit より前に見える事:
   ioctl の経路の lock 命令で WC の buffer は掃けるはずだが確かめる。WC からの読み戻しは遅い）。
3. **acquire の待ち（mailbox でも image の返却を待つ）**。根拠: acquire が scene-1 7.65・1500 5.27・6500 0.42〜0.56 ms、Linux 0.04。軽い scene が
   約 105 fps で頭打ち。直す所: libvulkan の WSI（swapchain の image の数・返却）と Keiland の direct scanout の release（RDNA 固有ではない）。
   見込み: scene-6500 では −0.5 ms 程度、軽い scene で大。手間: 調査 2 LW + 修正 2〜4 LW。危険: 中（desktop の present の経路）。
4. **画素の側の小物**（合計でも ≤1.6 ms: 上の小さい窓の計測）: PS を wave64（RADV、p005 の IB の比較）、HTILE/HiZ（Linux の nohiz で +0.7 ms）、
   guard band（PA_CL_GB_*_CLIP_ADJ: RADV 255、我々 1.0。画面の縁をまたぐ三角形を clipper に回さない、register だけで 0.5 LW）、
   PA_SC_MODE_CNTL_0 の ALTERNATE_RBS_PER_TILE。binning は Linux の nobinning で差が無いので外す。scene-1500（GPU 9.06 対 4.67 ms、比が 6500 より大きい）
   ではこの側の比重が大きい見込み（未計測）。手間: 各 0.5〜2 LW。危険: 低（HTILE は中）。
5. submit 0.36・present 0.29 ms（Linux 0.10・0.07）: 小さい。後回し。

見込みの合計（推測）: 1 + 2 + 4 の guard band で scene-6500 の frame 34 → 約 24〜27 ms。Linux の 20.3 ms に並ぶには 1 が見込みの上の方まで効く事と 3 が要る。

## 未実施

- record の内訳の計測（候補 2 の帰属の確認）、scene-1500 の画素の側の切り分け、wave64 の NGG の比較（p004 の legacy では wave の大きさに差が無かった）。
- 機械の状態（16:53）: Debian が起動中。USB の stick は vmunix-compact1・`amdrdna.vk.ngg=2` のまま（動く kernel）。
