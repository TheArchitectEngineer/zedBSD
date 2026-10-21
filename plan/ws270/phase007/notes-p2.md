# ws270-p007 P2 の記録（q1539、driver の draw の道）

板は Q1 だけが触る。ここには host の確認と、板で見る手順を書く。板の結果は Q1 が phase.md に書く。

## q1539-i01（2026-10-21、P2）

### 1. 1 job の draw の上限（P1 の報告と d48 の実測から）

- d48（Q1）: 3 window の desktop の frame は全画面 1 pass で 263 draws → job 5 個・全画面の LOAD 4 回、rec submit 約 390 ms/frame。103 draws → job 2・LOAD 1 回で約 275 ms。
- 変更（2d3c67809）: `POWERVR_RENDER_DRAWS` 64 → 512（render-pass.h）。job の general の配置を広げた（render-job.c: stream 0xa100 byte、scissor 表 513 個、`GENERAL_BYTES` 0x2000 → 0xc000）。365cf27ec は 256 にした途中の版。
  - job の struct は 144 KB（draw 1 つ 280 byte）。submit ごとに kern_calloc で job の数 + 1 個分を取り、build の後に返す。LOAD の job は 3D の間 1 個の写しを持つ。
  - draw ごとの memory（general・PDS 各 4 KiB/draw）は従来どおり draw の数で取る。
- 危険: 1 job の primitive が増えると parameter buffer（global の free list 16 MiB、grow しない、SPM 無し）を多く使う。全画面 1080p の draw 1 つで約 64 KiB の control stream の見込みで、全画面の draw が約 250 個で尽きる。尽きると dmesg に `asked to grow`・`partial render`・`parameter memory ran out` が出る。出たら上限を下げる。
- 確認（host）: p010 の host 試験 14 本すべて 0 failed。VF2 の vmunix rc 0、warning 0。板は未実施。

### 2. 全画面の quad の語の Mesa との比較

Mesa 26.2.5（drm-shim、BVNC 36.50.54.182、`PVR_DEBUG=cs`、vkmatrix の perf-flat、harness は build/mesa-pco/flat.txt の job 20）とこちらの render-draw.c の語。

| 項目 | Mesa | こちら |
| --- | --- | --- |
| ISPA passtype | PUNCH_THROUGH（Mesa の pco が sample mask を discard に lowering するため。discard 無しの shader なら OPAQUE） | OPAQUE（blend の時 TRANSLUCENT） |
| ISPA dwritedisable・dcmpmode | 1・ALWAYS | 同じ |
| ISPB（pres_ispctl_fb） | あり（stencil の既定） | 無し |
| ISPCTL scenable・bpres | 1・1 | 1・0 |
| pres_pds_state_ptr1（varying base） | 無し（coefficient program が無い時） | 常にあり、無い時は 0 |
| pds_tempsize | 0 | 1（4 語） |
| pds_tri_merge_disable | 1 | 1（depth を使わない時、q1536 で Mesa と同じに） |
| usc_sharedsize・pds_uniformsize | 16 語・12 語 | shader の数による |
| region clip | OUTSIDE、16 px の tile で right 119・bottom 67 | 同じ式 |
| viewport・wclamp・ppp_ctrl | 同じ値 | 同じ |
| output select | vtxsize 5、psprite_size_pres | vertex の出力の数、psprite 無し |
| VDM state | cam 31・16 instances | 同じ表（cam_size） |
| frag の command の flags | DISABLE_PIXELMERGE（常に） | 無し |
| frag の command の他の語 | clear だけの job と同じ（address 以外） | q1536-i02 で clear と同じと確認済み |

- 読み: 3D の時間の主は draw の側（d4 の timing で TA 1.6 ms、3D が大半）。固定費は約 1.4 µs/tile（全画面 8160 tile で約 11 ms）で、tile ごとに parameter buffer（region header → control stream → primitive block）を読む費用の疑いが強い。GPU の read が約 400 MB/s 相当（LOAD・texture）なら tile あたり約 300 byte で数 ms になる。これは P3 の memory の read の速さと同じ根の可能性がある。

### 3. 試しの switch と perf の case（fc1e5f0a5）

