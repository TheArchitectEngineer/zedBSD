<!-- awesome-plan project=zedbsd record=ws237-p001-design -->

# WS237 の設計: Gen9・Gen9.5 の display controller（段 (1) の詳細と段 (2)〜(4) の見通し）

2026-10-14 P2。親: [p001](phase.md)・[WS237](../ws.md)。code は書かない（p002 以降）。

## 0. 出典とライセンス

| 出典 | 場所 | ライセンス・確かめ |
| --- | --- | --- |
| Linux v6.8.12 `drivers/gpu/drm/i915/` | WS229 p001 が取得・検証した展開（`/home/awe/zedBSD-worktrees/p1/build/ws229-p001/linux/linux-6.8.12/`、tarball の sha256 は `sha256sums.asc` の行と一致） | 使う file は全部 MIT（2026-10-14 P2 が先頭の行で確認）: `i915_reg.h`（"Permission is hereby granted"、sha256 `efcb6385…0e626`）、`display/intel_psr_regs.h`（SPDX MIT、`0a207944…adf3b`）、`display/intel_dpll_mgr.c`（`6b339fd5…02bb7`）、`display/intel_display.c`（`e535b025…8dfb4`）、`display/intel_fb.c`（SPDX MIT、`9255f705…361e4`）、`display/intel_display_power_map.c`（SPDX MIT、`442583aa…e47b2`）、`display/intel_backlight_regs.h`（SPDX MIT、`fe411d1e…98b7`）、`display/skl_universal_plane.c`（SPDX MIT、`0e7579f7…442da`）。GPL の file は使わない |
| SKL PRM Vol.12 Display（IHD-OS-SKL-Vol 12-05.16） | 同じ展開の `prm/`（PDF と pdftotext） | Intel の公開文書。double buffer と arming（p.168 付近: PLANE_SURF の書き込みが PLANE_CTL・STRIDE・POS・SIZE・OFFSET などを arm し、vblank の始めに反映、PLANE_SURFLIVE は今 scan out している番地） |

扱いは今の tree の規則（[i915-rebuild-rules](../../ws031/i915-rebuild-rules.md) §1）と同じ: register の番地・bit・表の**値だけ**を新しい header に出典（file・行・sha256）付きで転記し、logic は Zlib で書く。

## 1. SKL・KBL・CFL（display 9）の display の要点

SV7 は Kaby Lake R（UHD 620 `8086:5917`、Gen9.5、display 9）、PCH は Sunrise Point-LP（SATA `8086:9d03`）。SKL・KBL・CFL・CML の display は同じ「display 9」で、差は PCH（SPT・KBP・CNP・CMP）と DMC の版だけ。

