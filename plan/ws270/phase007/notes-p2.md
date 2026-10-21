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
