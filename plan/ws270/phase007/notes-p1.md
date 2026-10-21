# ws270-p007 P1 の記録（q1538 / q1538-i01、Keiland 側）

Phase の本体は [phase.md](phase.md)（Q1 が書く）。ここは P1 の作業の記録。

## 読んで分かった事（2026-10-21、code の読み、板は未実施）

- Keiland はすでに damage を持つ（ws035-p055・ws211）: buffer age の history（`compose_region_since`）、damage の矩形ごとの scissor と draw
  （`compose-bind.c`）、矩形の外の quad の省略（`kwl_compose_clip_visible`）、窓の移動・cursor・client の commit の測った damage
  （`damage.c`・`shell.c` の `drag_damage`）。全画面になるのは `server->dirty`（測れない変化）と、damage が画面の 7 割を超える時。
- driver の費用の形（`src/drivers/gpu/powervr`、読むだけ）:
  - 1 つの render job は draw を 64 まで持つ（`POWERVR_RENDER_DRAWS`）。pass の draw が 64 を超えるごとに次の job が増え、その job は
    render area 全体を LOAD する（`powervr_render_job_continue`）。render-pass.h の注記: 全画面の compositor の job 1 つ約 37 ms。
  - LOAD は全画面の draw で行う（`load_draw_build`、scissor は render area）。板 d42: 全画面の LOAD 23.35 ms。
  - transfer（copy・blit、host image の copy）は前の render の完了を待つ（`order_after`）。
- Keiland の側で pass が切れる所: backdrop（`backdrop.c`、blur を頼む窓・docked の中央の窓の下の scene を小さく描いて blur し、
  出力の pass を LOAD で再開）、scene の capture（cursor が plane に無い時だけ）、layer の picture（App Home など）。

## 計測の追加（dee85df07）

- `KWL PASSES`（`--log-frames` の時だけ、frame の完了の時に KWL DRAWS の次に出る）: frame の list の pass の数、job の数（64 draw ごとに
  1 job と数えた推定）、draw の数、LOAD する pass の数、pass の面積の和（area_px）、LOAD する画素（load_px: LOAD の pass の面積と、
  続きの job ごとの面積）、copy の数と画素、最初の 8 つの pass の `x,y,WxH:draws[l]`。全画面 1 枚 = 2073600 px（1920x1080）。
- 確認: `make -j16 ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-gpu.mk BUILD=build/q1538 build/q1538/bin/wayland` rc 0、
  warning 0。板は未実施（Q1 に依頼済み、scenario A〜E）。

## 板の数と変更（2026-10-21）

| 板 | wayland | 3 窓の全画面の frame | terminal の部分 frame |
| --- | --- | --- | --- |
| d48 | dee85df07（計測） | draws 263・jobs 5（driver の 64 draw ごと）・blended 7.2 M px、rec submit 392 ms | （terminal の command が失敗、未取得） |
| d50 | ef1a02591（glyph の batch） | draws 263→132、MODES image 6.98 M px・glass 2.10 M・shadow 1.32 M・text 153 glyph、rec submit 300〜320 ms（P2 の 512 draw の kernel） | 同上 |
| d56 | 98c7a4ccf（壁紙・desktop の穴）、P2 の vf2c kernel | draws 165、image 4.93 M px、rec submit 約 160 ms | 916x632 の部分、draws 26、opaque 1.01 M・blended 2.51 M px（面積の 6 倍）、rec submit 約 66 ms |

変更（commit、全部 VF2 の build で rc 0・warning 0）:

- dee85df07・720d55dbb: `KWL PASSES`（pass・job・LOAD の画素・copy）と `KWL MODES`（mode ごとの draw/px）の行（`--log-frames`）。
- ef1a02591: 連続する glyph を 1 draw に（`shaders/panel-text.*`、batch.c の text の道、最大 85 glyph）。PowerVR の compiler の host 試験（p010-compiler の複写に 2 shader を足す）425 passed。
- 98c7a4ccf: 壁紙と desktop の surface を窓の不透明な内側の下で描かない（`kwl_damage_rects_subtract`、帯に分けて最大 64 片）。subtract の host 試験（全 pixel の総当たり、固定 9 例・乱数 200 例）bad 0。
- ec2694de6・c3dde7fc7: 一般化（下の窓の shape も上の窓の穴の下で描かない、`glass_shape_cover`、`server->cover_*`）、opaque region（compositor の wl_region 保持・`set_opaque_region`、libkeiland の `kl_window_set_opaque`、KL_VERSION 95、terminal が宣言）、desktop の描いた範囲（`kl_desktop_surface_v1` version 2 の `set_drawn`、libkeiland が present の時に 32x32 の tile で見て送る）。desktop の tile→矩形の host 試験 bad 0。

板で分かった事:

- damage は効いている。全画面になるのは dirty=1（map・configure）と、その後 swapchain 3 枚の buffer age の追いつきの 2 frame。
- d50 の D'（terminal の出力）は command が失敗していた（画面に「test: -lt: unexpected operator」）。

## 区切り（2026-10-21、Q1 の指示で Keiland の作業を止める）

- ユーザー（2026-10-21、Q1 経由）:「Raspberry Pi 4の非力なGPUでも60fps近く出ているので、Keiland自体はそれなりに最適化してあると思います。そこで、PowerVRのドライバの改善にフォーカスしてみてほしいです。」
- merge してよい範囲: agent/p1-q1538 の 3f7097df1 まで（dee85df07・720d55dbb・ef1a02591・98c7a4ccf・ec2694de6・c3dde7fc7・3f7097df1 と記録の commit）。
- 板で確かめた: d50（glyph の batch、text=1、shot50 の文字は同じ）、d56（98c7a4ccf、shot56）、d57（c3dde7fc7、shot57、terminal の出力 37 frame が全部部分・rec submit 平均 13.5 ms、全画面 frame の shade 4.0 M px）。
- 未確認: 3f7097df1（影の quad の切り詰め）は板で未実施（build と式の確認だけ）。desktop の set_drawn は icon の無い desktop でしか見ていない（icon がある時の見た目は未確認）。opaque region は terminal だけ（他の app は宣言しない）。全画面の frame の 85〜110 ms と、dirty=1 になる操作（hover・focus・drag）は aat-input が無く未測定。
- 次の候補（未 commit、`git stash` の「q1538 raise_measured (untested, not merged)」に保存）: 窓を前に出す（focus）時の dirty=1 を、2 つの窓の範囲と bar の damage にする（shell.c の raise_measured）。build は rc 0・warning 0、板は未実施。