| 部分 | display 9 の事実（値は §6） | 段 (1) で触るか |
| --- | --- | --- |
| power well | `HSW_PWR_WELL_CTL2` 0x45404 の STATE bit。PW1（bit 28）に pipe A・transcoder EDP・DDI A、PW2（bit 30）に pipe B・C、transcoder A・B・C、DDI B〜E（`intel_display_power_map.c` の skl の表） | 読むだけ |
| DC state | `DC_STATE_EN` 0x45504 の bit 1:0（DC5・DC6）。DMC を load した OS が立てる。GOP は立てない | 読むだけ。立っていれば段 (1) を使わない |
| CDCLK | `CDCLK_CTL` 0x46000、`LCPLL1_CTL` 0x46010（DPLL0 が CDCLK の源） | 読んで log だけ |
| DPLL | DPLL0〜3。`DPLL_CTRL1` 0x6c058 の id ごとの 6 bit（override・link rate 3:1・SSC・HDMI mode）、`DPLL_CTRL2` 0x6c05c の port ごとの clock の選択。eDP の DDI A は普通 DPLL0 | 読んで refresh の計算に使う |
| DDI と transcoder | `TRANS_DDI_FUNC_CTL_EDP` 0x6f400（bit 31 enable、30:28 port、26:24 mode、14:12 eDP の入力の pipe: 0 と 4 = A、5 = B、6 = C）、`TRANSCONF` EDP 0x7f008（bit 31 enable、30 state）。transcoder A・B・C は 0x60400・0x61400・0x62400 と 0x70008・0x71008・0x72008、transcoder x は pipe x を受ける | 読むだけ |
| timing | transcoder の HTOTAL 0x6f000・VTOTAL 0x6f00c（EDP、A は 0x60000・0x6000c）: bit 28:16 = total − 1、12:0 = active − 1。DP の LINK_M1 0x6f040・LINK_N1 0x6f044（23:0）。pixel clock（kHz）= LINK_M × port clock ÷ LINK_N、port clock = DPLL の link rate の表 × 2（`skl_ddi_lcpll_get_freq`、`intel_dotclock_calculate`） | 読んで refresh を出す |
| pipe | `PIPESRC` 0x6001c（+0x1000/pipe）: 28:16 = 幅 − 1、11:0 = 高さ − 1。frame counter `PIPE_FRMCOUNT` 0x70040（+0x1000/pipe） | 読むだけ |
| plane 1（primary） | `PLANE_CTL_1` 0x70180（+0x1000/pipe）: 31 enable、27:24 format（4 = XRGB8888）、20 ORDER_RGBX、12:10 tiling（0 linear、1 X、4 Y、5 Yf）、9 async flip、1:0 rotation。`PLANE_STRIDE` 0x70188（linear は 64 byte 単位）、`PLANE_POS` 0x7018c、`PLANE_SIZE` 0x70190（(高さ − 1) << 16 \| (幅 − 1)）、`PLANE_SURF` 0x7019c（31:12 GGTT の番地、書くと arm）、`PLANE_OFFSET` 0x701a4、`PLANE_SURFLIVE` 0x701ac（今の番地） | **SURF だけを書く** |
| scaler | `PS_CTRL` pipe A 0x68180・0x68280（bit 31 enable）。GOP の解像度が panel より小さい時に使われ得る | 読んで log。変えない |
| watermark・DDB | `PLANE_WM_1_*`・`PLANE_BUF_CFG_1` 0x7027c。format・tiling・幅・pipe の clock が同じなら firmware の値がそのまま正しい | 触らない |
| PSR | `EDP_PSR_CTL`（transcoder EDP は 0x6f800、bit 31 enable） | 読む。立っていれば段 (1) を使わない |
| 割り込み | `GEN8_MASTER_IRQ` 0x44200 の bit 16+pipe（DE_PIPE、状態の要約）。`GEN8_DE_PIPE_ISR/IMR/IIR/IER(pipe)` 0x44400/04/08/0c + 0x10×pipe。bit 0 VBLANK、bit 3 plane 1 の FLIP_DONE | pipe の FLIP_DONE だけを有効にする |
| backlight（段 2） | SPT・KBP: `BLC_PWM_PCH_CTL1` 0xc8250（31 enable）・`CTL2` 0xc8254（31:16 周期、15:0 duty）、`SOUTH_CHICKEN1` 0xc2000 の bit 0（PWM の粒度）。CNP（CFL）は同じ番地で clock の算出が違う | 触らない（段 2） |

## 2. 今の zedBSD の物との関係

### 2.1 Gen12 の display（`src/drivers/gpu/i915/display/`、約 100k 行）

Gen12 は N0（読み取りの precheck）→ N1（firmware の pipe の takeover）→ Linux 由来の modeset（`modeset.c`・`pipe.c`・`power.c`、TGL・ADL-P の power well・combo PHY・DKL・TC・DMC・watermark）→ present（worker の「display window」の中で GPU が blit、vblank の割り込みで flip、FIFO）。版の前提（TGL/XE_LPD の power well の表、GEN11 の display の割り込みの要約、combo PHY）が深く、Gen9 には使えない部分が大半。

段 (1) で**そのまま使う**物:

| 物 | file | Gen9 で使える理由 |
| --- | --- | --- |
| scanout の buffer（create・pin・publish・unpin・destroy） | `display/scanout.c` | linear XRGB8888、stride 64 byte、surface の alignment 256 KiB、VT-d の guard 168 page は display 9 の Linux の値と同じ（`intel_linear_alignment()` は display ≥ 9 で 256 KiB、`VTD_GUARD` 168 page） |
| GGTT の display window | `ggtt.c` `drv_i915_gt_display_window_init`・`_bind` | GT window の真下。N0-lite の重なりの検査は GT window ＋ display window の最大（1 GiB）を予約として数えている（`device.c` の `reserved`）ので、firmware の framebuffer（stolen、GGTT の低い番地）と重ならない。PTE は bit 0 present と DMA 番地で Gen8 の形と同じ。GGTT の更新ごとの GFX_FLSH_CNTL は WS229 p002（937f8c714）で Gen9 に対応済み |
| PPGTT への uncached の map と GPU の blit | `ppgtt.c` `drv_i915_ppgtt_insert_uncached`、`render/blit.c` `drv_i915_gfx_rect` | blit の MOCS は `drv_i915_gfx_mocs_uncached(graphics_ver)`（WS229 p004）。LLC と display は coherent でないので、PTE と MOCS の両方を uncached にして書き込みを memory に届かせる（Linux も scanout を `I915_CACHE_NONE` にする） |
| 待ちの completion | `sync.h` `drv_i915_complete`・`drv_i915_wait_for_completion` | 割り込みから complete できる |
| lease（`rd`）と display の op の枠 | `display/bootfb.c`・`present.c` の lease の mutex | bootfb がすでに Gen9 の node で使っている |

### 2.2 WS229 の N0-lite と bootfb

- N0-lite（`takeover.c` `drv_i915_n0_lite_gen9`、device.c の `i915_start_registers` の後）: PWR_WELL_CTL2、TRANSCONF_EDP、VT-d、各 powered pipe の PLANE_CTL・STRIDE・SIZE・SURF を読んで log し、driver の GGTT と重なれば EBUSY、VT-d が働いていれば EIO。結果に関わらず `display->absent = 1`。**書き込みは無い**。
- 割り込み: Gen9 の display の源は interim の reset（`device.c` `i915_interim_display_irq_reset`）で PW の上がっている pipe・port・misc・PSR・SDE を mask・disable・clear している。`irq.c` の Gen9 の handler は display の bit を数えるだけで display の op を呼ばない。
- bootfb（BUG-310、9cdd5689c、SV7 の UAT で OK）: `display->absent` の Gen9 の node に display を 1 つ bind（mode は loader の framebuffer の大きさ @60 Hz、BGRA/RGBA、1 plane、lease 1 つ）。present は GPU の copy（staging の RAM）→ clflush → CPU が行ごとに framebuffer へ。claim で `kern_text_suspend()` と黒の消去、release で `kern_text_resume()`。vsync 無し（tearing あり）、1 frame ごとに全画面の CPU の copy。

## 3. 段 (1) の設計: readout と同じ mode のままの primary plane の flip

### 3.1 形

**bootfb の display の上に「flip」の present を足す**。query・mode・claim・release・lease・session close は bootfb の物のまま。flip が使えない・失敗した時は今の copy の道（bootfb）にそのまま戻る。drop は「既定の語を copy のままにする」か、bootfb.c の flip の分岐の数行を外すだけ。

```
bind:   bootfb の準備（今と同じ） ─┬─ i915.gen9.display=copy（既定）→ copy の道（今と同じ）
                                    └─ =flip → readout → 使える？ ─ no → log の理由 → copy の道
                                                         └ yes → 2 枚の scanout を GGTT に pin → flip の道
present（flip の道）:
   1. 前の flip の latch を待つ（SURFLIVE == 前に書いた SURF、上限 100 ms）
   2. 表示されていない方の buffer へ GPU が blit（同期、uncached）
   3. PLANE_SURF に その buffer の GGTT の番地を書く（arm。次の vblank の始めで反映）
   4. sequence を進めて返る（FIFO: 次の present の 1 で待つ）
   失敗（latch の timeout、blit の失敗）→ firmware の SURF を書き戻して copy の道へ切り替え（この boot の間ずっと）、この frame は copy で出す
wait:   最新の flip の latch を待つ（割り込みで起き、SURFLIVE で確かめる）
release: firmware の SURF を書き戻し → latch を待つ → text console を戻す（今と同じ）
```