- `POWERVR_DRAW_TRIAL`（render-draw.h、既定 0）: bit 0 coefficient program が無い時に varying base を出さない、bit 1 pds_tempsize 0、bit 2 blend の無い draw を PUNCH_THROUGH に。
- `POWERVR_FRAGMENT_PIXEL_MERGE_OFF`（render-command.h、既定 0）: frag の command に DISABLE_PIXELMERGE（0x8000）。
- vkmatrix に perf-tri1（全画面を三角形 1 つ）、perf-tri8（2 x 2 の quad）、perf-tri128（8 x 8 の quad）、perf-stack4（全画面の quad 4 枚を 1 draw）、perf-draws4（全画面の quad 4 枚を 4 draw）を足した。不透明の quad は重ねても pixel を 1 回だけ shade するので、stack4・draws4 の傾きは tile ごとの primitive の費用、tri の数の傾きは三角形 × tile の費用を表す。
- 確認（host）: lavapipe で vkmatrix 41/41 PASS。p010 の host 試験 0 failed。trial の値（DRAW_TRIAL=7、PIXEL_MERGE_OFF=1）で render-draw.c・render-command.c が -Werror で通る。VF2 の vmunix・bin/vkmatrix rc 0、warning 0。板は未実施。
- 板の試しの kernel（commit しない使い捨て、fc1e5f0a5 の header の既定だけを替えて build）:
  - build/q1539/vf2/vmunix: 既定（switch 0、512 draws）、sha256 6dec829f775fb0fa…
  - build/q1539/vf2-ta/vmunix: `POWERVR_DRAW_TRIAL` 3 と `POWERVR_FRAGMENT_PIXEL_MERGE_OFF` 1（PUNCH_THROUGH 以外を Mesa に合わせる）、sha256 fe6c1611f5bea1a2…
  - build/q1539/vf2-tb/vmunix: `POWERVR_DRAW_TRIAL` 4（PUNCH_THROUGH だけ）、sha256 590718a0dac284ab…

## q1539-i02（2026-10-21、P2）

### 板の d50（Q1、512 draws の既定の kernel）と ta の読み

- d50: parameter の溢れ 0、HWR 0、vkmatrix 41/41。perf（us/frame）: clear 3850、flat 15700、immediate 15200、quarter 7250、tri1 15750、tri8 15700、tri128 16050、stack4 16000、draws4 19200、load 18750、texture 38150。
  - tri128≈tri1、stack4≈flat → 三角形 × tile も parameter buffer の read も主因でない。費用は shade する pixel の数に比例する。
  - P3 の perf-read で、texture の read はほぼ費用 0 → memory の read は遅くない。
- ta（varying base を出さない・pds_tempsize 0・DISABLE_PIXELMERGE）: clear・flat・immediate・quarter は変化なし、draws4 は 19200 → 16050。全画面の draw を足す費用が、1 つ約 1.1 ms から約 0.02 ms になった → 3 つを既定にした（a8f297ddb）。どれが効いたかは未分離。
- 読み（未確認）: shade の費用は fragment program の group の数。immediate.frag は 15 group、clear の background の program は 1 group。tc（clear の program を 14 個の即値で 15 group にする試し、POWERVR_CLEAR_TRIAL_PAD）で確かめる。

### compiler の直し（c0a079e60）

- IR:
  - 死んだ代入の除去（次の構造の命令までに読まれず書き直される、または読まれない 1 語の値）。
  - copy の伝播（move の先を読む所を move の元に。core の定数の register の 0・1・1.0・全 1 も）。
- blend:
  - 因子が channel に依らない時は 1 回だけ計算する（premultiplied の 1−a）。
  - ONE と ZERO の乗算を省く。
  - 目的の側の項を FMAD にまとめる。
- clamp: MINMAX 2 つをやめ、FADD x,0 の .sat 1 つにした（Mesa の pco の fsat と同じ）。IR の flag は POWERVR_IR_SATURATE、機械の命令は saturate。
- 機械の命令:
  - 枝の無い fragment では、計算の結果を後の move の先へ直に書き、move を省く。
  - 枝の無い fragment の最後の move・即値に END を乗せる（Mesa の pco_end と同じ）。
- 試験: WS269 の compiler の simulator（machine-run.c）に end の flag と saturate を、IR の interpreter（ir-run.c）に saturate を足した（Q1 の許可）。