## q1541（2026-10-21、P1）: PowerVR の job ごとの固定費

branch agent/p1-q1541（main を merge 済み）。板は Q1（d59〜d68）。

### 分かった事

- 部分の render area の job が全画面と同じ時間だった原因の一部: ISP_CTL の process_empty_tiles を常に立てていた（c69651a44 で clear の無い job は落とす、Mesa と同じ）。
- 64x64 の target でも約 2 ms → tile の数ではなく job ごとの固定費。job trace（POWERVR_JOB_TRACE 1、kick から geometry・fragment の timeline までを hal_rtc_read_counter で spin）: small job で TA 639 us・3D 608 us（d66）。
- firmware の trace（d66、全 group）を timestamp（約 0.65 us 単位）付きで読むと、hardware の TA は約 100 us・3D は約 120 us で、残りは firmware の処理（受け取り→dust on・units init 約 220、→Kick TA 約 225、TA 終わり→Kick 3D 約 480、3D 後の後片付け約 330 us）。1 render に trace の entry が約 90。
- firmware の trace の記録を止めると（d68、groups 0）TA 250 us・3D 276 us と半分以下。dust の電源を保つ試し（d67、DUST_POWER_BY_HOST 1）は TA が約 140 us 減るだけで既定には入れない。
- firmware の code・data の section を MIPS で uncached に map している（mainline Linux と同じ）。cached の試し D1（code）・D2（code と data）を作った（結果待ち）。

### 既定に入れた変更（merge 用の形は 16611bb41）

- c69651a44: clear の無い job は空の tile を処理しない。130e4d42d: old contents の要らない job は background object の tag を切る。
- 277ef9dd8: job の memory を session で使い回す（map・unmap と MMU flush の同期の往復を無くす）。
- 2535513c9: dmesg の timing を細かい counter で、job の memory への書き込みを 32 bit ずつ。
- 16611bb41: firmware の trace の既定の group を HWR だけ（0x401）に。根拠: 全 group で firmware の時間が倍（d66→d68）。job ごとに記録される MAIN・MTS・PM・RTD・BIF・POW は診断の build で POWERVR_FIRMWARE_TRACE_GROUPS 0x7fff にする。HWR の記録（recovery の時の情報）は残す。
- vkmatrix: load-part・perf-load-part・perf-load-small（42389dad2）、perf-clear-64・perf-load-64（d9ff1b38f）。
- 計測の道具（既定は off）: POWERVR_JOB_TRACE（render-queue.c）、40 番目の small job の firmware の trace を mark から全部 dump（powervr_firmware_trace_since_mark）。

### VF2 の Linux（IMG DDK 1.19@6345021）と zedBSD の設定の比べ（2026-10-21、Q1 の dump: build/uat/vf2-desktop/runs/linux-ddk/）

値と手順の事実だけ（DDK の文は写さない）。zedBSD の register の値は 4d20d12be の one-shot の log（100 回目の render の kick の後、「powervr: registers:」）で板が戻ったら埋める。

| 項目 | DDK（Linux） | zedBSD（今） | 試し |
| --- | --- | --- | --- |
| gpu_core / noc_bus_gpu_axi | 594 / 594 MHz | 396 / 396 MHz（axi は core の gate） | POWERVR_VF2_CORE_594 1 |
| gpu_sys_clk（isp_axi） | 297 MHz | 297 MHz | 同じ |
| FW system config | 0x00020010（CSW medium、RD power island） | 0x00000010（RD power island、CSW なし） | CSW は入れない（2 session の lockup の過去） |
| FW OS config | 0xF（CSW TDM・TA・3D・CDM） | 0 | 同上 |
| firmware の trace | none | 0x401（HWR だけ、q1541 で 0x7fff から） | 済み |
| APM の latency | 100 ms | 0 ms（mainline と同じ） | POWERVR_APM_LATENCY_MS 100 |
| MIPS の firmware の code・data | cached（MIPS の TLB の C の印、stack も） | code cached（q1541）、data uncached | POWERVR_FIRMWARE_DATA_CACHED 1 |
| CLK_CTRL / CLK_CTRL2 | 0x002AAA002A22AAAA / 0（自動の gating） | host は書かない（値は log で） | 比べてから |
| SLC_CTRL_MISC | 0x3（64-byte interleave、hazard check、burst combiner の bypass） | hazard ＋ PVR hash（0x200000）＋ combiner の bypass | POWERVR_SLC_DECODE_INTERLEAVED 1 |
| SLC_CTRL_BYPASS（0x3828） | 0x00001D1F01000000 | host は書かない（値は log で） | 比べてから |
| ISP_CTL（最後の job） | 0x00023000 | 0x80033000 / 0x80013000（skip_init_hdrs・sample_pos、空 tile の skip） | 比べてから |
| MULTICORE_FRAGMENT / COMPUTE_CTRL_COMMON | 1 / 0x101 | log で | 比べてから |
| device-local の memory | SLC を通す（GPU だけの memory） | SLC bypass（POWERVR_DEVICE_MEMORY_CACHED 0、1 で板が再起動した） | 別の調べ（q1540） |
| frame buffer の圧縮 | FBCDC v3.1 の flag（UM が OPTIMAL の image に使う可能性） | 無し | Keiland の linear の swapchain には効かない |

DDK の vkmatrix（594 MHz）: flat 1372、texture 2325、quarter 1324、clear-64 348、load-64 581、load-small 1964 us。flat と quarter がほぼ同じ → 全画面でも固定費が主で、pixel の費用は小さい（圧縮か SLC の効果の疑い）。

試しの kernel: 01c0e812f（594 MHz ＋ APM 100 ＋ SLC 64-byte interleave ＋ firmware の data cached、build/q1541k）。既定（merge 用）は 5bced8f66（全部 0、register の one-shot の log だけ入る）。

## q1546（2026-10-21、P1）: uniform の work を shared program へ、pixel の出力の道

branch agent/p1-q1546（基 main 9c0672c58）。commit 83d4df7e3（hoist・pack の元を直に・vkmatrix uniform-ramp/perf-uniform）、f9f2a7929（form と runtime の trial、PACK の pixout 直書き、itrsmp、vkmatrix -t・batched・新 case）。板は未実施（Q1 へ: build/q1546/vf2/vmunix sha256 a1b4323b…、bin/vkmatrix c5c8b7cc…）。

### 仕組み

