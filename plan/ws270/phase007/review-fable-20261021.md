<!-- awesome-plan project=zedbsd record=ws270-p007-review-fable -->

# brief-20261021 への Fable 5.1（high）のレビュー（2026-10-21、一回きり、読むだけ）

ユーザーの依頼「PowerVRドライバの最適化について、わかっていることを文書にまとめて、Fable 5.1 Highのサブエージェントに解析してもらい、何か気づきを得られるか、また、検討の進め方など、助言をもらってください。」の結果の要点（Q1 の要約、原文は Q1 のセッションの記録）。

## 気づき
1. DDK の alu・rcp・mul4 は畳んだのではなく hoist（入力が push だけ）→ DDK の pixel あたりの ALU の速さは未測定。
2. DDK の clear 1219 us ≈ 348（固定費）＋ 2073600/4/594 MHz（ISP・PBE の 4 px/clock の床）。flat・quarter・immediate が 1.3 ms なのは shader がこの床の下に隠れるため。zedBSD の slot × 約 0.45 ms の model は自己整合（alu 0.44、alu4 0.52 ms/group）。
3. clear の差 1 ms は GPU 側か host 側か未分離 → POWERVR_JOB_TRACE を perf-clear に当てる。
4. vkmatrix の時計は 1 ms の tick（main.c:4361）、同期の wait も 1 tick の poll（render-queue.c:1243-1260）→ 0.5 ms 未満の差（APM・SLC・data cached）は判定できていない。hal_rtc の counter を userland に。
5. DDK の perf-load 2〜4 us は draw の無い LOAD を DDK が省いた → draw 付きで作り直す。
6. 8 lane/clock か 16 lane/clock かが戦略を決める（Mesa の bxe-4-32: usc_itr_parallel_instances 16、max_instances_per_pds_task 32、has_s8xe）。
7. job ごとの register は比べていない。ISP_CTL は DDK 0x00023000、zedBSD 0x80033000（bit 31 skip_init_hdrs、bit 16 sample_pos、render-command.c:45-48・307）。

## 候補（順位）
- A（最有力）: pixel の出力の道の slot（shader-backend.c:2469-2515、PACK rpt4・UNPACK rpt4）。手: TPU に F16 で返させる（smp の f16 flag）→ PBE が U8 に変換（pack 0 slot）、has_usc_itrsmp_enhanced で ITER＋WDF＋SMP を 1 命令、PACK を olchk 付きで直に pixel output へ、uniform の hoist（q1546）。
- B: hardware の rate（8 か 16 か）。DDK で pixel 依存の FMAD の鎖（perf-alu-pix）を測る。半分なら KM の bridge の 3D・TA の command を dump して zedBSD と語ごとに比べる（値だけ）。
- C: job ごとの host 側 約 1 ms（部分 frame 5.8 ms の大半は host 側の見込み）。JOB_TRACE と細かい時計で分ける。
- D: texture の TPU state、ISP_CTL の bit、DOUTU の sample rate など（低い）。

## 進め方
1. 試しを compile 時の define から runtime の switch（boot 引数か sysctl）に、vkmatrix に --trial、時計を細かく → 1 回の起動で全組。
2. 順: JOB_TRACE で host と GPU を分ける → Linux を 1 回起動して perf-alu-pix・texture-nob と KM の command の dump → A か B へ → C。
3. 目標: 部分 frame ≤16 ms は達成（5.8 ms）。全画面は hoist と出力の道で 25〜35% 減 → 40〜50 ms が現実的。DDK 並みは B で rate の差が見えた時だけ。

## やめてよい調べ
SLC・FBCDC・memory read・clock・APM・dust・精度・temps・dual-phase、DDK の alu・rcp・mul4・perf-load の比較、Mesa pco の group 数の目標、三角形数・draw 数・parameter buffer・TA。