| shader | 前 | 後 |
| --- | --- | --- |
| Keiland quad.frag | 51 | 19 |
| Keiland batch.frag | 100 | 60 |
| panel.frag の変種 [0]〜[7] | 158・104・99・96・103・69・148・174 | 123・73・65・65・72・36・108・139 |
| vkmatrix immediate・push4・texture | 15・16・25 | 6・6・12 |
| quad.vert | 42 | 25 |

- 確認（host）: compiler 411・22 passed、p010 は全 0 failed、p009 は 505 passed・0 failed、VF2 の vmunix は warning 0。板は未実施。
- 危険: vertex の出力の書き込みが core の定数の register（0・1.0）を直に読む。hardware で読めるかは板で確かめる。

### 直した試験（Q1 の許可、a8f297ddb）

- p010 command-host-test: fragment の flags に DISABLE_PIXELMERGE。
- p010 draw-host-test: vkdemo の PPP の SIZEINFO1 の pds_tempsize を 0 に。
- p009 vk-host-test・frame-host: keiland B を 2×POWERVR_RENDER_DRAWS+8 draws にし、R512 R512c R8c を確かめる（512 draws の前提に追従、2d3c67809 で落ちていた 2 件）。

### 板の組（Q1 に送った）

- build/q1539/vf2/vmunix（a8f297ddb、sha256 879a868ba79183d5…）と build/q1539/vf2/bin/vkmatrix（sha256 0aca9fc3a0f96599…）。
- build/q1539/vf2-tc/vmunix（a8f297ddb と、clear の padding 14 の使い捨て、sha256 2a0814d907139e3a…）。

### compiler の 2〜4 回目（7b613a1c0・93546829a・e51fbc510）

- 7b613a1c0:
  - 連続する iterate を 1 つにする（count を variant＋1 に）。
  - 枝の無い program で、同じ即値の作り直しを省く。
  - fragment の add(mul, x) を FMAD にする。SPIR-V の NoContraction がある shader ではしない（IR の exact）。
  - 誰も読まない値の代入は、構造を越えて除く。
  - simulator に複数語の iterate を足した。
- 93546829a: shared register の語を float の演算（FADD・FMUL・FMAD）が直に読み、move を省く。encoder の form では s1 が shared を名指せないので、s0・s2 に置く（入れ替え、だめなら move）。
- e51fbc510: fragment の clamp(x, 0, 1)（min・max の 2 つ）を FADD.sat 1 つにする。

| shader | 元（c0a079e60 の前） | e51fbc510 |
| --- | --- | --- |
| quad.frag | 51 | 17 |
| batch.frag | 100 | 55 |
| panel.frag の変種 [0]〜[7] | 158・104・99・96・103・69・148・174 | 100・58・52・53・57・32・71・114 |
| panel.frag（fallback） | 511 | 375 |
| quad.vert・panel.vert | 42・62 | 21・30 |

- host: compiler 411・22 passed、p010 全 0 failed、p009 505 passed、VF2 warning 0。板は未実施。
- 板の kernel: build/q1539/vf2b/vmunix（93546829a、sha256 1a64b714de5ccba4…）、build/q1539/vf2c/vmunix（e51fbc510、sha256 9a4d5f32aeb635b4…）。

### 板の d55（Q1、vf2c = e51fbc510）

- vkmatrix 41/41、HWR 0、parameter の溢れ 0。
- perf（us、前は d50）: clear 4000、flat 9200（15700）、immediate 9200（15200）、quarter 5650（7250）、tri1 9000、tri8 9200、tri128 9500、stack4 9300、draws4 9100（19200）、load 10250（18750）、texture 17750（38150）。
- 読み:
  - group の数が主因と裏付いた。
  - flat と quarter から、6 group の program は面積に比例する費用が全画面あたり約 4.7 ms、面積に依らない費用が約 4.5 ms。group 1 つは全画面 1080p あたり約 0.8 ms。
  - Mesa の pco も 1 命令 = 1 group で、別の命令を 1 group にまとめる事はしない（pco_group_instrs.c）。Mesa の quad.frag は 67 group、こちらは 16 なので、残りは shader の math と描く pixel の数（Keiland の側）になる。