### 3.2 readout（`display/gen9-flip.c` の新しい関数、生の値の読みと解読を分ける）

読み（書き込み無し、PW の STATE で gate。PW1 が落ちていれば何も読まない）:

1. PWR_WELL_CTL2、DC_STATE_EN、CDCLK_CTL、DPLL_CTRL1・CTRL2。
2. transcoder EDP（PW1）: TRANS_DDI_FUNC_CTL_EDP、TRANSCONF_EDP、HTOTAL・VTOTAL、LINK_M1・LINK_N1、EDP_PSR_CTL。
3. PW2 が上がっていれば transcoder A・B・C の TRANS_DDI_FUNC_CTL と TRANSCONF。
4. 灯っている pipe ごとに PIPESRC、PLANE_CTL・STRIDE・POS・SIZE・SURF・OFFSET・SURFLIVE、PS_CTRL（2 個）、PIPE_FRMCOUNT。

解読（`drv_i915_gen9_readout_decode()`、host で試す、mmio に触れない）の結果と「使える」の条件（どれか 1 つでも外れたら理由を名指して copy の道）:

| 条件 | 理由の語（log） |
| --- | --- |
| PW1 が上がっている | `pw1-off` |
| DC5・DC6 が立っていない | `dc-state` |
| 灯っている pipe（transcoder が enable で state 1、それが送る pipe の plane 1 が enable）がちょうど 1 つ | `no-lit-pipe`・`several-lit-pipes` |
| plane 1 が XRGB8888、linear、rotation 0、async flip 無し、OFFSET 0（POS は見ない: 大きさと番地が同じなら plane の置き場所は変わらない。p002 で決めた） | `format`・`tiled`・`rotated`・`async`・`offset` |
| FBC（`ILK_DPFC_CONTROL` 0x43208 の bit 31）が無効（p002 で足した: 圧縮の cache が firmware の surface を持つと、切り替えに invalidate が要る） | `fbc` |
| SURF が 256 KiB の倍数、SURFLIVE == SURF（firmware の flip が途中でない） | `surf-align`・`surf-pending` |
| 幅・高さが 1〜8192、STRIDE × 64 ≥ 幅 × 4 | `size`・`stride` |
| eDP なら PSR が無効 | `psr` |

使える時の結果: pipe、transcoder、幅・高さ（PLANE_SIZE）、firmware の stride と SURF、byte の順（ORDER_RGBX → blit の先の形式 R8G8B8A8、無ければ B8G8R8A8）、refresh（mHz、DP の M/N と DPLL の link rate と HTOTAL・VTOTAL から。HDMI mode の DPLL・link N が 0・計算の外れは 60000 に戻して `refresh=assumed` と log）、scaler の有無（log だけ）。

**loader の framebuffer との関係**: 素の起動では loader の framebuffer は GOP の物で、`fb_physical − GMADR == SURF`（aperture の中の stolen）。passthrough の guest では loader の framebuffer は QEMU の VGA で、plane は SV7 の panel（host の GOP の物）。flip の道は **plane の readout の大きさ**を display の mode にし、loader の framebuffer と一致するかを log に書く（`fb=same`・`fb=other`）。一致しなくても使う（passthrough でも flip を試せるように）。copy の道へ戻った時は loader の framebuffer に描く（大きさが違えば bootfb の clip のとおり左上）。

### 3.3 flip