- compiler（shader-backend.c split_shared）: fragment で、if・loop の外で 1 回だけ書かれ、push・descriptor・定数だけから決まる値を uniform とし、その命令を shared program（compute の形で select、同じ request 表）へ写す。pixel 側が読む computed の値と、uniform だけの色の pack は、新しい request（POWERVR_SHADER_REQUEST_COMPUTED、PDS は load しない）の語になる。shared program が SHARED_STORE（mov → sh）で書き、pixel 側は SHARED で読む（色は PIXEL_SHARED で sh → pixout の mov 1 つ）。0・1 だけの定数の色は literal の request（PDS の DOUTW）にする。
- 使われなくなった uniform の命令は後ろから消す（drop_unread）。remove_dead_writes は 1 pass に 1 段しか消えず、64 段の鎖が残っていた。
- driver: shared register program の最後の load の END を外し、DOUTU（cc なし、END）で shared program を issue する（Mesa の pvr_pds_generate_descriptor_upload_program の secondary_program_present と同じ形、data-program.c powervr_data_shared_build_task）。code は fragment の code の後ろ、16 byte 境界（program.shared_offset）。temps は program.shared_temps。sample rate instance。
- PACK の pixout 直書き（form 0x2）: PACK を olchk 付きで pixel output に書かせ、mov を消す。program は PACK で END（Mesa pco_ops.py の pck は olchk・end・rpt を持つ。pco_legalize.c は pixout を名指せない時だけ mov を挟む）。
- itrsmp（form 0x4）: sample の座標が 2 語の iterate の値で、他に読まれない時、ITER＋WDF＋SMP を control の itrsmp（ctrlop 0b1000、6 byte、Mesa pco_isa.py の I_ITRSMP、BXE-4-32 は has_usc_itrsmp・_enhanced）1 つにする。image・sampler の state は 4 の倍数の sh。Mesa 自身はこの命令を出さないので、encoding の意味（count が dmn から決まる等）は推定。
- sample の 4 語をそのまま pack する時は、移さずに sample の register から PACK する（texture 8→6 group、form に依らず既定）。
- 試しの切り替え: VkPipelineCreateFlags の bit 24〜31 で form を反転する（render-draw.c）。vkmatrix -t 0x0,0x1,… で 1 回の起動で順に流す。

### group 数（host、pixel 側。括弧は shared program の命令数）

| shader | 前 | shared program | pack 直書き | itrsmp | 全部 |
| --- | --- | --- | --- | --- | --- |
| vkm push4（flat） | 4 | 1（8） | 3 | 4 | 1 |
| vkm immediate | 2 | 1（literal） | | | |
| vkm alu・alu4・mul4・rcp | 68・41・41・38 | 1 | | | |
| vkm uniform（新） | 18 | 8（17） | 17 | 18 | 7 |
| vkm texture | 6（pack の元を直に、前は 8） | 6 | 5 | 4 | 3 |
| kwl quad.frag（blend） | 12 | 12 | 11 | 10 | 9 |
| panel[0] glass | 61 | 54（16） | 60 | 61 | 53 |
| panel[1]〜[7] | 39・36・31・33・18・40・67 | 32・33・27・29・14・36・60 | 各 −1 | | |

### texture の内訳（DDK は texture−clear 約 1.0 ms、こちら 6.0 ms）

perf-texture（blend over、vkm texture.frag、1 group ≈ 0.45 ms/全画面 at 594 MHz の model）の前の pixel あたり: ITER（1）、WAIT（1）、SMP（1）、WAIT（1）、MOV×2 の組（sample の 4 語を pack の連続 register へ、2）、PACK rpt4（4 slot）、MOV olchk（1）、blend の UNPACK rpt4（4 slot）と FMAD 4 つ（組で 2）。model で 9〜15 slot ≈ 4〜7 ms で、測った 6 ms と合う。
減らせる所と今回の扱い:
1. MOV×2（sample の語を移す）: 既定で消した。
2. MOV olchk（pack の後）: form 0x2（板で確認待ち）。
3. ITER＋WAIT＋SMP: form 0x4 の itrsmp（板で確認待ち）。
4. PACK rpt4・UNPACK rpt4（8 slot）が残りの最大。手は TPU に F16 で返させ（smp の f16、pco_isa.py smp_extb）、pixel output を F16 にして PBE に U8 へ変換させる事。ただし pixel output の形は pass の全部の draw と end-of-tile の PBE の source format（render-program.c pbe_source）、clear の色の pack（image-state.c）、load draw で揃える必要がある。BXE-4-32 は usc_min_output_registers_per_pix 2 なので、F16 にしても tile の register は増えない見込み。今回は入れていない（次の候補）。
5. WAIT の latency: task が十分あれば隠れるはず。perf-texture-nob（blend 無し）との差で blend 分を分ける。

### 次（板の結果を見てから）

- -t の組の perf: shared program の効果（flat 4.3 → clear 2.3 ms に近づくか）、0x2・0x4 の効果と正しさ・HWR。
- perf-alu-pix を Linux（DDK）でも流し、pixel の ALU の rate（8 か 16 lane/clock か）を見る（Fable の候補 B）。
- F16 の pixel output（上の 4）を pass 単位で設計する。
- 未確認: secondary の USC task から sh に書いた値を pixel task が読める事（hardware の順序の保証）は Mesa に使用例が無い。板の push4・push32・push128・uniform-ramp の readback で確かめる。

## q1547（2026-10-21、P1）: Linux（DDK）の計測の道具と、DDK の job の語・pixel の program の事実

branch agent/p1-q1547（基 main feb645053）。commit a277c0b76（vkmatrix: perf-alu-pix-sq・perf-alu-pix-sq32・perf-flat-blend、`-c <file>` の VkPipelineCache）、
0f9d272b1（`tools/pvr-ioctl-shim.c`、LD_PRELOAD で RGXKickTA3D2・RGXCreateHWRTDataSet の in を書く）、f5996e3fc（`tools/linux-ddk-measure.sh`、今回は Q1 が手で流したので未使用）。
板（Linux、DDK 1.19、594 MHz）は Q1 が流した。DDK の出力（cache・shim の dump）は `build/uat/vf2-desktop/runs/linux-ddk2/` だけに置き、git に入れない。以下は値の事実だけ（DDK の byte・encoding は写さない）。

### 測った値（batched、594 MHz、Q1）