### d4bd75b22（main 3029404e9 を merge した後）

- 0 と 1 だけの定数の色の pack を compile の時に畳む（immediate.frag 6→2 group）。
- blend の時の pixel output の読みは、UNPACK が olchk 付きで直に読む（blend する shader で 1 group 減）。
- main の config の CONFIG_INPUT_TEST_INJECT y は、riscv64 の vmunix.mk に input-inject.c が無く link できない（Q1 に報告）。板の kernel は `CONFIG_INPUT_TEST_INJECT=n` で作った: build/q1539/vf2d/vmunix（sha256 209cafbf4c0ab87e…）。

### 3f1de35ac・b3995c934・92789513b（glass の math、Q1 の依頼「shader の実行の速さ」）

- 3f1de35ac:
  - fusion の前にも、誰も読まない代入を除く（fusion を外していた 2 つ目の書き込み）。
  - 単独の結果の .sat を、それを作る FADD・FMUL・FMAD に付ける。
  - GLSL の mix(x, 0 or 1, t) を x·(1−t)〔+t〕にし、1−t は同じ重みの語の間で 1 回だけ計算する。
- b3995c934: vkmatrix に perf-alu4 を足した（独立した FMA の鎖 4 本・74 group）。perf-alu（依存する鎖 1 本・71 group）と比べて、依存の待ちを分ける。
- 92789513b:
  - fragment の float の定数は shared register の literal の request で置く（1 program に 10 個まで、request が 20 未満の時。両方の source が shared なら即値に戻す）。
  - shared を読む和は FMAD(x, 1.0, shared)。
  - simulator に literal の読みを足した。
- group 数（e51fbc510 → 92789513b）: glass の [0] 100→84、dark の [7] 114→93、blur の [6] 71→61、[1]〜[5] は 53・47・48・52・27、batch.frag 55→49、fallback の panel.frag 375→351、quad.vert 21→17。
- 調べた事（Mesa 26.2.5 の source）: pco は 1 命令 = 1 group（pco_group_instrs.c）で、uniform だけの計算を pixel の外へ出す最適化（nir_opt_preamble）も使っていない。glass の box の中心・1/screen など約 10 group は、pixel ごとの計算のまま。
- host: compiler 411・22 passed、p010 全 0 failed、p009 505 passed、VF2 warning 0。板は未実施。

### Mesa pco と我々の compiler の比較（2026-10-21、ユーザーの指示「同じシェーダについて、Mesaのコンパイラが出力したものと、我々の出力で、どれくらい違うかも見てください。命令数、分岐の扱い、レジスタspillなど、最適化ができていないところは直しましょう。」）

- 条件:
  - 同じ SPIR-V（userland/desktop/wayland/shaders.h、libkeiland/ui/shaders.h、userland/tests/vkmatrix/shader.h）、同じ key（B8G8R8A8_UNORM、Keiland は premultiplied の blend ONE/ONE_MINUS_SRC_ALPHA、vkmatrix は blend 無し）。
  - Mesa 26.2.5 は drm-shim、BVNC 36.50.54.182、`PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1 PCO_DEBUG_PRINT=all,stats`、vertex と fragment を 1 つの pipeline にした（Mesa の vertex は link された数）。
  - ours は 92789513b を host の probe で数えた。
  - 道具: scratchpad の mesa-stats.c（build/mesa-pco/pcostats.c の拡張）と stats.c。repository には入れていない。

| shader | Mesa FS group（temps、control group） | ours FS group（temps） | Mesa VS | ours VS |
| --- | --- | --- | --- | --- |
| quad | 67（32、8） | 16（18） | 21 | 17 |
| panel（fallback） | 393（24、46） | 351（51） | 27 | 26 |
| batch | 83（13、7） | 49（20） | 16 | 34 |
| turn | （quad.frag） | （quad.frag） | 19 | 21 |
| panel[0] glass | 141（16、8） | 84（20） | 19 | 26 |
| panel[1] shadow | 101（13、7） | 50（18） | 19 | 26 |
| panel[2] image | 91（13、8） | 47（18） | 27 | 26 |
| panel[3] solid | 92（13、7） | 48（18） | 19 | 26 |
| panel[4] ring | 97（13、7） | 52（18） | 19 | 26 |
| panel[5] text | 83（32、8） | 27（18） | 21 | 26 |
| panel[6] blur | 138（31、12） | 61（26） | 21 | 26 |
| panel[7] dark glass | 153（16、8） | 93（20） | 19 | 26 |
| canvas（libkeiland） | 67（32、8） | 16（18） | 17 | 21 |
| vkm push4 | 28（9、1） | 6（9） | 6 | 7 |
| vkm immediate | 35（6、1） | 2（3） | 6 | 7 |
| vkm alu | 220（6、1） | 70（10） | 6 | 7 |
| vkm alu4 | 225（10、1） | 73（13） | 6 | 7 |
| vkm texture | 59（32、8） | 10（14） | 7 | 11 |

