<!-- awesome-plan project=zedbsd record=ws270-p007 -->

# ws270-p007 VF2 の desktop の速さ（使える水準へ）

- WS: [WS270](../ws.md)。
- Status: in-progress（2026-10-21 Q1）。前提: p006a（desktop が PowerVR で動く、lockup 解決）。
- 出典: 2026-10-21 ユーザー「lockupは解決したのですが、描画が遅すぎてデスクトップが利用可能な水準ではありません。たぶん2fpsくらいです。これを解決したいです。」
- 板: Q1 だけが触る（kei@10.0.30.2、SwitchBot で電源）。担当は commit と板で流す手順を Q1 に送る。

## 受け入れ

2026-10-22 ユーザー「すべての操作で16msで描画でき、60fpsになることが目標です。ただ、どうしても難しければ、retireしてかまいません。おそらく限界まで最適化して、1280x720に落とせば、目標は自動的にクリアされています。」→ 全ての操作で 1 frame ≤ 16 ms（1920x1080）。難しければ retire してよい。

### 前の案（2026-10-21 Q1、ユーザーに示した）

1. カーソルの移動・文字の入力・窓の一部の更新が目に見えて滑らか（KWL の frame の GPU の時間 ≤ 16 ms）。
2. 窓の移動・全画面の書き換えが 20 fps 以上（frame ≤ 50 ms）。
3. 見た目は同じ（keiland-shot で比べる）、vkmatrix 全 PASS・HWR 0。

## 出発点の数（板 d42〜d46、1920x1080、396 MHz）

clear 3.85 ms、LOAD 23.35 ms、平らな quad 15.1 ms、1/4 面積 7.2 ms、402 op の ALU 268.75 ms、texture 38 ms。KWL の rec submit 108〜162 ms/frame。
固定費 ≈ 5.4 ns/pixel（≈ 2 clock/pixel、BXE-4-32 の名目は 4 pixel/clock）。LOAD・texture は GPU の read が約 400 MB/s 相当。

## 道（並行）

| Queue | 担当 | 道 |
| --- | --- | --- |
| q1538 | P1 | Keiland: frame ごとに描く量を減らす（damage の範囲だけを scissor で描く、壁紙・窓の再 sample を減らす、全画面の redraw をやめる） |
| q1539 | P2 | driver の draw の道: 全画面の quad の PPP・ISP・PDS・pixel の出力の語を Mesa と比べる、PUNCH_THROUGH、三角形の数を変えた perf |
| q1540 | P3 | GPU の memory の read の速さ: SLC bypass・cache の属性・MMU・device-local を cached にすると板が再起動する原因 |

## 板の結果

