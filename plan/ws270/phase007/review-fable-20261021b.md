<!-- awesome-plan project=zedbsd record=ws270-p007-review-fable-b -->

# Linux の 2 回目の後の Fable 5.1（high）の助言（2026-10-21 夜、一回きり、読むだけ）

ユーザーの依頼「進める前に、これまでの経緯と計測結果をFable 5.1 Highのワンショットのサブエージェントに伝えて、助言を得てください。現状で疑うべき理由や、それを切り分けるための進め方を聞いてみてください。そのほかに根本的に気になることなど、なんでも助言してもらえるように聞いてみてください。」の結果の要点（Q1 の要約）。

## 疑うべき理由
- (a) DDK の OPTIMAL の数は FBCDC 圧縮あり（shim の PBE word0 bit 3 = pbestate.xml の compression）。単色の clear は圧縮で書き込みがほぼ消える。LINEAR 同士なら clear 1659 対 2500、flat 1896〜1999 対 2400、texture 2520〜2543 対 8250〜8750。ISP・PBE の pixel の床が 2 倍遅い証拠は無い。
- (b) 1 ms の tick: powervr_session_point_wait（render-queue.c:1243〜1260）と vk-queue.c の order_after・settle は sched_sleep(now+1) の poll。完了の割り込みは来ているのに次の tick まで気付かない → sync の計測は 1 ms に量子化、clear-64 の 1000 us は GPU 約 350 us＋poll。d80〜d82 の「効果なし」は未判定。desktop でも transfer と render の間に tick 待ちが入りうる。
- (c) job の語は DDK と一致（ISP_CTL 0x80033000、USC_PIXEL_OUTPUT_CTRL 0x1ff8、PBE word1、TA の ppp_ctrl・te_psg、HWRTDataSet の macrotile・rgn_header）。構造の差: DDK は 1 job = 1 KCCB command（KickTA3D2）、zedBSD は combined と fragment の 2 command（render-queue.c:640〜690）。
- (d) firmware の trace は raw の unit で比べる（P1 の 0.65 us/unit は矛盾）。DDK も毎 frame dust を off・on。
- (e) DDK の blend は命令を足さない（fragment の code は flat 24 B・flat-blend 24 B、texture 40 B・texture-nob 40 B）。texture は itrsmp で始まる（0x4 の方向は正しい）。bxe-4-32 は has_usc_f16sop_u8、F16SOP（encoding は Mesa で TODO）1 命令で pack・unpack ごと置き換えている可能性が高い。Mesa を手本にする限り zedBSD の blend の約 10 slot は減らない。zedBSD の内訳 約 8 slot ≈ 3.6 ms と測った 4.65 ms の差 1 ms は olchk の読みの待ちの疑い。
- (f) ALU の rate の一致は依存の鎖だけで確認。独立の命令では DDK が速い疑い（uniform の case）。
- (g) desktop の数は組成を見る（d85 の全画面 frame に全画面の copy 1 回、KWL PASSES の jobs は 64 draw の古い推定）。
- (h) 全画面 frame の ALU の床は 25〜35 ms の見積、driver だけでは 50 → 30〜35 ms が上限。

## 切り分けの順
0. 板なし: wait を割り込み駆動に（service thread が retire したら waitq を wake）、vkmatrix の時計を細かく、kernel の switch を runtime に。
1. 1 起動目: 全 perf を sync・batched、OPTIMAL・LINEAR で取り直し LINEAR 同士の表。clear-64 が 1000 → 400〜500 なら (b) 確定。
2. 固定費: zedBSD の clear-64 の firmware trace を raw unit で DDK と並べる。2 KCCB → 1 と data cached を再判定。
3. blend: flat-blend、PIXEL_LOAD だけ、math だけ、olchk の MOV だけの 4 変種で slot の内訳。F16SOP はユーザーの判断。代替は SMP の f16 返し＋f16f16 の pack。
4. desktop: KWL MODES の px × group 数で ALU の床、全画面 copy の正体、KWL PASSES の推定を直す。

## 根本的に気になること
- 目標: Keiland の frame は ALU 律速で rate は DDK 並み。DDK 並みでも全画面 2 倍止まり、その先は Keiland の shader の group 数。Pi 4 の V3D とは ALU の形が違う。
- 1 ms の時計で 0.5 ms 以下を多数判定した。
- Mesa pco を上限にしない。0x2 の device lost は host simulator で 1 回だけ見る。SD の read 失敗が往復の律速。DDK の数は OPTIMAL/LINEAR・sync/batched を併記。

## やめてよい事
DDK の OPTIMAL と zedBSD の比較、alu・rcp・mul4、register・HWRT・ISP_CTL の bit 追い、dust・APM、lp、依存鎖への pairing、Mesa の group 数、tri・stack・draws4、FBCDC の導入。