- buffer: `drv_i915_scanout_create(&gt->mem, w, h, XRGB8888, LINEAR)` を 2 枚、display window（`drv_i915_gt_display_window_init`、2 枚と guard と alignment が入る大きさ、32 page の倍数）に `drv_i915_scanout_pin`。pitch が firmware の stride と違えば PLANE_STRIDE も flip の前に書く（SURF で一緒に arm される。普通は 1920×4 = 7680 = 120×64 で同じ）。
- claim: 2 枚を黒で埋めて publish（CPU の memset と clflush）、`kern_text_suspend()`。最初の present で buffer 0 へ flip する（その前は firmware の画面のまま）。
- present: 3.1 の 1〜4。blit は bootfb の staging の copy と同じ `drv_i915_gfx_rect`（session の render context、終わるまで待つ）。PPGTT への map は session ごとに 1 回（bootfb の staging と同じ「1 つの address space だけ」の規則、release で clear）。frame が大きければ左上で clip、小さければ残りは claim の黒。
- latch の確かめ: `PLANE_SURFLIVE == armed`。待ちは completion を 1 tick（amd64 は 1 ms）の deadline で寝て、起きるたびに SURFLIVE を読む。FLIP_DONE の割り込みが来れば即座に起きる。上限 100 ms（60 Hz の 6 frame）で timeout → fallback。
- release・session close・fini: SURF を firmware の値に書き戻し → latch を待つ（timeout なら log）→ `kern_text_resume()`。buffer は fini まで pin のまま（次の lease で使い回す）。firmware の SURF に戻せなかった buffer は `drv_i915_scanout_abandon` の規則で解放しない。

### 3.4 割り込み（FLIP_DONE と vblank）

- 有効化（flip の道に入る時、claim）: `DE_PIPE_IIR(pipe)` を clear → `IER |= FLIP_DONE(plane 1)` → `IMR &= ~FLIP_DONE`。release・fini で逆（IMR を mask、IER を 0、IIR を clear）。他の display の源は interim の reset のまま。
- handler: `irq.c` の Gen9 の分岐で display の bit（`master_ctl & ~GT_BITS`）があれば `display_ops->handle()` を呼ぶ（今は数えるだけ）。`device.c` の interim の handle は Gen9 の時 `drv_i915_gen9_flip_irq(master_ctl)` を呼ぶ。そこで `master_ctl` の DE_PIPE(pipe) の bit を見て、IIR を読み・書き戻して ack、FLIP_DONE なら数えて complete する。
- 割り込みが来なくても（MSI の不具合など）1 ms の poll で latch は確かめられるので flip は動く。数（flip の数・割り込みの数・timeout の数・latch までの平均 μs）を 600 frame ごとに log する。割り込みが 0 なら原因の切り分けの材料になる。
- vblank の割り込み（bit 0）は段 (1) では使わない（flip の完了だけで足りる。refresh の時刻は latch の tick）。

### 3.5 boot の語と既定

- 新しい key `i915.gen9.display=flip|copy`（`include/kern/boot.h` の enum と `src/kern/boot.c` の名前・語の検査。HAL の API ではない）。`i915.gen9=off` なら i915 自体が Gen9 を取らないので無関係。
- 既定は `display/gen9-flip.h` の 1 つの定数 `I915_GEN9_DISPLAY_FLIP_DEFAULT`（0 = copy）。実機で通ったら Q1 がここを 1 にする。1 にした後は `i915.gen9.display=copy` で bootfb に戻せる。
- 読むのは bind の時（publish、kernel の boot の parse の後）なので `kern_boot_parameters_current()` で読める（`i915.gen9=` と違い loader の line を直に読む必要は無い）。

### 3.6 log の印（実機・T1 の判定に使う）

| 時 | 行 |
| --- | --- |
| bind | `i915: gen9 display: readout pipe A transcoder EDP 1920x1200 stride 7680 XRGB8888 linear surf 0x… live 0x… refresh 59950 mHz (dp m/n) scaler off psr off dc off fb=same` |
| 使えない | `i915: gen9 display: flip not used (<理由の語>); the copy display is used` |
| 語が copy | `i915: gen9 display: copy (i915.gen9.display=copy or the default)` |
| flip の準備 | `i915: gen9 display: flip on pipe A, buffers at 0x… and 0x… (GGTT), the firmware's surface 0x… is restored on release` |
| 最初の frame | `i915: gen9 display: frame 1 WxH gpu_copy_us=… latched_us=… irq=…` |
| 600 frame ごと | `i915: gen9 display: N flips, avg gpu_copy_us=… avg latch_wait_us=…, flip-done irqs …, timeouts 0` |
| fallback | `i915: gen9 display: flip to 0x… did not latch in 100 ms (live 0x…); back to the firmware's surface and the copy display` |
| release | `i915: gen9 display: lease N released after … frame(s); the firmware's surface is back (latched)` |