- spill: どちらも全部 0。ours は spill を持たず、temps が 96 を越えると compile が失敗する。今の最大は fallback の 51。
- 分岐:
  - 変種の panel・quad・batch・canvas は、どちらも分岐なし。
  - fallback の panel.frag は、どちらも本当の branch と EMC の predication。Mesa は br 16・cndst 7・cndef 6・cndend 6・wdf 10、ours は BR 12・COND 19・WAIT 12。
  - divergence は、どちらも EMC で無効の instance を止め、全部が無効の時だけ branch で飛ぶ。
- texture: sample 1 つにつき wait 1 つで、どちらも同じ。vec2 の iterate はどちらも 1 命令。
- 定数と shared:
  - Mesa は定数ごとに movi32（alu で 136 の bitwise group）、shared は mbyp で temp に移してから使う所が多い。
  - ours は shared をそのまま読み、定数は literal の shared。
- 残る差（ours が劣る所）:
  - vertex の group 数: batch 34 と 16、texture 11 と 7。vertex の入力を temp へ移す move と、link しない出力のため。
  - temps: 変種で 18〜26 と Mesa の 13〜16。fallback で 51 と 24。

### 1 group に 2 命令（phase 0 と phase 1、d6f569269・12ddbecc7・51688a583）

- 板:
  - d60（main と b3995c934）: alu 61100 us・alu4 55700 us で、ほぼ同じ → 依存の待ちではない。group あたり全画面で約 0.86 ms。
  - d61（d6f569269 の既定）: mul4 54300 us（74 group）。
  - d62（PAIR_TRIAL 1）: mul4 36500 us（42 group、0.67 倍）、46/46、HWR 0、見た目は同じ（Q1 の shot62.png）、desktop の全画面 frame は 71〜78 ms（81〜85 から）。
  - → group の費用は issue で決まる。
- 12ddbecc7（既定 on、POWERVR_SHADER_PAIR 2）:
  - 枝の無い program で、FADD・FMUL・FMAD・plain の MOVE の 2 つを 1 group の phase 0（s0〜s2、ft0、w0）と phase 1（s3〜s5、ft1、w1）に置く。Mesa の op_ref_maps で、この 4 つは phase 1 を持つ。Mesa 自身は 1 命令 = 1 group で、組を作らない。
  - 12 命令先まで探し、間の単純な命令と読み書きが重ならない物だけを隣へ動かす。
  - 組の条件: 2 つ目が 1 つ目の結果を読まず、同じ register に書かない（group の中では両方が書く前に読む）。
  - encoder が受ける組だけを作る。upper set の s4・s5 は shared を名指せない。
- 51688a583: phase 0 に置けない組は、順を逆にして試す（shared の addend は phase 0 でしか名指せない）。
- group 数（命令 → group）: panel[0] 84→61、[7] 93→67、[6] 61→40、[3] 48→31、quad.frag 16→12、batch.frag 49→32、text.frag 22→16、vkm mul4 74→41。
- 残る単独の group: 依存の鎖、MINMAX・SINGLE（rcp・rsq。phase 1 を持たない）、ITER・SMP・WAIT・PACK・UNPACK・olchk の MOV。
- host: compiler 411・22 passed、p010 全 0 failed、p009 505 passed、vkmatrix（lavapipe）46/46。
- 板の kernel: build/q1539/vf2h/vmunix（12ddbecc7、sha256 9bad8ad4987b34be…）。FMAD・MOVE の phase 1 は hardware で初めて使う。

### d64（vf2h = 12ddbecc7）と、その後（a7eb3188e・711dea277）

