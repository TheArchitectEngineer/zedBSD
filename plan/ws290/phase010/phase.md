<!-- awesome-plan project=zedbsd record=ws290-p010 -->

# ws290-p010 転送の shader による代替

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p010（版 4.5、ghci.md cb937e6）。
- 状態: in-progress
- 依存: p009

## 範囲

転送の shader による代替（§11.6、U10）: `vk-meta.c`（内部の shader を IR で組む）、compute の copy・fill・update・query・buffer と image の写し・resolve、描画の blit、転送の後の bind の置き直し。gsi1model.py での試験

詳細は design の該当の節（§4〜§16）と §14 の p010 の場面。

## 受け入れ

- p010 の場面 PASS、build の warning 0
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 20 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3 の手伝い（q1644-i01、途中の記録）: commit は P3。
  - 新: `src/drivers/gpu/ghci/vk/vk-meta.c`（内部の compute program を IR で組む: 要素の copy（1〜4 byte の要素、行・slice の step、端は atom の and・or）と query の詰め直し。GPU ごとに最初の使用で一度 compile（`struct ghci_vk` の `meta`、device の lock の下で載せる。detach で解放）、device ごとに code の heap に PSO と code を置く（`struct ghci_vk_device` の `meta`、heap と一緒に消える）。`ghci_vk_meta_copy`・`ghci_vk_meta_query` が descriptor（4 byte 揃えの始まりと bytes）と push constant を計画する）。
  - 変更: `vk.h`（meta の型と push の語）、`vk-execute.c`（`meta_run`: BIND_PIPELINE・set 2 の表（source は ReadOnly）・LOAD_CONST・DISPATCH（65535 group ごとに分ける）、その後 client の bind を全部 dirty。BLT 無しの CopyBuffer・Fill・Update・ResetQueryPool・buffer↔image・CopyImage・拡大縮小の無い Blit を program で。CopyBuffer の端の byte と word の中の位置の違う copy は両方の profile で program（BLT 有りで揃った中央は COPY_BUFFER）。D24S8・D32S8 の 1 つの aspect は両方の profile で program（buffer は depth 4 byte・stencil 1 byte/texel）。CopyQueryPoolResults は両方の profile で program（WAIT は前に BARRIER WAIT_IDLE）。BLT 無しの Resolve と拡大縮小・反転・変換の Blit は飛ばして log（描画の resolve・blit はまだ））、`vk-node.c`（detach で meta の program を解放）、両 platform の vmunix.mk と `ghci-host-test.sh` の一覧に vk-meta.c、`ghci-execute-host-test.c`（transfers の BLT 有りの列に端の dispatch、queries の列に copy の dispatch、no-blt の ENOTSUP の場面を外した）。
  - quick（pipeline の binary だけ）: execute 187/0。gsi1model の試験はこれから。
  - 試験（途中）: 新 `plan/ws290/tests/ghci-meta-host-test.c`（`ghci-pipeline-host-test.c` の末尾で include、`meta <tests> <tools> <out>` で流す）と `ghci-meta-run.py`（program の code を gsi1dis で .s にし gsi1asm で戻る事を確かめ、`gsi1-model-run.py` で 64 lane × group を走らせる）。GPU model が受けた内部の DISPATCH ごとに、placed の code・set 2 の表（descriptor の base を model の番地に置き換える）・LOAD_CONST の bank・source と destination の byte を model の data image にして gsi1model.py で走らせ、destination を GPU model の memory に書き戻し、最後に 1 MiB の memory を手で書いた期待（Vulkan の意味で前の byte から作る）と比べる。場面: buffers（端 2 byte・word を跨ぐ読み・端 3 byte、揃った 64 byte、1 byte、fill、update。最初の dispatch の PSO（cs_code・64×1×1）・set 2（source の word・ReadOnly、destination）・LOAD_CONST c0〜c13 を手で）、images（buffer → RGBA8 の level 1・layer 1、R8 → 5 byte の行（word を共有）、D24S8 の stencil ← buffer・depth → buffer（上の byte を保つ）、D32S8 の depth ← buffer、CopyImage、等倍の Blit）、queries（no-blt と full: 64 bit + availability、32 bit + availability、32 bit だけ（使えない query は書かない）、WAIT（前に WAIT_IDLE の BARRIER）、no-blt は後に reset の 0）、binds（dispatch → fill → dispatch で client の pipeline・set 2・c0〜 が戻る、draw → fill → draw で pipeline・viewport・vertex buffer が戻る）、refusals（拡大の Blit は飛ばす、8 に揃わない 64 bit の結果・D24S8 の両 aspect の buffer copy は EINVAL）。
  - 試験の道具の変更: `ghci-model.h`・`.c`（`MODEL_COMMAND_WORDS` 16 → 32（LOAD_CONST 14 語を見るため）、`model_more_formats`（FMT が R8・D24S8・D32S8 も知る。meta の試験だけが立てる））、`ghci-pipeline-host-test.c`（stdlib.h、meta の mode、include）、`ghci-host-test.sh`（execute の後に meta の節）。
  - quick: meta 48/0（GSI1 model の dispatch 23）、execute 187/0。no-blt の profile に MSAA が無いので、no-blt の resolve の場面は作れない（記録）。
  - 描画の resolve・blit（BLT 無し）を足した: `vk-meta.c` に draw の program（VS: vertex index だけで viewport を覆う 1 枚の三角形、blit は 2 つの矩形から source の座標を location 0 に。FS: resolve は FragCoord の画素 + push の差の texel を 2・4 sample の LD で読んで平均、整数の format は sample 0、blit は SAMPLE_LOD 0 で sampler が filter）。kind は RESOLVE_AVERAGE2・4・RESOLVE_FIRST・BLIT（VS と FS を一緒に compile、FS は VS の表を継ぐ、texture・sampler の slot 0）。`vk-execute.c` の `meta_draw`: dst を colour 0（LOAD・STORE）の render pass を矩形に、BIND_PIPELINE、SET_VIEWPORT・SET_SCISSOR（矩形）、set 0 の texture descriptor（src の level・layer、sRGB は flag）、set 1 の sampler（filter、CLAMP_TO_EDGE）、LOAD_CONST、DRAW 3、END。ResolveImage（layer ごと）、BlitImage（src が sampled で depth でなく dst が colour attachment にできる時は描画、反転は角の順、そうでなく同じ format の等倍なら compute の copy、他は飛ばして log）、subpass の colour 1〜7 の resolve（ROP → FS の BARRIER（WAIT_IDLE・FLUSH_L1・INVAL_TEX）の後、src の view の single descriptor で描画）。
  - 試験: meta に draws（full から CAP_BLT を外した profile（MSAA が有る）: 拡大・反転・LINEAR の blit と 4 sample の resolve の packet と description・PSO・viewport・scissor・texture・sampler・push の byte を手で）と subpass（colour 1 の resolve が BARRIER と描画になる）を足した。images の等倍の blit は描画になった（GSI1 model は texture を走らせないので memory の期待から外した）。refusals の拡大の blit は depth の blit に替えた（描画も copy もできず飛ばす）。quick: meta 75/0（dispatch 22）。
  - IR の組み立ての入れ子の呼び出し（引数の評価の順が決まらず命令の順が compiler で変わる）を平らにした（`vk-meta.c`）。
  - **結果**（最後の source の変更の後）: `sh plan/ws290/tests/ghci-host-test.sh` rc=0: words 141/0、ref 56/0、model 254/0、driver 587/0、old-names PASS（39 sources）、ghci-isa PASS、gsi1 50/0、vk 383/0、pipeline 175/0、execute 187/0、**meta 75/0（GSI1 model の dispatch 22）** → PASS ghci290-host-test。build（timeout 3000）: `config-amd64-ghci.mk`（build/ws290-p010-amd64）・`config-visionfive2-ghci.mk`（build/ws290-p010-vf2）・`config/ci/config-amd64.mk`（build/ws290-p010-amd64-ci）・`config/ci/config-visionfive2.mk`（build/ws290-p010-vf2-ci）とも rc=0・warning 0。`git diff --check` 空。QEMU・実機は無し。
  - **決めた事（P3 の判断を頼む）**: image は全部 linear（p009 の作り）なので compute の番地は linear だけ（design §11.6 の tiled は今は無い）。D24S8 の depth → buffer は 3 byte を書き buffer の上の byte は残す（Vulkan は未定義）。query の copy は available か PARTIAL・WAIT の時に値を書き、WAIT は前に BARRIER（WAIT_IDLE・FLUSH_L1・INVAL_L1）。BLT 無しの blit は src が sampled で depth でなく dst が colour attachment にできれば描画、でなく同じ format の等倍なら compute の copy、他は飛ばして log。resolve は 2・4 sample の平均と整数の sample 0、他の数は飛ばす。dst が colour attachment にできるかは optimal の feature で見る（linear の tiling の image も同じ）。内部の program は compile 失敗を覚えない（使う度に試し、拒否は submit を ENOTSUP）。
  - **未実施・残り**: ClearAttachments の部分の rect（R31、任意。FS が colour k だけを export した時の他の attachment の扱いが仕様に無いので作らなかった）。描画の resolve・blit は packet と byte だけ（GSI1 model は texture を走らせない。画素は p014 の実機）。3D の blit は飛ばすまま。framebuffer の layer 1 以降の resolve は無い（layer 0 だけ）。規約の全文の見直しは p015。ws.md の p010 の行（状態 in-progress）は P3。消してよい file は無い。