## 4. 試験の道と切り分け

| 道 | 何が分かるか | 限界 |
| --- | --- | --- |
| host | readout の解読（SV7 の形の register の組、negative の 12 例）、refresh の計算、plane・割り込みの register の番地と値、display window の大きさ | hardware には触れない |
| build | amd64 の CI の config で warning 0 | — |
| QEMU（T1、boot-test） | i915 の無い機械の回帰（bootfb も flip も bind されない） | Intel の display は無い |
| SV7 の passthrough（T1、`I915_MACHINE=sv7`、`i915.gen9.display=flip` を `I915_BOOT_EXTRA` に） | (a) readout の行で guest から pipe が見えるか。SV7 の host は i915 を blacklist しているので、host の GOP が灯した eDP が vfio の後も灯ったまま guest に渡る可能性がある。灯っていれば flip の道が guest の GGTT の buffer で動き、log（flip の数・latch・割り込み・timeout 0）で判定できる（絵は QEMU の画面ではなく SV7 の panel に出るので、QMP の PNG では見えない）。(b) 灯っていなければ `flip not used (no-lit-pipe)` と copy の道（今と同じ、QMP の PNG に desktop）で fallback を確かめる | 絵の確かめはできない（(a) の時は SV7 の panel を人が見るしかない）。device の reset で plane が消えている可能性もある |
| SV7 の素の起動（ユーザー、USB） | flip の道の本当の受け入れ: panel に desktop、tearing が無い、log の行 | 人の目と写真 |
| 5330（Gen12）の回帰（T1、軽く） | Gen12 の道が変わらないこと（`i915: gen9 display` の行が出ない） | — |

素の起動の確かめの手順は [phase.md](phase.md) の「ユーザーの確かめ」と p002 の phase.md に書く。

## 5. 段 (2)〜(4) の見通し

| 段 | 内容 | 主な仕事 | 見積もり（LW、±50 %） |
| --- | --- | --- | --- |
| (1) p002 | readout・flip・FLIP_DONE の割り込み・fallback・boot の語 | §3。新しい `display/gen9-flip.[ch]`・`intel/display-regs-gen9.h`、bootfb.c の分岐、irq.c・device.c の各数行、boot の key、host の試験 | 1.5 |
| (2) p003 | panel の backlight | SPT・KBP の `BLC_PWM_PCH_CTL1/2`（CNP・CMP は clock の算出が違う）、VBT の backlight の最小値・PWM の周波数、Keiland の Display の拡張（`GPU_DISPLAY_POWER`・明るさの op）への接続、eDP の DPCD の AUX の backlight の panel は対象外（SV7 は PWM の見込み、readout で確かめる） | 1.0 |
| (3) p004 | mode set と外部の DP・HDMI | PW2 の on・off、DPLL1〜3 の計算（DP の固定の link rate、HDMI の WRPLL の divider）、DDI の buffer の translation の表（SKL・KBL・CFL の U/Y/H の別）、DP の link training（今の `dp.c` の流用の可否、Gen12 の combo PHY と違う）、HDMI、SPT・CNP の SDE の hotplug、watermark と DDB の計算（`watermark.c` の Gen12 の物の Gen9 の分岐）、CDCLK の変更、DMC（`skl_dmc_ver1_27`・`kbl_dmc_ver1_04`）と DC state。Keiland の Display の拡張の hotplug の経路。guardrail の「GOP の出力先以外に自分で scanout を始めない」により、外部の出力は Keiland の明示の指示の時だけ | 6.0 |
| (4) p005 | suspend・resume | 今の Gen9 は suspend を EBUSY で拒む（WS229 U2）。display の状態の保存と戻し（PW・DPLL・DDI・pipe・plane）、段 (3) の modeset を resume で使う | 1.5 |
| p006 | 規約の全文 | conformance-reviewer | 0.5 |