| case | DDK | 読み |
| --- | --- | --- |
| perf-alu-pix-sq32 | 28561 us | 32 段 × 2 op（fmul＋fmad）× 2073600 px ÷（28.56 ms × 594 MHz）≈ 7.8 op/clock → zedBSD と同じ 8 lane/clock |
| perf-alu-pix | 1133 us | affine の鎖を compiler が畳む（比較に使えない） |
| perf-flat / perf-flat-blend | 983 / 1527 us | blend +0.54 ms |
| perf-texture / perf-texture-nob | 1974 / 1869 us | blend +0.1 ms |
| perf-clear | 975 us | |

perf-alu-pix-sq（64 段）は時間の行が Q1 の抜き出しに無かった（vkmatrix は case ごとに PASS の行に時間を書き、合計の行はその後。生の出力を確かめる）。

### job の語（shim、steady な kick 12、perf-clear・flat・flat-blend・texture・texture-nob）

- DDK の command の形は mainline Linux（v7.3）の形と違う: TA 64 byte、3D と 3D PR 320 byte（こちらは mainline の形）。値の一致で対応を取った。
- 5 つの case で 3D の command は program の address の 4 語（PDS の data・code の address と思われる）以外が全部同じ。blend の有無・texture の有無は job の語に出ない → blend の差は pixel の program だけ。
- ISP_CTL の語 0x80033000（skip_init_hdrs・process_empty_tiles・sample_pos・tiles in flight 4）、USC_PIXEL_OUTPUT_CTRL 0x1ff8（2 register/pixel、10 partition）は zedBSD と同じ値。
- PBE の register の語: swizzle（B8G8R8A8）、twiddle の layout、line stride（2048 の padded の幅）、clip（1919・1079）、log2 の大きさ（11・11）、tilerelative は zedBSD の式の値と同じ。違いは word 0 の bit 3 が 3D で立つ（3D PR では 0、こちらは 0）。意味は Mesa の cr.xml で確かめる（未確認）。3D PR の PBE は linear・stride 1920 の別の target。
- ZLS は無し（depth なし）。
- clear だけの job（perf-clear・perf-clear-64）も kick_ta 1・kick_3d 1・kick_pr 1。DDK は clear だけの job でも TA を省かない（Q4 の「TA を省く」案は DDK の形ではない）。
- HWRT data set（RGXCreateHWRTDataSet）: multi_sample_ctl 0x88、isp_mtile_size 0x00020002、mtile_stride 4、te_mtile1/2 0x00080000、rgn_header_size 16、tpc_size 128。64x64 の target は te_screen 0x1001・ppp_screen 0。

### pixel の program（pipeline cache の USC の group を header で数えた）

道具: group の header（usc-encode.c と同じ layout）で区切る walker（build/q1547/decode/walk-cache.py、DDK の file と一緒に repo の外）。operation の名前は Mesa 25.0.7 pco_isa.py の F_SNGL_OP・F_CTRLOP で引いた。

| shader | DDK の fragment の group | zedBSD（q1546 の host の数） |
| --- | --- | --- |
| flat（push4） | 1: main p0 の sngl **f16sop**（olchk、w0p、end） | 1〜4（shared program 次第） |
| flat-blend（OVER） | 1: f16sop（olchk、w0p、end）。別に 1 group の program（fmul、end）＝ push の red × 0.5 を draw ごとに 1 回 | blend は UNPACK rpt4＋FMAD×4＋PACK rpt4＋MOV |
| texture（blend） | 2: control の **itrsmp**（ctrlop 8）＋ f16sop（olchk、end） | 6（itrsmp・pack 直書きの form で 3） |
| texture-nob | 2: itrsmp ＋ f16sop（blend の版と operand の語だけ違う） | |
| alu-pix-sq（64 段） | 130: ditr（ctrlop 12）1、fmul 64、fmad 64（1 op 1 group、co-issue 無し）、f16sop（end）1 | |

- DDK は色の出力を **F16SOP 1 命令**で書く（U8 への変換・pixel output への書き込み・end を兼ねる）。blend も同じ 1 命令で、destination を読む（blend の版と無しの版は同じ形で operand だけ違う）。PACK・UNPACK・MOV olchk は使わない。BXE-4-32 の usc_f16sop_u8（render-program.c の pbe_source の注記）と合う。
- texture は itrsmp（iterate＋sample を 1 つ）＋ F16SOP の 2 group。zedBSD の form 0x4（itrsmp）は DDK と同じ道。
- DDK は main の group に 2 byte の header（ext 0、end・rpt 無し）を多く使う（vertex と alu の group）。こちらの encoder は「Mesa が出さないので未証明」として使っていない。
- Mesa 25.0.7 の pco_isa.py は F16SOP の encoding が「TODO」で、Mesa からは field が取れない。

### 次の実装の案（判断待ち、Q1 へ）

1. 色の出力と blend を F16SOP 1 命令にする（flat 4→1、blend の UNPACK＋FMAD＋PACK＋MOV＝約 10 slot→1、texture 6→2）。DDK の blend の費用 +0.1〜0.5 ms は、これが pixel ごとに 1 命令である事と合う。
   - 障害: encoding の公開の出典が無い（Mesa は TODO）。DDK の binary から encoding を写すのは license の注意（encoding を repo に写さない）に当たる。進め方はユーザーの判断が要る（例: 公開の資料を探す、DDK の cache から field の意味だけを事実として観察する事を許すか）。
2. 1 が駄目なら: blend を F16 の pixel output（PBE の source F16）にして PACK・UNPACK を減らす（q1546 の候補 4）。
3. 2 byte の main の header を使う（code の大きさ・fetch の減り。DDK が使うので hardware で動く事は分かった）。
4. clear だけの job の TA を省く案は DDK に根拠が無い（DDK も TA を流す）。

## q1548（2026-10-21、P1）: pixel の出力と blend を sum of products の 1 命令に（trial の form）

branch agent/p1-q1548（基 main c6eebc780）。commit 7468edfdd・e8baec967・ee37f4c19（vkmatrix の sop-* case、`-C <dir>` で case ごとの pipeline cache）、5c80fe4dd（compiler の form 0x8・0x10）、e18aded3b（form 0x20）、その上に書き方だけの直し。

