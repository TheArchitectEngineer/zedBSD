<!-- awesome-plan project=zedbsd record=ws270-p007-linux-plan -->

# Linux（DDK）で何を測るか: Fable 5.1（high）の助言（2026-10-21、一回きり、読むだけ）

ユーザーの依頼「Linuxで何を測定するべきか、Fable 5.1のサブエージェントをワンショットで立てて状況を共有して聞いてみてください。」の結果の要点（Q1 の要約）。

## 問い
- Q1 DDK の pixel 依存の ALU の rate: perf-alu-pix が約 29 ms なら zedBSD と同じ（8 lane/clock）、約 15 ms なら 16 lane 相当、8 ms 以下なら畳まれた（perf-alu-pix-sq で見直す）。
- Q2 blend の費用（zedBSD 4.7 ms）: DDK の texture − texture-nob、flat-blend − flat。
- Q3 texture の内訳: texture-nob − flat（ITER・SMP・WDF）、texture − texture-nob（blend）。
- Q4 clear の床 1219 us が GPU か host か、clear だけの job で DDK が TA を省くか（ftrace の 3D の span、PERF_3D_CYCLE）。
- Q5 小さな job の固定費（DDK 348 対 zedBSD 1000 us）の内訳。
- Q6 job ごとの語の差（ISP_CTL、PBE の source format、pixel task の設定）: KickTA3D2 の TA・3D の command の hexdump。

## 測る物（優先順）
1. perf の一式（-t なし）: clear・immediate・flat・alu-pix・texture・texture-nob・uniform・load-draw・load-small・clear-64・load-64・quarter・draws4。2 回。
2. 足す case: perf-alu-pix-sq（x = x*x*0.999+0.0005 の鎖、64 と 32 段）、perf-flat-blend（push4 を OVER の blend で）、-c で VkPipelineCache を file に（DDK の USC の binary の可能性）。
3. ftrace（KM は compile 済み）: rogue の sched_switch・job_enqueue・firmware_activity、case ごとに trace_marker。
4. PERF_3D_CYCLE・PERF_TA_CYCLE: 描画中に debug_dump を 2 回（電源 ON の時だけ register が出る、devmem は使わない、32 bit で約 7.2 s で wrap）。
5. KM の command: LD_PRELOAD の ioctl の shim で DRM_IOCTL_PVR_SRVKM_CMD の bridge 130・func 12（RGXKICKTA3D2）と func 0（HWRTDATASET）を hexdump。
6. firmware trace（timing を乱すので最後）。
7. power_timing_stats、idle の debug_dump、dmesg。

## 手順（60〜90 分）
起動前に host で vkmatrix（足す case と -c）、shim、全部を回す script を用意。板で: 環境 → perf 一式 ×2 → ftrace → PERF cycle → ioctl の dump → pipeline cache → FW trace → 回収（file の数と size を確かめてから電源を切る）。timing の工程に trace・shim・cache を混ぜない。

## 読み方
- Q1 ≈ 29 ms: rate は同じ → 非 ALU の slot（PACK/UNPACK rpt4、ITER、WDF、SMP、olchk の MOV）と固定費に集中（F16 の pixel 出力＋PBE 変換、itrsmp の既定化、PACK 直書きの再試行）。≈ 15 ms: pixel task の語と binary を比べ設定の差を試す。
- Q2 blend ≈ 0.1〜0.2 ms: zedBSD の UNPACK＋FMAD＋PACK が 20 倍遅い → F16 の出力の設計を最優先。0.5 ms 以上: slot の数を減らすだけ。
- Q4 3D span ≈ 0.9 ms・TA 無し: clear だけの job の TA を zedBSD でも省く。
- Q5 外側の時間の差: 完了の通知（割り込み→wake）と kick の道を測る。
- Q6 語の差があればそれが最初の試し、無ければ compiler の道に絞る。

## 測らなくてよい物
LINEAR 対 OPTIMAL、perf-alu・alu4・mul4・rcp、perf-load、perf-read、tri・stack4、SLC・CLK・BYPASS の register、HWPerf、vulkaninfo、Keiland の desktop、kernel module の再 build、GPU が off の時の register 読み。

## license の注意（Fable の指摘）
pipeline cache の binary と KM の command は proprietary の出力。git に入れない（build/uat の下）、encoding を repo に写さない、読むのは事実（命令数・F16 の有無・PACK の形・語の値）だけ。