段 (1) は 1.5 LW で、2026-10-16 夕のベータ2 に間に合う見込み。段 (2) 以降は p002 の実機の結果を見てから（Q1 の指示）。

## 6. 段 (1) で転記する値（出典: Linux 6.8.12 の `i915_reg.h` ほか、行は sha256 の file の物）

| 名前（zedBSD の header で付け直す） | 値 | Linux の場所 |
| --- | --- | --- |
| PWR_WELL_CTL2・PW1 STATE・PW2 STATE | 0x45404、bit 28、bit 30 | `i915_reg.h:5451,5455`（`HSW_PWR_WELL_CTL_STATE(idx)` = 1 << (2×idx)、SKL_PW_CTL_IDX_PW_1 14・PW_2 15、`intel_display_power_map.c:361,389`） |
| DC_STATE_EN、DC5・DC6 | 0x45504、bit 1:0 の 1・2 | `i915_reg.h:6188-6196` |
| CDCLK_CTL・LCPLL1_CTL | 0x46000・0x46010 | `i915_reg.h:5886,5917` |
| DPLL_CTRL1（id ×6: 0 override、3:1 link rate、4 SSC、5 HDMI）、link rate の表（0 2700、1 1350、2 810、3 1620、4 1080、5 2160）、DPLL_CTRL2（port ×3: 0 override、2:1 select、15+port clock off） | 0x6c058・0x6c05c | `i915_reg.h:5922-5942`、`intel_dpll_mgr.c:1824-1856`（port clock = 表の値 ×2 の kHz） |
| TRANS_DDI_FUNC_CTL EDP・A・B・C、enable 31、port 30:28、mode 26:24、eDP の入力 14:12（0・4 A、5 B、6 C） | 0x6f400・0x60400・0x61400・0x62400 | `i915_reg.h:5574-5611` |
| TRANSCONF EDP・A（enable 31、state 30） | 0x7f008・0x70008（+0x1000/pipe） | `i915_reg.h:2535`、`intel_display_device.c` の PIPE_EDP_OFFSET 0x7f000 |
| TRANS_HTOTAL・VTOTAL（A 0x60000・0x6000c、EDP 0x6f000・0x6f00c） | total−1 は 28:16、active−1 は 12:0 | `i915_reg.h:1928,1943` |
| LINK_M1・LINK_N1（A 0x60040・0x60044、EDP 0x6f040・0x6f044） | 23:0 | `i915_reg.h:3975-3976` |
| PIPESRC（A 0x6001c） | 幅−1 28:16、高さ−1 11:0 | `i915_reg.h:1959` |
| PIPE_FRMCOUNT（A 0x70040） | — | `i915_reg.h:3025` |
| PLANE_CTL・STRIDE・POS・SIZE・SURF・OFFSET・SURFLIVE（plane 1 pipe A） | 0x70180・0x70188・0x7018c・0x70190・0x7019c・0x701a4・0x701ac（+0x1000/pipe） | `i915_reg.h:3584-3690` |
| PLANE_CTL の bit | enable 31、format 27:24（XRGB8888 4）、ORDER_RGBX 20、tiling 12:10（linear 0）、async 9、rotation 1:0 | `i915_reg.h:3587-3649` |
| PS_CTRL（pipe A の scaler 1・2）、enable 31 | 0x68180・0x68280 | `i915_reg.h:4038-4043` |
| EDP_PSR_CTL（transcoder EDP）、enable 31 | 0x6f800 | `intel_psr_regs.h:24-27` |
| GEN8_MASTER_IRQ の DE_PIPE_A | bit 16（+pipe） | `i915_reg.h:4279,4287` |
| GEN8_DE_PIPE_ISR・IMR・IIR・IER(pipe) | 0x44400・0x44404・0x44408・0x4440c（+0x10×pipe） | `i915_reg.h:4311-4314` |
| plane 1 の FLIP_DONE | bit 3 | `i915_reg.h:4340` |
| linear の surface の alignment | 256 KiB（display ≥ 9） | `intel_fb.c:790-793`（今の `scanout.c` の値と同じ） |