- form（pipeline の flag の bit 24〜31 で反転、vkmatrix -t）: 0x8 = 8-bit の attachment への色の出力を sum of products の 1 命令で（不透明、blend の OVER・SUBTRACT（one, one minus src alpha）・ADD（one, one）・MULTIPLY（dst color, zero））。それ以外の blend・write mask・F16 の attachment は従来の道。選べない時は従来の道で compile し直す。0x10 = blend の色の clamp を命令に任せる（命令が clamp するかは板で確かめる）。0x20 = iterate and sample（0x4）が自分の data を待ち、後の WAIT を省く。
- IR: POWERVR_IR_PIXEL_BLEND（色 4 つと blend の種類、色は [0,1] に clamp して blend）。machine: MACHINE_SUM_OF_PRODUCTS（usc-encode の powervr_usc_sum_of_products、group は internal selector の byte を持たない）。
- host: compiler 試験 2827/2827（form 0x8・0x8|0x10・0x8|0x10|0x20 を含む、machine と interpreter の一致、quad.frag・texture・batch・flatblend × 4 種の blend）、group 試験 27/27、vkmatrix lavapipe 103/103。VF2 build warning 0。
- group 数（host）: texture＋OVER 12 → 0x8: 7 → 0x18: 5 → 0x1c: 3 → 0x3c: 2。Keiland の panel の 8 変種は 0x8 で各 −5、push4 4 → 2。
- 板 d87（5c80fe4dd）・d88（e18aded3b）、Q1: trial 0・0x8・0x18・0x1c・0x3c とも 102/103（FAIL は sop-const だけ、trial に依らない）、HWR・reset 0。sop-clamp・sop-clamp-over は 0x18 でも PASS → 命令が clamp する。perf（batched、us）: flat 2300→2250、flat-blend 7300→2850（0x8）→2700（0x18）→2750（0x1c・0x3c）、texture 8300→3250→3050→2950（0x1c・0x3c）、texture-nob 3600→3000→3000→2900。0x20 は正しく、速さは変わらない。
- sop-const の原因: driver が pipeline の静的な blend 定数を使わず常に 0 を書いていた。69318f836 で直した（動的な vkCmdSetBlendConstants は未対応のまま）。
- 9a1d07516: 既定の form を 0x1 → 0x3d（shared program・itrsmp・sum of products・raw・wdf）。板の確認待ち（vkmatrix 既定・perf・desktop の keiland-shot）。
- 3f27c0545: 試しの切り替え（pipeline の flag の bit 24〜31、vkmatrix -t）と form 0x2 を撤去（規約 §12）。host 試験は compiler 2387/2387・group 27/27・p010 の driver 15 本・vkmatrix lavapipe 103/103。

## q1550（2026-10-21、P1）: VF2 の desktop の速さ（cursor が出ない、描画が 6 fps 程度）

branch agent/p1-q1550（基 main 5907c9d3b）。ユーザー（2026-10-21、板の desktop を操作して）:「まず、マウスカーソルが表示されないです。それから、描画自体も6fpsくらいでしょうか。」

### cursor

- d48〜d89 の保存 log には「KWL CURSOR shown」が一度も無い（測定の run で mouse を触っていない）→ 板で hardware cursor が動いた log の証拠は無かった。
- d90（ユーザーが操作した session、runs/d90/cursor-grep.txt）: event1 の REL で「KWL CURSOR shown by=pointer」、その後「KWL CURSOR_PLANE present x=… copy=0/1」が多数（x 1616→254 と動く）、WSI の present の失敗も無い → Keiland・libvulkan は位置を plane に出している。それでも見えない → kernel の GPU_DISPLAY_CURSOR_SET・MOVE か dc8200。
- dc8200 の register の書き方（0x1468 config、0x146c address、0x1470 location、shadow の hold と plane の commit bit）は StarFive Linux（~/visionfive-linux の vs_dc_hw.c dc_hw_commit）と同じ。
- 診断 6ce298b0c（ecf1693be で外した）: set ごとに写した pixel の alpha・colour の数と (2,2) の pixel、dc8200 の cursor の register の読み返し。
- d91（Q1、aat-input で mouse を動かす）: set 1〜7 の全部「alpha 0 colour 0」、dc8200 は config 0x2e・address・location とも正しく更新 → 写した image が全部 0。VK CURSOR は worker・fast とも result 0。
- vkmatrix に image→image の 6 case を足した（c36d3ee83・69a051178: blit-bgra、blit-rgba、copy-bgra、copy-image、copy-to-linear、cursor-chain。lavapipe 109 本全 PASS）。d92（Q1）: blit-bgra・blit-rgba・cursor-chain が FAIL（4096 texel 全部 0）、copy-* は PASS → format に依らず vkCmdBlitImage が 0 を書く。
- 原因: transfer の blit の program（transfer-program.c build_program）が sample の fcnorm（固定小数の channel を float に直す bit、usc-encode の powervr_usc_sample の第 1 引数）を 0 で出していた。8-bit unorm の channel が整数のまま返り、極小の float を PACK が 0 にした。注記は coordinates の正規化と取り違えていた（座標は sampler の non_normalized_coords）。Mesa 26.2.5 pco_nir_tex.c は destination が float の時 fcnorm 1。今日の変更とは無関係で、blit は最初から 0（板で cursor が見えた記録も無い）。
- 直し 15ecf0e4d: blit（packed）の時だけ fcnorm 1。host の p010 の driver 試験 15 本全 PASS、VF2 build warning 0。板の確認待ち（vkmatrix blit-*・cursor-chain、desktop の矢印）。

### frame（d90 の session.log）