- d64（Q1）: 46/46、HWR・reset 0、見た目は同じ（shot64.png）。perf: mul4 36050、alu 48250、alu4 52150、flat 6800（8050）、immediate 3350、quarter 4050、texture 13050（14250）、load 8000、load-small 2900。desktop は全画面 64〜74 ms（71〜78）、部分 12.0 ms。
- repeat の費用: flat（push4 = MOV の組 2・PACK rpt4・MOV、4 group）と immediate（2 group）の差 3.45 ms は約 5 slot → rpt4 の PACK は 4 slot。unorm8 の出力の PACK rpt4 と、blend の UNPACK rpt4 は、どの shader でも pixel ごとに 8 slot かかる（Mesa も同じ）。
- F32 の pixel output（PBE が unorm8 へ変換する案）は取り下げた。Mesa の pbestate.xml の SOURCE_FORMAT の注記で、F32 の source は F32 の packing mode でしか使えない。F16 の source は U8 へ変換できるが、pack が 6 slot で今の 5 より悪い。
- a7eb3188e: 2 つ以上の FMAD の addend になる定数を、program の始めに temp に置く（upper set の s5 が shared を名指せないので、組を妨げていた）。alu4 73→41 group。
- 711dea277: x·1.0 を copy にする（1.0/x が rcp と mul だったのを rcp 1 つに）。vkmatrix に perf-rcp（依存する rcp 32 個、38 group）を足した。perf-mul4 との比で rcp 1 つの slot 数を見る。
- 板の kernel: build/q1539/vf2i/vmunix（711dea277、sha256 910e868996b965da…）、build/q1539/vf2i/bin/vkmatrix（sha256 3b1f30647707c226…）。

## q1553（2026-10-22、P2）: 全ての操作で 60 fps（driver 側）

branch agent/p2-q1553（基 main 782f995e4）。

### idle の後の 139 ms（72ecf8ec6）

- 原因（読みからの推定）: vk-queue.c の submit() が POWERVR_TIMING_SUBMIT を足した直後に powervr_timing_report() を呼んでいた。5 s ごとに約 9〜14 行を kern_logf で同期に出し、debug の UART と text console に書く。idle の後の最初の submit で必ず出る（kick の後、vkQueueSubmit の中）。
  - 「job wait（kick → present の submit での reap）139 ms ＝ Keiland の rec submit 139 ms」と合う。kernel の submit の計測（284 us）は report の前で終わる。
  - d95a の app の起動時の 191 ms（frame 15）も `pace waited_ms=41101` の idle の後の最初の frame だった（同じ原因に import の待ちが足された物と読む）。
- 直し:
  - POWERVR_SUBMIT_TIMING の既定を 0 に（計測の build は `ZEDBSD_TEST_CPPFLAGS += -DPOWERVR_SUBMIT_TIMING=1`）。
  - 1 の時も report は専用の kernel thread（timing_thread、100 ms ごとに終了を見て 5 s ごとに出す）が出す。
- 他の 5 s ごとの report の点検: submit・present・割り込みの道で同期に出す物は timing だけ。job trace の report は POWERVR_JOB_TRACE の計測 build だけ。

### vkQueueSubmit の 7〜14 ms（b44f248a7 の診断、b60c38183 の直し）

- 診断: libvulkan に `VULKAN_ZEDBSD_SUBMIT_TIMING=1` の時だけの submit の内訳（5 s ごとに stderr へ `VULKAN TIMING`）を足した。
- d101・d102（Q1）の結果:
  - frame の vkQueueSubmit の大半が execute（QUEUE_SUBMIT の ioctl）で 13〜15 ms。
  - kernel の powervr の submit() は 0.7 ms。render jobs 0.2 ms、order wait 0、reap 2 us。
- 原因:
  - libvulkan の transaction は decoder の fence（GPU_COMMAND_SUBMIT の CONTEXT_FENCE）の完了を待って reply を読む。
  - powervr_render_frame はその fence を、session の最後の GPU の point の後に ring へ入れていた。そのため全ての transaction が、直前に kick した GPU の仕事の完了まで返らなかった。
  - libvulkan はその間 context->mutex を持つので、compositor の他の thread も止まっていた。
