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