- 操作中（App Home の開閉、WiseView、窓 1 つの desktop）の frame は全部 dirty=1 damage=0 の全画面。copy_px は全 frame 0。
- 1 frame の GPU の時間（KWL LAT の recorded→queued）を mode の pixel で回帰（d89・d90 の 265 frame、idle の後を除く）: glass 12.3 ms/Mpx、shadow 4.8、image 3.9、solid 3.2、定数 2.3 ms。host の group 数（既定の form 0x3d: glass 49、image 25、solid 22、shadow 27）× 約 0.22 ms/Mpx/group と合う。
- 原因: panel.frag は角丸の距離（sqrt 等で約 18 group）を全 pixel で計算する。不透明な内側は blend 無しの別 draw だったが shader は同じ。
- 94b262375・97f784be0: 内側の variant（`KWL_PANEL_INSIDE`、距離を定数 -3: cover 1・edge 0 が元の式どおりになる範囲だけ）を作り（`regenerate.py` の INSIDE_MODES 0・2・3・7）、glass（light・dark、半径+3 px 内）・image・solid の内側を、不透明なら blend 無し、そうでなければ blend 付きの inside pipeline で描く。host の group 数（scratch の p010 の複写に 8 本を足して、machine と interpreter の一致 2793/2793）: glass 49→33、image 25→6、solid 22→1（blend 無し）・3（blend）、dark glass 55→39。KWL PASSES の job の模型を driver の 512 draw に（log だけ）。VF2 build warning 0。板は未実施。
- idle（60 s）の後の最初の frame は内容に依らず 139 ms（d90 で 60 回以上）。kernel の timing（runs/d90/timing-idle.txt）: submit 284 us、job wait（kick→retire）138.8〜139.1 ms、interrupts 4。host 側に固定の sleep は無く、firmware の idle からの復帰の疑い（APM latency 0）。切り分けは firmware の trace（MAIN・MTS・POW）の診断 kernel が要る。未着手。
- 2026-10-22: 3bdad0823（不透明な内側の variant `KWL_PANEL_OPAQUE`: alpha 1・opacity 1 なので ×opacity を省く。opaque は opacity ちょうど 1 の時だけ）、43224aa62（glass の内側は縁の mix（縁 0 で恒等）を省く。通常の variant の SPIR-V は不変）、fed1a5fa3（影の内側: 半径と softness の半分＋1 px 内で alpha 一定、`distance` の定数を -1e6 に）。host の group（scratch の p010 の複写、3100/3100）: 不透明 image の内側 2（元 25）、glass の不透明な内側 28（元 49）、影の内側 3（元 27）。板の計測は Q1。
- 15ecf0e4d の確認（d93、Q1）: cursor set の alpha 118、blit-bgra・blit-rgba・cursor-chain PASS、vkmatrix 109/109、HWR 0。main 287e3feb3 に merge 済み（c43ef8812 まで）。
- idle の 139 ms の診断: ac2420800（POWERVR_JOB_TRACE の build で、10 s 以上 kick の無い後の最初の small job の firmware の trace を dump、その job の sent・ta・3d を出す。既定の build は不変）。板は Q1 待ち（runs/d95/）。
- d95a（Q1、fed1a5fa3 の wayland）: 1 窓の全画面 14〜18 ms（d89・d90 は 29〜35）、2 窓 17〜21 ms、窓無し 5〜7 ms。drag の区間は compose の acquire 13.9 ms・frame_ms 28.7 ms。191 ms の外れ 1 本（frame 15）は新しい client 2 つの接続・Vulkan の import と同時（app の起動）。
- idle の 139 ms の読み（d95、ac2420800 の trace kernel）: firmware は速い（dust の電源 on 1〜2 tick、TA 0.3・3D 1.1 ms）。同期の job trace では sent 10 ms（host 側）で 139 ms は出ない。timing の job wait は kick→reap で、reap は次の submit まで遅れる（vk-queue の powervr_render_jobs_reap(…, 0)）。Keiland の vkQueueSubmit の 139 ms は kernel の powervr の submit（284 us）の外（libvulkan の capacity・semaphore の待ちか gpu.c の受け付け）。後回し（Q1）。
- 39a097e0e（flip を 1 blank で）: zero copy の flip は commit から 2 blank 後を取り込みとしていた（DC_PANEL_LOCATION が板で常に 0 のため位置で判定できない）→ flip は 2 blank に 1 回で acquire が待つ。blank の割り込みで fine counter を記録し、直前の周期（8〜50 ms）から次の blank の 3 ms 以上前の commit はその blank で取られると数える。板の確認（tearing が無いことの目視）待ち。

## q1552（2026-10-22、P1）: 全ての操作で 60 fps（16 ms）

branch agent/p1-q1552（基 main 782f995e4）。ユーザー（2026-10-22）:「すべての操作で16msで描画でき、60fpsになることが目標です。ただ、どうしても難しければ、retireしてかまいません。おそらく限界まで最適化して、1280x720に落とせば、目標は自動的にクリアされています。」

### 計測の道具（plan/ws270/tests/frame-ops/）

- frame-ops.sh（板で root）: Terminal の起動、文字の入力、seq の出力、wheel、title bar と system bar の hover、title bar の drag 往復、App Home 開閉 ×2、Wiseview 開閉 ×2。各段の前に session.log の行数を marks に書く。
- frame-times.py（host）: KWL LAT の行から frame ごとに pace（acquired→draw）・cpu（draw→recorded）・submit（recorded→queued、GPU）・present・work（draw→submit）・period（前の frame の draw から、100 ms 未満の時）・shown（submit→完了の受け取り）と KWL PASSES の area を表にし、段ごとに work>16・period>16 の数、p50/max、fps を出す。
- shot-diff.py（host）: keiland-shot の 2 枚の画素の差（max、over2、over8、差の box、差の画像）。

### 読み（d97・d98 の session.log、main 782f995e4 相当）

- drag（d98 frame 39〜48）: 全部 dirty=1 の全画面、work 24〜27 ms（cpu 5、submit 18、present 2）、period 39〜50 ms（約 20 fps）。d97 は work 37〜43、period 61〜73。
  - 原因 1: damage.c の kwl_damage_pointer が drag 中（button の押下で hover の記録が使えない）に dirty=1 にしていた。shell.c の motion 側（glass_motion_take）は drag_measured で部分の書き直しを選んでいたのに、pointer 側が見ていなかった。
  - 原因 2: cursor plane の時は pointer_moved が立たないので、scene を変える pointer の move の frame も frame pacing（client の commit 待ち、前の frame の時間の半分、約 13 ms）で毎回待っていた（pace の wait_ms 13〜15）。
- 約 1/15 の frame で submit→shown が 100〜102 ms（= KWL_RENDER_FENCE_NS の 100 ms）。vkWaitForFences（libvulkan sync.c の sync_notification_wait）が fence の signal で起きず timeout まで寝ている疑い。次の frame もその分遅れる。driver・libvulkan 側（P2 へ）。
- App Home（d90、古い）: frame ごとに stage の全画面 glass（2 Mpx）と全画面の光の shadow（2 Mpx、内側無し）と content の picture。submit 40〜80 ms。Wiseview: 全画面 glass（sheen 付き、opacity=progress）＋全画面の solid の shade＋窓の tile。

### 変更