- d48（main の kernel、P1 dee85df07 の計数）: 3 窓の frame は全画面 1 pass、263 draws → 64 draw ごとに job 5 個・全画面 LOAD 4 回、rec submit 約 390 ms。
- d50（P2 2d3c67809 512 draws、P1 ef1a02591 glyph の batch）: 132 draws・1 job、job wait 約 305 ms。perf: clear 3850、flat 15700、tri1/tri128 15750/16050、stack4 16000、draws4 19200、load 18750、texture 38150 us。P3 perf-read: 8 MB の texture の read は追加 0 → memory の read は主因でない（q1540 の道は閉じる、SLC の試し B・D は未実施）。
- d51（ta: ptr1・pds_tempsize 0・pixel merge off）: draws4 16050 の他は不変。d52（tb）は起動の不調で未測定。
- d53〜d55: HDMI の cable が抜けていて boot framebuffer なし（ユーザー 2026-10-21「すみませんHDMIが抜けていました。」）、desktop は出ず。d55 の perf（P2 vf2c、compiler で fragment の group 数を約半分、ta 既定）: clear 4000、flat 9200、quarter 5650、draws4 9100、load 10250、texture 17750 us、vkmatrix 41/41、HWR 0。
- d56（vf2c + P1 98c7a4ccf 壁紙の穴）: 見た目同じ（build/review/vf2-desktop/shot56.png）。3 窓の全画面 frame 165 draws、rec submit 約 160 ms。terminal の出力の frame は部分（916x632）だが blended 2.5 M px（area の 6 倍）で約 66 ms → P1 の opaque region（案 A）へ。
- d57（P1 c3dde7fc7: 上の窓の不透明な所の下を描かない・opaque region・desktop の描いた範囲）: 見た目同じ（shot57.png）。terminal の出力の部分 frame は rec submit 約 13.5 ms（d56 は 66）、3 窓の全画面の frame は約 85〜100 ms。
- d58（glass を切る、ユーザーの問い「glass effectを切ったとき…それでも遅さが変わらないですか？GPUからのメモリのリードが遅い？」）: 全画面の frame 約 38 ms（MODES quad 5/4.98 M px ≈ 2.4 画面分、perf-texture の 17.75 ms/画面と合う）、部分 frame 13.3 ms（job wait 約 10 ms）。→ glass の費用は shader の計算。memory の read は d50 の perf-read で遅くない。残る driver の課題: shader の group あたりの費用（P2）と job の面積に依らない固定費（P1 q1541）。
- 2026-10-21 ユーザーの指示で Keiland の作業は止め、driver に集中（Master の decisions-log）。
- d59（P1 c69651a44 だけ、main 基で P2 の compiler 無し）: 比較にならない（shader が古い）。timing: 部分 frame の job wait 15.3 ms、map 0.3・build 1.1・release 0.6 ms。
- d60（Q1 の合成 c53546e6a = main 9bfff4fe3 + P2 b3995c934 + P1 42389dad2）: vkmatrix 45/45、HWR 0。perf: clear 3850、flat 9250、immediate 4750（2 group）、alu 61100・alu4 55700（依存でなく issue の速さ、約 0.86 ms/group/全画面）、load 10350、texture 16650、load-part（916x632）5850、load-small（64x64）4050 us → 面積に依らない固定費 約 4 ms は残る（空 tile の skip の効果なし）。desktop: 部分 frame 12.5 ms、全画面 frame 80〜88 ms。
- 2026-10-21 ユーザー「同じシェーダについて、Mesaのコンパイラが出力したものと、我々の出力で、どれくらい違うかも見てください。命令数、分岐の扱い、レジスタspillなど、最適化ができていないところは直しましょう。」→ P2 q1539 に追加。
- 比較の表（P2、Mesa 26.2.5 pco と ours の fragment の group 数）: Keiland の全部の変種で ours が少ない（quad 67→16、glass 141→84、dark glass 153→93、blur 138→61）。spill はどちらも 0、分岐の扱いは同じ（変種は分岐なし、fallback の panel.frag は branch と EMC）。表は notes-p2.md。
- d61（Q1 の合成 faa09fa62 = main + P2 d6f569269 + P1 277ef9dd8、main bd7435336 と kernel の source が同じ）: 46/46、HWR 0。perf: clear 3400、immediate 3400、flat 8050、quarter 4450、alu 49950、mul4 54300、load 9050、load-part 4550、load-small 3100、texture 15600 us。desktop の部分 frame 13.3 ms、全画面 81〜85 ms。
- d62（d61 + POWERVR_SHADER_PAIR_TRIAL 1: 独立の FADD・FMUL を 1 group の phase 0・1 に）: 46/46、HWR 0。mul4 36500（0.67 倍）、texture 14250、load-small 2800。desktop の見た目は同じ（shot62.png）、全画面 71〜78 ms、部分 11.95 ms → issue が律速と確定、pairing を既定にして組を増やす（P2）。
- d63（P1 trace の kernel、main bd7435336 + 0d54f4bc0）: 48/48。small の job は sent 約 15 us、TA 約 600 us、3D 約 650 us。clear-64 2000、load-small 3100 us → GPU の外に約 1〜2 ms、tile の数には依らない。
- d64（P2 12ddbecc7: pairing を既定、12 命令先まで並べ替えて FADD・FMUL・FMAD・MOVE を組に）: 46/46、HWR 0、見た目同じ（shot64.png）。perf: mul4 36050、flat 6800、quarter 4050、texture 13050、load 8000、load-small 2900 us。desktop: 全画面 64〜74 ms、部分 12.0 ms。12ddbecc7 を main 3bd89fa96 に merge。
- d65・d66（P1 の trace、firmware の trace の全部）: small の job ごとに dust の電源の on・off。GPU の hardware の仕事は TA＋3D で約 220 us、残り約 1 ms は firmware（MIPS）の処理（TA の前 225 us、TA→3D 480 us、後片付け 330 us）。
- d67（B: host が dust の電源を入れたまま、DUST_POWER_BY_HOST 1）: 48/48、HWR 0。TA 639→500 us、3D は不変 → 電源の on・off は主因でない（既定に入れない）。
- d68（C: firmware の trace を記録しない）: 48/48。TA 250、3D 275 us（約半分）。load-small 2350 us。→ P1 16611bb41 で既定の trace groups を 0x401（trace on と HWR だけ）に。
- d69（D1: firmware の code を MIPS の MMU で cached、trace は全部のまま）: 48/48、gpu-selftest 13 ok、HWR 0。TA 約 300、3D 約 355 us。
- 2026-10-21 ユーザー「浮動小数点が32bitなのはちょっと気になるので、precisionを落とせないか確認するのがいいと思います。」→ P2: BXE の main ALU に F16 の vec2 の SIMD は無い（Mesa の ISA の表）、lp の形（fadd_lp 等）の板の試しを作った。
- 2026-10-21 ユーザー「Linuxを起動してシェーダーの実行時間を計測することができます。Linuxを起動する必要があると思ったとき、私に言ってください。」
- d70（P2 vf2j = 99ca2cfd1 既定）: 47/47。alu4 38000（52150）、rcp（38 group）28900 → rcp は掛け算と同じ費用。d71（lp の試し）: 速さは同じ（mul4 36150、alu4 37900）→ 精度を落としても issue の速さは変わらない、lp は使わない。
- d72（P1 af2e35267: trace groups 0x401 ＋ firmware の code cached）: 48/48、gpu-selftest 13 ok、HWR 0。small の TA 152〜186、3D 192 us（d66 639・608）。
- main 4b7d22191（P2 99ca2cfd1）・7af26e870（P1 99adb52fc: code cached 1・data 0・trace groups 0x401・job trace 0）。
- d73（main 7af26e870 の kernel）: 49/49、HWR 0、見た目同じ（shot73.png）。perf: flat 5950、immediate 2750、texture 12300、load 6950、load-part 3150〜3350、load-small 1950 us。desktop: 部分 frame 10.4 ms、全画面 59〜80 ms。CPU 側 timing: job build 約 1.4 ms/job が最大、wait 8.4〜10.6 ms、transfer 4.4〜4.8 ms/回。
- d74・d75（P1 6f16ceac4・11d87cd40）: job build 約 1.3 ms の内 draws 1.2〜1.3 ms、draw の数に依らない。shader の code を pipeline ごとに 1 度だけ置く直しで 1.22 ms。
- Linux（2026-10-21、ユーザーが VF2 で起動、StarFive 6.12.5、IMG の proprietary DDK 1.19@6345021、GPU 594 MHz）: initramfs に rgx.fw・rgx.sh を入れて driver を初期化（ユーザー「Mesaと比較するのではなく、proprietaryなドライバと比較するのがよさそうです」）。同じ vkmatrix 49/49。perf（594 MHz）: flat 1372、texture 2325、mul4 1911、draws4 1513、load-small 1964、clear-64 348 us（alu・rcp は DDK の compiler が畳むので比較不可）。396 MHz 換算で flat 約 2.06 ms（ours 5.95）、texture 約 3.5 ms（ours 12.3）→ pixel あたり 3〜3.5 倍の差。小さな job の固定費は同程度。全文 build/uat/vf2-desktop/runs/linux-ddk-perf.txt。→ P1 が DDK の kernel 側（img-rogue）の clock・firmware の初期化・register の設定と比べる。
- d77（P1 5bced8f66: draw の buffer の切り上げ・clear 無し、LOAD の job の複写の廃止）: 49/49、HWR 0。job build 1253→210 us。desktop の部分 frame 8.1 ms、全画面 64〜76 ms。main 5c005c15f に merge。
- d78（P1 01c0e812f: 594 MHz＋APM 100 ms＋SLC 64-byte interleave＋firmware の data cached）: 49/49、HWR 0。alu4 23750、mul4 23900、tri1 4200、texture 8500、load-small 1650 us。desktop の部分 frame 5.9〜9.7 ms、全画面 45〜61 ms。ALU は clock 分の 1.5 倍。DDK（594）との残りの差は flat 1.37 対 約 4 ms、texture 2.3 対 8.5 ms → SLC と FBCDC。
- d79〜d83（P1、594 MHz を既定 8ccc9435e とその上の 1 つずつの試し）: 全部 49/49、HWR 0。d79（594 既定）: flat 4350、tri1 4050、alu4 23700、mul4 23900、texture 8300、load-small 1650 us、desktop の部分 frame 5.8 ms、全画面 45〜59 ms。APM 100 ms（d80）・SLC 64-byte interleave（d81）・firmware の data cached（d82）は効果なし（既定に入れない）。d83 の register: CLK_CTRL 0x002aaa002a22aaaa、SLC_CTRL_MISC 0x3、SLC_CTRL_BYPASS 0x00001d1f01000000 で DDK と同じ → 設定の差は無く、残る差は FBCDC（圧縮）の疑い。8ccc9435e を main に merge。
- LINEAR の比較（2026-10-21、P1 の perf-*-linear）: DDK（594）clear 1219・clear-linear 1659、flat 1290〜1368・flat-linear 1896〜1999、texture 2225〜2242・texture-linear 2520〜2543 us。zedBSD（594、d83）clear 2200・2500、flat 4350・4300、texture 8450・8250〜8750 us。→ 圧縮の効果は DDK で 0.4〜0.6 ms だけで主因でない。差は pixel の shader の実行の費用（DDK は flat−clear 0.1〜0.3 ms、texture−clear 1.0 ms、こちらは 2.1 ms・6.2 ms）。次: PDS の非依存の texture の読み（DOUTT）などを P1 が調べる。
- WS278 の P3 b0e19a77f（bsp-rpi-common・bsp-rpi4・bsp-rpi5 の分離と規約の書き直し）を main 640ee8d31 に merge。rpi4 の QEMU raspi4b の boot-test を Q1 が流している。
- 2026-10-21 夜: 新しい SD（SA128、128 GB）で起動。dw-mmc の read の失敗が 1 起動で 48 回 → SD の card ではなく driver 側（BUG-343）の疑いが強い。
- d84（P1 c90be335c: push・descriptor を PDS の DOUTW で直に、main 5c83c16e4 相当＋）: 52/52、HWR 0。flat 4300、texture 8300、immediate 2350、clear 2300 us → 変化なし、DMA の待ちは主因でない。merge しない。読み: immediate（2 group、pack は compile 時に畳む）は clear と同じ、push4 の flat（4 group、実行時の PACK rpt4）は +2 ms。DDK は flat−clear 0.1〜0.3 ms → DDK は uniform だけで決まる計算（色の pack、glass の 1/screen 等）を draw ごとの shared の program に移している疑い。次: uniform の計算の hoist（P1 の新しい generation、q1546）。
- Fable 5.1 のレビュー（review-fable-20261021.md）: DDK の alu・rcp は hoist、DDK の clear は 4 px/clock の床、時計は 1 ms、8/16 lane が分かれ目、候補 A（出力の道の slot）・B（rate）・C（host 側）。
- d85（P1 q1546 f9f2a7929: uniform の hoist（shared program、既定 on）、PACK の pixout 直書き（0x2）、itrsmp（0x4）、vkmatrix -t で実行時に切り替え、batched の計測）: 既定 54/54、0x1 54/54、0x4 54/54、**0x2 は device lost（errno 42）で全滅** → 0x2 は使えない。HWR・context reset 0。既定（batched）: flat 2250（d79 4350）、immediate 2200、alu 3700（hoist）、texture 8200、**texture-nob 3550（blend の費用が約 4.7 ms）**、alu-pix 31500（68 group、pixel の rate 約 0.44 ms/group）、uniform 4850（0x1 で 9400）、load-small 1450、clear-64 1000、clear 1850。0x4 の itrsmp: texture 8250→7800、texture-nob 3550→3150。desktop: 見た目同じ（shot-d85.png）、部分 frame 5.4 ms、全画面 48〜52 ms。
- 残る大きな費用: texture の blend（UNPACK rpt4 と blend の計算）約 4.7 ms。DDK の texture（blend 付き）は 2.2 ms。
- Linux の 2 回目（2026-10-21 20:3x、DDK 1.19、594 MHz、batched の us、file は build/uat/vf2-desktop/runs/linux-ddk2/、git に入れない）: clear 975、immediate 985、flat 983、flat-blend（push の色 alpha 0.5 で OVER）1527、alu-pix（affine の鎖、畳まれた）1133、alu-pix-sq32（x*x*0.999+0.0005 を 32 段 = 64 op）28561 → 約 8 op/clock で zedBSD と同じ rate、texture（blend 付き）1974、texture-nob 1869、uniform 2023、load-draw 1561、load-small 約 1960（同期）、clear-64 353、load-64 約 580、quarter 981、draws4 988。perf-alu-pix-sq（64 段）は perf の行が出ず。ftrace は kernel に無く取れない。debug_dump の PERF_*_CYCLE は試験の合間に電源 off で読めず。取った物: firmware の trace（main・mts・pow、fwtrace-*.txt）、pipeline cache（cache/*.bin、DDK の shader の binary）、LD_PRELOAD の shim で KickTA3D2 と HWRTDataSet の in（shim/*.txt、steady は *-skip12.txt）、idle の debug_dump。
- 比較（zedBSD d85 の既定、batched）: clear 1850、flat 2250、texture 8200（itrsmp 7800）、texture-nob 3550（itrsmp 3150）、alu-pix 31500、clear-64 1000。→ ALU の rate は同じ、差は (1) blend（DDK +0.1〜0.55 ms、zedBSD +4.7 ms）、(2) clear・flat の床（DDK 約 0.98 ms、zedBSD 1.85〜2.25）、(3) 小さな job の固定費（353 対 1000 us）。
- q1549（P2、2026-10-21 21:27、commit 6194ba71f）: 完了の待ちを割り込みで起きる形に。render node の waitq `reached` を firmware の割り込みと service の各 pass が wake し、powervr_session_point_wait・powervr_render_finish・powervr_transfer_finish は sched_sleep(now+1) の poll をやめて眠る。割り込みが来ない時の上限 POWERVR_WAIT_GUARD_MS 10（service の busy の時も同じ）。lost・timeout の道は同じ。timing に `interrupts N, timeline waits W (L found after the guard), retires after the guard R` の行。build（config-visionfive2-desktop-gpu.mk）warning 0、host の p010 全 PASS・p009 505/0（render-queue.c は stand-in で host では通らない）。板は未実施。d65 の dump の「kccb executed 131886」は kccb の write 46 と合わず、その offset は疑わしい（interrupts 314 は job の数より多く、完了ごとに割り込みが来ている見込み）。vkmatrix の時計: zedBSD の CLOCK_MONOTONIC は kernel の tick（1 ms）で、user から rdtime も使えない（scounteren 未設定）→ vkmatrix だけでは細かくできず、kern_clock_gettime を hal_rtc_read_counter で補う kernel の変更が要る（Q1 の判断、未実施）。
- d86（P2 6194ba71f: GPU の完了の待ちを waitq で割り込みから起こす、guard 10 ms）: 57/57、HWR 0。timing: interrupts 931〜1694／5 s、timeline waits 4〜19 のうち guard の後に見つけた物 0、guard の後の retire 1〜3 → 待ちは割り込みで起きている。perf（sync・batched）: clear 2250・1850、flat 2350・2250、load-small 1600、texture 8250（batched）。desktop: 部分 frame 5.7 ms、全画面 33〜37 ms（frame の組成が違う窓あり）。vkmatrix の時計は kern_clock_gettime が 1 ms の tick のため、sync の値は改善を示せない（細かい時計は src/kern の変更が要る）。62dac7c92 を main に merge。
- d87・d88（P1 5c80fe4dd・e18aded3b: pixel の出力と blend を 1 命令（0x8）、clamp の命令を省く（0x10）、itrsmp（0x4）、itrsmp が自分の data を待つ（0x20）、既定は off で試しの bit）: どの trial も 102/103（sop-const だけ FAIL、trial 0 でも同じ）、HWR 0。batched: flat-blend 7300 → 2750 us、texture（blend 付き）8300 → 2950 us、texture-nob 3600 → 2900 us、flat 2250（変わらず）。0x10 で範囲外の色の case も PASS。0x20 は正しいが速さは同じ。既定にする commit を P1 に依頼。
- d89（main＋P1 9a1d07516: form 0x3d を既定）: 103/103、HWR 0、見た目同じ（shot-d89.png）。desktop: 部分 frame 4.7 ms、全画面 42〜48 ms。旧の道（-t 0x3c）との比較: flat-blend 7300 → 既定、texture 8250 → 3000 us。
- main ddb093e61（P1 の q1548 を 3f27c0545 まで: 既定 0x3d、試しの切り替えの仕組みと 0x2 を撤去）・45ca4a27c（notes）。d90 = main の kernel: 103/103、HWR 0、desktop が出る。ユーザーの体感の確認へ（ユーザー「測定が終わったら私にもデスクトップを操作させてください。体感を測定します。」）。
- d91（P1 6ce298b0c の cursor の診断の kernel）: cursor の image は 7 回とも全 pixel 0（alpha 0）、dc8200 の config・address・location は正しく更新 → cursor が見えないのは GPU で作る cursor の image が空の為。P1 が原因を調べる。
- d92（d91 の kernel ＋ P1 97f784be0 の Keiland: 内側の variant（角丸の距離の計算を省く、不透明・半透明とも）、KWL PASSES の模型を 512 に）: aat-input で hover と窓の drag。2 窓の全画面の frame の rec submit 平均 33 ms（d90 の操作中は 32〜55 ms、max 85〜154）、hover・drag の区間は submit 平均 13.7 ms（max 22）、compose の frame_ms 33 ms。MODES frame=39: glass 54/1.17 Mpx、shadow 16/0.51 Mpx、image 64/2.16 Mpx。
- cursor が見えない原因（P1 q1550）: transfer の blit の program が sample の fcnorm を 0 で出していて、vkCmdBlitImage が format に依らず全部 0 を書いていた（copy は raw なので正しい）。Keiland の cursor は BGRA の矢印を blit で写すので透明になっていた。blit は以前から 0 を書いていた見込み。直し 15ecf0e4d（blit の時だけ fcnorm 1）。d93: cursor の image の alpha 118（前は 0）、blit-bgra・blit-rgba・cursor-chain PASS、vkmatrix 109/109、HWR 0。
- d95a（P1 fed1a5fa3 の Keiland: 不透明な内側の variant、glass の内側の縁の mix を省く、影の内側を分ける）: aat-input の hover・drag、見た目正常（shot-d95a.png）。P1 の読み（KWL LAT の recorded→queued）: 窓無しの全画面 5〜7 ms（d89 は 9〜20）、1 窓 14〜18 ms（29〜35）、2 窓 17〜21 ms、drag の部分 frame 2〜11 ms、約 2 倍。drag の区間の compose: frame_ms 28.7、acquire 13.9、submit 13 ms。1 本だけ 191 ms の外れ。
- d95（idle の診断の kernel）: idle 60 s の後の job も firmware の中は速い（TA 0.3、3D 1.1 ms）、同期の kick で sent 10 ms。前の「job wait 139 ms」は kick から reap までで reap は次の submit の時 → GPU の時間ではない。139 ms は libvulkan か gpu.c の受け付け（capacity の待ち等）の疑い、優先度を下げる。
- main e6b12bba9（fed1a5fa3 まで）。板は main の kernel と wayland。
- 2026-10-22 ユーザーの体感（main e6b12bba9）:「すばらしい。実用スピードです。」
- d97（P1 39a097e0e: zero copy の flip を条件付きで 1 blank で取られたと数える）: drag の窓で acquire 13.9 → 7.3 ms、frame_ms は 34.6（rec の end 11.45 ms が次の律速）。output lost 0。tearing の目視はユーザー待ち。
- d98（P1 41ed59242: kernel の vk-command の object の検索を 256 bucket の hash に、39a097e0e 込み）: vkmatrix 109/109、HWR 0、output lost 0。rec の end 11.45 → 0.8〜1.5 ms、drag の窓の compose frame_ms 22〜33 ms（acquire 3〜6.5、submit 12〜14）。
- 2026-10-22 ユーザー（d98）「うん、これは素晴らしい。問題ないです。」→ tearing なし、main 8a1c69fd8 に merge。
- d99（P2 72ecf8ec6: POWERVR_SUBMIT_TIMING の既定 0、report は submit の道から外して専用の thread）: idle 60 s の後の時計の frame の rec submit 3 ms（d90 は 139〜140 ms）、操作中の submit の max 21 ms（起動直後の窓は 80）。main に merge。
- d101（P2 2feac9314 の timing 1 の kernel: texture の F16 の sample の道・uniform への mix の書き直し、P1 f67a811c0 の Keiland: drag の部分の書き直し・pacing・glass の map・Home の stage・影の picture、P2 の libvulkan の診断）: vkmatrix 109/109、HWR 0、見た目正常（shot-d101.png）。perf: texture 2950・texture-nob 2900（F16 の道で変わらず）。frame-ops（P1 の試験、操作ごと）: type 44 fps（work p50 8、period p50 18）、output 39、scroll 15 fps（work 6 なのに period 65）、hover 20、drag 43 fps（work p50 11）、home 29（work 17）、wiseview 26（work 24）、launch 28。VULKAN TIMING: submit の大半は execute（kernel の ioctl）平均 7〜8.5 ms・max 20〜27 ms。
- 2026-10-22 ユーザー（d101 の frame-ops の操作をディスプレイで目視して）「アニメーションが素晴らしいです。この調子でチューニングを続けてください。」
- d102（P2 9d7db594a の kernel: clamp・min/max・uniform の積の書き直し、kernel の timing の内訳）: 見た目正常（shot-d102.png）。frame-ops: drag 44 fps（work 11）、home 29（17）、wiseview 25（24）、type 40。kernel の powervr の submit は平均 0.7 ms、libvulkan の execute は 7〜8 ms → ioctl の入口から powervr の submit までの外側が 6〜7 ms（P2）。Home の frame は submit から次の acquire が 18〜21 ms で 2 blank ごと → flip の latch（P1 に dc8200 の調べを許可）。
- d103（P2 b60c38183: command の frame の fence を、その場で実行し終えたら直ちに完了（GPU の仕事の完了を待たない）、uncached の 64-bit の store、P1 95db457c4: drag の部分の書き直し・pacing・glass の map・Home の stage・影の picture・Home の隠れる所を描かない・Wiseview の地を 2 枚に）: vkmatrix 109/109、reset・HWR・fault・device lost 0、見た目正常（shot-d103.png）。VULKAN TIMING の execute 7〜8.5 ms → 1.0〜1.3 ms。frame-ops の work p50/max: drag 8/10（51 fps）、home 6/14（17/31）、wiseview 7/50（24/64）、type 7、launch 8。残りは表示の flip の待ち（next p50 24 ms、P1）。main 76ff675fd（P2）・P1 95db457c4 を merge。