- 直し b60c38183:
  - command の frame の callback はその場で完了する。
  - 同じ point に callback の無い entry を入れる。これで、仕事が in flight の間は resource_unmake の遅延 unmap が効く。
  - 失敗した frame・error の後の frame・空の marker の frame は従来どおり。
- 板は未確認（Q1 待ち）。

### compiler（363d9226d・2feac9314・9d7db594a）

- 363d9226d: iterate and sample の 4 channel をそのまま 1 つの sum of products（不透明・OVER）だけが読む時、f16 で 2 register に返し、sum of products の half の form で読む。
  - form の byte は DDK の perf-texture・perf-texture-nob の cache に合わせた（derived from observation）。temps 6→4。
  - d101: texture 2950・texture-nob 2900 us で変化なし。
- 2feac9314: mix(x, y, z)（y・z は uniform）を x·(1−z) + y·z に（1−z と y·z は shared program）。
- 9d7db594a の 1: clamp の内側の操作を saturate の融合の前に消す（clamp の saturate が演算に付かずに残っていた）。
- 9d7db594a の 2: float の min・max の定数の bound を shared の literal で読む（即値の group が減る）。
- 9d7db594a の 3: (x + u1)·u2 を x·u2 + u1·u2 にする（factor が sum より後で読まれる時は、sum を product の直前へ動かす）。
- group 数（782f995e4 → 9d7db594a、host）:

| shader | 前 | 後 |
| --- | --- | --- |
| glass | 49 | 40 |
| 不透明な glass の内側 | 28 | 19 |
| glass の内側 | 30 | 22 |
| dark glass | 55 | 47 |
| dark の内側 | 37 | 30 |
| 不透明な dark の内側 | 34 | 27 |
| 影 | 27 | 25 |
| image | 25 | 24 |
| solid | 22 | 21 |
| batch | 27 | 26 |

- d102（Q1）: 見た目は正常（glass も）。
- 試した物:
  - 解放の古い register から割り当てる allocator: shader ごとに ±1〜2 group で、総和はほぼ同じなので入れない。
  - ITR＋WAIT の WAIT を iterate 自身の wdf に: itrsmp の 0x20 が速さを変えなかったので、やめた。
- Keiland への提案（P1）: glass は `point / panel.screen.xy` を pixel ごとに 2 つの FMUL で作り、SMP と WAIT を挟む。vertex の側で uv を別の varying にすれば itrsmp に乗る（約 4 group 減）。

### その他

- ef034bd05: render-job.c の uncached の alias への job の書き込みを 64-bit の store に。
- d102 の kernel の timing（全 session の合計）に見える物:
  - draw memory 平均 400〜500 us（kept の job の memory が足りず作り直している疑い）
  - job setup が 1.6 ms の窓がある
  - transfer 2.9〜4.8 ms/回（同期の transfer）

### d103 と、その後（52a0ce932・d5e6d9475）

- d103（Q1、b60c38183 の q1553t ＋ P1 95db457c4 の wayland）:
  - vkmatrix 109/109。context reset・HWR・page fault・device lost は 0。desktop の操作（app の起動・窓の drag・Home・Wiseview の開閉）で異常なし。
  - VULKAN TIMING: submit 平均 1471〜1884 us（前 7660〜8785）、execute 957〜1326 us（前 7196〜8452）。
  - frame-ops の work p50/max: launch 8/76、type 7/32、drag 8/10、home 6/14（前 17/31）、wiseview 7/50（前 24/64）。
  - 残る context_hold の max 32 ms は opcode 21（ALLOCATE_MEMORY）と 18（QUEUE_SUBMIT）。
- d5e6d9475: powervr_buffer_create の 0 埋めを、uncached の alias（store 1 回ごとに bus の write、8 MB で約 30 ms の見積）から、direct map の kern_memset に変えた。その後の alias の作成が、範囲の line を書き戻して捨てる。
- 52a0ce932:
  - session の render target を 4 → 8（framebuffer の大きさが 5 種以上で毎 frame 作り直していた疑い）。
  - 取った kept の job の memory の draw の buffer が小さい時、keep の中の合う物と交換する。
  - timing に `target make`。
- d5e6d9475: timing の各行に `longest`。
- 板は未確認（Q1 待ち）。