- fdc686ca4: kwl_glass_drag_measured を公開し、kwl_damage_pointer も測れる drag では damage（窓の旧と新の場所）だけに。scene を変える pointer の move（hover の probe の変化、drag、dirty）では pointer_moved を立て、pacing で待たない。
- 99b63fedd・1ce1d3758（glass-map.c、新）: glass の内側の色は blurred wallpaper の画素と glass の色（白の lift・dark の shade・sheen）だけで決まる。色ごとに出力の大きさの map へ同じ shader（opaque の inside variant）で 1 度描き、以後の内側は map を最近傍で読む variant（KWL_PANEL_INSIDE_MAP: texture 1 回）。sheen 付きは box が全画面の時だけ（Wiseview・App Home の stage）。2 frame が求めた色だけ、次の frame の pass の前に描く（6 枚まで、1 frame に 1 枚、直前 2 frame に使った map は追い出さない）。light の無色の flat glass は blurred そのものを同じ variant で読む（map 不要、式が恒等）。blurred の generation・version と出力の大きさが変われば描き直し。`--no-glass-map` で前の描き方（比較の基準）。
  - App Home の stage（全画面の glass と光）は frame ごとに同じなので、全画面の picture（同じ map の仕組み、描く関数を home.c が渡す）に 1 度描き、frame では opaque の image 1 枚に。
  - shaders.h は regenerate.py で作り直し、既存の全 variant の SPIR-V が前と同一（名前の番号だけ変わった）。map variant は texture 1 回と除算 1 つ。
- d3f2abd6e: 窓の commit の damage を全画面にする条件を「上の窓が近い」から「上の窓が近く、その glass が下の scene を blur する（set_blur か docked の中央）」に。上の窓が wallpaper の blur だけなら、その部分も damage の中で描き直される。
- build（config-visionfive2-desktop-gpu.mk、BUILD=build/q1552）: rc 0、warning 0。板は Q1 待ち。

### 板の結果と続き（2026-10-22）

- d101（f67a811c0 の wayland）: drag は全画面でなくなり work p50 11 ms。Home・Wiseview は work 17〜24。Home の frame は period ちょうど 33.3 ms、次の frame の acquired が 2 blank ごと。
- d103（P2 b60c38183: command の frame の fence を即時完了 → submit が非同期、KWL LAT の shown が GPU の完了に）: work p50 は全部 5〜9 ms。Home の後半（GPU 4〜5 ms）は 60 fps、GPU 9〜14 ms の frame は 30 fps。
- d104（flip の診断 0b95ffc6d）: flip の 80〜87% は 1 blank で取られる。commit の多くは blank の後 8〜14 ms、14 ms 以降は 2 blank。
- d105: 60 fps の frame でも次の acquired は blank ちょうど（78028・78044・78061）。
- 原因（1f7936510）: libvulkan の display の primary の present は display_mutex を持ったまま present の ioctl（flip が取られるまで返らない）を呼び、acquire の空き確認（display_image_available）も同じ mutex を取る → 次の frame は前の frame の flip の blank まで始まらない。primary の image の holds を atomic にし、空き確認は mutex 無しで読む。前の frame の flip 待ちと次の frame の CPU・GPU が重なる。
- Keiland 側の追加: 81bc03d70（animation 中は frame を渡した直後に次の want を出す）、5f0acec60（Home の content の picture を描いた box だけ描く）、9a05eba91（ring の空の中を描かない）、ce44dafb4（Wiseview の地を 2 倍幅の picture と KWL_PANEL_CROSSFADE の 1 回の draw に）、95db457c4（Wiseview の地の picture）、6beb321c7（Home の下がる desktop の layer の下を描かない）。
- 2ec8fa710: flip の report は既定 off（POWERVR_FLIP_REPORT、測る build で 1）。
- d110（1f7936510 の libvulkan）: home 55 fps（period p50 17）、drag 60、wiseview 38、launch 48、type 50、next p50 8〜11。一方 d108・d110 で compositor が落ちた（pc 0・0x29900、null か壊れた pointer への jump、P2 の pool の kernel と組の時）。d113（pool 無しの main＋1f7936510、100 s idle → frame-ops ×3）では落ちない。原因は未特定。330767e34: fault で殺される時に tid・command・pc・sp・a0 と sp から 16 語を出す（hal の user context は ra を返さない）。
- 残りの律速は Keiland の直列（frame N の GPU の完了まで N+1 を描かない）。(A) を実装:
  - 162059da7: submission の slot を 2 つ（command・copy・fence・held・callback・完了の情報、batch の vertex は slot ごとの半分）。
  - c436c7db0: render thread は最大 2 つの job を flight に（完了は submit の順）、flight 中も次の image を取り次の list を走らせる。main は compose_may_draw（flight 0、または 1 で copy・bake 無し、head・turn・scene の capture・shot 無し）の時に次の frame を描く。shm の upload は kwl_compose_host_writable の時。glass の map の破棄は flight 0 の時だけ。render thread の host 試験（plan/ws213/tests/render-thread-host-test.sh）に 2 flight の check を足して 158 PASS（TSan も）。
- e08a5e577（glass の blurred の sample 座標を varying に）、9817b450f（Wiseview の tile の影を不透明な tile の内側で描かない）。
- d115・d116c・d118（pipelining 以降、c436c7db0〜e588c63b0）: drag 60、home 58〜59、wiseview 58〜59（prewarm で初回の hitch 無し）、launch 56〜60、type 約 52（period p50 19、lag 11/19）、output 43〜44。scroll・hover は入力の間隔が律速。d116 の起動時の crash は SD 上の /bin/wayland の破損（offset 712705 から違う、dw-mmc の読み失敗の後）で、code の不具合ではなかった。e7ea311a9（frame callback を give の時に）は type の lag を悪くしたので 67a35db60 で戻した。

### Terminal（d118 の ZTERM PERF、TERMINAL_PERF=1 の時だけ出る）

- 1 frame 約 16 ms の CPU: build 4.05/7、acquire 1.74/9、record 2.44/4、submit 2.11/4、present 5.27/11、wait 0.79/2 ms（平均/最長）。
- record: bbf66e118 で image ごとの command buffer を、同じ vertex 数・背景なら記録し直さず再 submit（vertex は同じ場所に書き直す）。fence の待ちは次の draw の頭（terminal_renderer_idle）へ。
- build: VF2 の host-visible memory は client に uncached で map される（vk-memory.c、gpu_mmap の VM_DEVICE_UNCACHED_RAM）。1 cell 6 vertex × 12 float を 32bit の store で mapping に書いていた（1 cell 72 回の bus write）。6e8ac6693: host の memory（vertex_build）で作り、前の frame の写し（vertex_kept）と 64bit ごとに比べ、違う doubleword だけを mapping へ store する（render_upload）。ZTERM PERF に upload（store の時間）。
- present: libvulkan の Wayland の present（frame ごとの fence の作成・export・fd、空の command の Begin/End、vkQueueSubmit）。00a8d6f6f で VULKAN TIMING に present_prepare/present_record/present_submit/present。内訳は板待ち。
- 1920x1080 の制限: Terminal の速さは cell の数（窓の大きさ）に比例する部分（build・upload）と、frame ごとの固定の部分（acquire・present・submit の round trip）がある。後者は出力の解像度に依らない。

### 計測（Terminal の後）と打鍵の遅れ（2026-10-22 朝）

- d120〜d129: Terminal の upload の直し（1e1296a8d、-fno-builtin で 8 byte の memcpy が call になっていた）で upload 13.5 → 1.4 ms。Terminal の 1 frame の CPU は約 10 ms で律速ではなくなった。type（aat-input は 1 字 16.4 ms＝61 字/s）は 44〜47 fps のまま。
- type の 1 字の cycle（d126・d128、中央値）: compositor の frame の完了（frame callback）→ Terminal の commit 3 → display の acquire 2 → main の起床と pass 3〜5 → draw→recorded 4 → vkQueueSubmit 3 → vkQueuePresentKHR 3 → GPU の完了 2.5。Terminal の acquire の 6 ms は compositor が前の buffer を返すまでの FIFO の背圧（a7d6818ae の VULKAN ACQUIRE の行で release_us、timeouts 0）。
- 効かなかった物（外した）: 285e34b07（Wayland の fence を worker が先に作る、present_record が下がらず）、5409fbade 単独・2d5d13052（Wayland の FIFO の待ちを reader の約束どおりに）。5409fbade は 8eeedadb4 の後で必要になるので 776007a31 で戻した。
- 入れた物: 8eeedadb4（render thread が idle の時に display の次の image を先に取る → READY が adopt の +1 ms に）、776007a31（frame callback を頼んだまま commit しない window を awaited から外す、pacing の門が消える）、88deda6ff（--log-frames の行の時刻を rdtime の μs に、kwl_log_us）。
- main の pass の重さ（d133 の KWL PERF ticks、ce090a9fa）: kwl_schedule の先頭の glass の tick が idle でも 1 pass 約 1 ms（5 s に 416 ms、main の CPU の 8%）。描く frame はその pass を draw の前に通る。kwl_glass_tick の中の 25 個ほどの tick のどれかは未特定（network・audio・bluetooth の backend の読み、時計の syscall の数が候補）。
- 打鍵の遅れ（d133 の keys の段、200 ms 間隔 22 字、key-latency.py、seat に key が来てからの ms、中央値/最大）: Terminal が受けた 0.7/2.4、pty へ書いた 1.0/2.4、echo を読んだ 1.7/3.4、Terminal の present 9.7/11.4、compositor の adopt 15.9/19.4、compositor の frame の完了 30.0/36.0（この後 display の次の blank まで 0〜16.7）。内訳: echo → present 8 ms（Terminal の 1 frame の描画）、present → adopt 6 ms（Terminal の GPU の仕事と worker の commit、FIFO の待ちは 0.2〜0.4 ms）、adopt → 完了 14 ms（compositor の 1 frame: 先取りの READY 1、main の起床と pass 2〜5、record 3、submit 2、present 2、GPU 2）。input method は通らない（key は client へ直に、0.2 ms）。
- 速く打った時（type の段、60 字）は Terminal が受けるまで 12.7 ms（描画中で受けられない）、完了まで中央値 80 ms: throughput が 61 字/s に届かない分の溜まり。
- 間隔を空けた打鍵の 30 ms のうち、小さく安全に削れる所は今は無い: Terminal の描画（build 2.7・upload 1.5 は変わった行だけ作れば約 3〜4 ms 減るが中規模の変更）、compositor の main の pass の tick（約 1〜2 ms、どの tick かの特定が先）、submit・present の往復（driver 側、P2）。

### ws270-p007 の成果（2026-10-22、q1552 のまとめ）

- 朝（d97・d98、main 782f995e4 相当）: drag は毎 frame 全画面 work 24〜43 ms・period 39〜73 ms（14〜25 fps）、App Home・Wiseview は全画面 glass で submit 40〜80 ms、Home の frame は 2 blank ごと（30 fps）。
- 今（d128・d129、frame-ops.sh、1920x1080）: drag 60 fps、App Home 58〜60、Wiseview 60〜62、Terminal の起動 55〜58、type 44（入力 61 字/s に対し 1 字の cycle 20 ms）、output 35〜40、scroll・hover は aat-input の間隔が律速（lag 9〜15 ms）。打鍵 → compositor の frame の完了 30 ms（200 ms 間隔）。
- 効いた変更（大きい順）: glass の map（99b63fedd・1ce1d3758、色ごとの全画面の glass を 1 度描いて読む）と Home・Wiseview の picture（5f0acec60・95db457c4・ce44dafb4）、drag を部分の書き直しに（fdc686ca4）、libvulkan の display の primary の present が acquire を塞がない（1f7936510）、Keiland の 2 frame の重ね（162059da7・c436c7db0）、prewarm（初回の hitch）、Terminal の command buffer の使い回し（bbf66e118）と変わった doubleword だけの upload（6e8ac6693・1e1296a8d）、display の image の先取り（8eeedadb4）、awaited の残りの直し（776007a31）。
- 効かなかった・外した変更: e7ea311a9（frame callback を give の時に、type の lag 悪化 → 67a35db60）、285e34b07、2d5d13052、5409fbade 単独（後で 776007a31 として戻した）。
- 残り（目標の 60 fps に届かない操作）: type・output（1 字・1 塊ごとに Terminal → compositor → callback → Terminal の直列で約 20 ms。compositor の 1 frame の固定費 vkQueueSubmit・vkQueuePresentKHR の各 2〜3 ms は P2 へ、main の pass の glass の tick 約 1 ms/pass は未特定）、Terminal の描画の build（変わった行だけ作る案は未実装）。ユーザーの判断（2026-10-22）で文字入力は 60 fps を要しない、最適化はここで終わり。
- 回帰に使う道具（plan/ws270/tests/frame-ops/、Master の Tools に登録して plan/tools/ へ移す候補）: frame-ops.sh（板で root、Keiland --log-frames・aat-input、操作の段ごとに session.log の行数を marks に）、frame-times.py（marks の段ごとの frame の work・period・fps・lag の表）、key-latency.py（間隔を空けた打鍵 1 字ごとの seat→Terminal→pty→present→adopt→完了の ms）、shot-diff.py（keiland-shot の 2 枚の画素の差、見た目が変わらない確認）。
