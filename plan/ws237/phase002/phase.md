<!-- awesome-plan project=zedbsd record=ws237-p002 -->
# ws237-p002: 段 (1) readout と primary plane の flip（Gen9・Gen9.5）

Status: cleared（2026-10-15 ユーザーの実機: i5 の第 7・第 8 世代で i915 の display controller OK・Keiland 使用可、main の既定 FULL の image）
Disposition: normal
Parent: [WS237](../ws.md)
Queue: Q1 の起動の指示（2026-10-14、P2、worktree `/home/awe/zedBSD-worktrees/p2`、branch `agent/p2`）
依存: [p001](../phase001/phase.md)（設計 [design.md](../phase001/design.md) §3）

## 範囲

design.md §3: firmware の display の readout（書き込み無し）、bootfb の上の flip の present（GGTT の 2 枚の buffer、GPU の blit、`PLANE_SURF` の書き換え、`PLANE_SURFLIVE` の latch 待ち）、plane 1 の FLIP_DONE の割り込み、失敗したら firmware の surface に戻して今の copy の道、boot の語 `i915.gen9.display=flip|copy`（既定 copy）、log の印。

## 実装（base b810f0022 → main 74265812f に ff、commit a5613f4b2）

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/intel/display-regs-gen9.h`（新） | display 9 の register の値（Linux 6.8.12 の MIT の `i915_reg.h`・`intel_psr_regs.h`・`intel_dpll_mgr.c`・`intel_display_device.c`・`intel_fb.c`、行と sha256 を header に）。`I915_SKL_*` |
| `src/drivers/gpu/i915/display/gen9-flip.[ch]`（新） | readout の読み（PW で gate）と解読 `drv_i915_skl_readout_decode()`（host で試す）、refresh の計算（DP の M/N × DPLL の port clock ÷ total）、display window の大きさ、2 枚の scanout（`scanout.c` の create・pin）、session の PPGTT への uncached の map、latch 待ち（1 tick ごとに SURFLIVE を読み、FLIP_DONE の割り込みで早く起きる、上限 100 ms）、arm（stride が違う時だけ PLANE_STRIDE、そして PLANE_SURF）、restore、割り込みの有効・無効と handler、log。既定の 1 か所: `I915_GEN9_DISPLAY_FLIP_DEFAULT`（0 = copy） |
| `src/drivers/gpu/i915/display/bootfb.c` | bind で `drv_i915_gen9_flip_start()`（語が flip で readout が通れば flip）。query・mode は flip の時 plane の大きさと測った refresh を返す。claim は flip の時 buffer を黒にして割り込みを有効に（firmware の framebuffer は消さない）。present は flip → 失敗（ETIMEDOUT）なら `i915_bootfb_flip_failed()`（firmware の surface に戻し、framebuffer を消して copy の道へ、この boot の間）してこの frame を copy。copy の道は helper `i915_bootfb_present_copied()` に切り出しただけで中身は同じ。wait は bootfb の物を新しく（flip の時 latch を待ち、latch の tick を present_time に、copy の時は今の `drv_i915_present_display_wait`）。lease の終わりで firmware の surface に戻す。fini で buffer を解放（plane が見ている物は abandon） |
| `src/drivers/gpu/i915/irq.c` | Gen9 の handler で display の bit があれば display の op の handle を呼ぶ（今までは数えるだけ） |
| `src/drivers/gpu/i915/device.c` | interim の display の handle: Gen9 なら `drv_i915_gen9_flip_irq(master_ctl)`（他の版は今と同じ何もしない） |
| `include/kern/boot.h`・`src/kern/boot.c` | key `i915.gen9.display=`（flip・copy） |
| `platform/amd64/vmunix.mk` | `display/gen9-flip.c` を足す |
| 試験 | `src/drivers/gpu/i915/tests/display/host-gen9-flip-test.c`・`plan/ws237/tests/run-gen9-flip-host-test.sh`、`plan/ws237/tests/host-boot-gen9-display.c`・`run-host-boot-gen9-display.sh`、image の config `plan/ws237/tests/config-amd64-gen9-flip.mk` |

設計からの変更: readout の条件に FBC（`ILK_DPFC_CONTROL` 0x43208 bit 31、`fbc`）を足し、PLANE_POS は見ないことにした（design.md §3.2 を直した）。

Gen12（5330）の道: bootfb は `graphics_ver == 9` の absent の node にしか bind されず、interim の handle は Gen9 以外で何もしない。Gen12 の display が live の node は interim の op を使わない（`display/interrupts.c` が bind する）。

## 確かめ（2026-10-14 P2、host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws237-p002 build/ws237-p002/vmunix` | rc 0、`warning:`・`error:` 0 行、`kernel include check: PASS`、`amd64 vmunix check: PASS` |
| `sh plan/ws237/tests/run-gen9-flip-host-test.sh` | `gen9_flip_host_test: 48 checks, 0 failures`（SV7 の形 6、受ける変形 12、拒否 17、clock 10、window 3、ASan/UBSan） |
| `sh plan/ws237/tests/run-host-boot-gen9-display.sh` | `host-boot-gen9-display: 11 checks, 0 failures` |
| `sh plan/ws229/tests/run-bootfb-host-test.sh` | `bootfb_host_test: 20 checks, 0 failures`（bootfb の算術は不変） |
| `sh plan/ws229/tests/run-host-boot-gen9.sh` | `host-boot-gen9: 10 checks, 0 failures` |
| `TMPDIR=build/tmp sh src/drivers/gpu/i915/tests/contracts/run.sh` | PASS（mmio dma pci rpm pte sync rps memory forget reset、ordinary + ASan/UBSan） |
| `sh plan/ws084/tests/run-native-decide-host-test.sh` | 14 checks、0 failures |
| `git diff --check` | 出力なし |

SV7 の形の readout（host の試験の値）: eDP、DDI A、DPLL0 の link rate 1350（port clock 270 MHz）、LINK_M 299038・LINK_N 0x80000 → pixel 154000 kHz、2080×1235 → 59950 mHz。実機の値は readout の log の行で分かる。

未実施: QEMU、SV7 の passthrough、SV7 の素の起動、5330 の回帰。

## T1 への依頼の文（Q1 が出す）

> **依頼主**: P2（ws237-p002）。**image**: main の HEAD（a5613f4b2 を含む merge の commit を書く）を `plan/tools/guest/test-image.sh --no-harness plan/ws237/tests/config-amd64-gen9-flip.mk build/ws237-flip`（CI の image に boot の語 `i915.gen9.display=flip` を足しただけ、graphical boot・login は CI のまま）。
> 1. **QEMU（GPU 無し、回帰）**: `plan/tools/boot-test.sh` の標準で同じ image。合否: login の画面。i915 が無いので `i915: gen9 display` の行は出ない。
> 2. **SV7 の passthrough（`I915_MACHINE=sv7`、`plan/tools/hwsv7/`）**: 同じ image を graphical に起動し、SSH で `grep -e 'i915: gen9 display' -e 'i915: bootfb' -e 'KWL VULKAN_ERROR' /var/log/kernel.log` を返す。QMP の screendump の PNG も。
>    - (a) `readout pipe A transcoder EDP …` と `flip on pipe A …` が出たら（host の GOP が灯した eDP が guest に見えた）: 合否は `frame 1 … gpu_copy_us=…` が出て、`did not latch` が無く、login の後しばらくして `N flips, the last 600 at … flip-done irqs M` の行が出ること（M が 0 でも flip は poll で動くので FAIL にしない、値を返す）。絵は SV7 の panel に出るので QMP の PNG は text console のままで正しい（ユーザーが SV7 の画面を見られれば一言）。
>    - (b) `flip not used (<理由>)` が出たら: 理由の語と、その直前の `PWR_WELL_CTL2=…`・`transcoder …`・`pipe A …` の行をそのまま返す。合否は copy の道が今と同じに動くこと（`i915: bootfb: frame 1 …`、QMP の PNG に login 画面か desktop）。
> 3. **5330（Gen12）の回帰（軽く）**: graphical の起動で desktop が panel に出て、`i915: gen9 display` と `i915: bootfb` の行が出ないこと。
> **返し先**: Q1 と P2。FAIL の解析はしない。

## ユーザーの確かめ（SV7 を USB から素に起動、flip の本当の受け入れ）

1. 上の image（`plan/ws237/tests/config-amd64-gen9-flip.mk`）を USB に書く。または release の候補の USB の小さい FAT の partition（`vmunix` と `zedbsd.cfg` の有る所）の `zedbsd.cfg` の最後に 1 行 `i915.gen9.display=flip` を足して保存する（`login=graphical` は足さない）。
2. SV7 で USB から起動する。期待: Kei の logo の後、login の画面と desktop が panel の解像度（SV7 は 1920×1200 の見込み）で出る。
3. **tearing が無いことの見方**: window を左右に速く引きずる、または scroll の長い page を速く scroll する。画面の途中に横の切れ目（上と下で絵がずれる線）が出なければ良い。今の copy の道（語を `copy` にするか消す）と見比べると差が分かる（copy の道は切れ目が出得る）。
4. log を見る（端末で）: `grep 'i915: gen9 display' /var/log/kernel.log`。正しい時の行:
   - `i915: gen9 display: readout pipe A transcoder EDP 1920x1200 stride 7680 XRGB8888 surf 0x… refresh 5995x mHz (measured, …) scaler off fb=same`
   - `i915: gen9 display: flip on pipe A, buffers at 0x… and 0x… (GGTT), …`
   - `i915: gen9 display: frame 1 1920x1200 gpu_copy_us=…`
   - しばらく使った後の `i915: gen9 display: N flips, the last 600 at gpu_copy_us=… latch_wait_us=… each, flip-done irqs M`（M が flip の数に近ければ割り込みが届いている）
   - 出てはいけない行: `did not latch`、`flip not used`。
5. 画面が乱れた・黒い・固まった時: 電源を切り、`zedbsd.cfg` の行を消す（または `copy` にする）と今の表示（BUG-310 の bootfb）に戻る。`flip not used (理由)` の行が出ていればその理由と前の数行を写真で。
6. 通ったら Q1 に知らせる。Q1 が `src/drivers/gpu/i915/display/gen9-flip.h` の `I915_GEN9_DISPLAY_FLIP_DEFAULT` を 1 にする（その後は `i915.gen9.display=copy` で今の表示に戻せる）。

## 制限（段 (1) の範囲）

- mode・明るさ・外部の display は firmware のまま（段 2・3）。suspend は今のとおり拒む（段 4）。
- firmware が灯した pipe がちょうど 1 つの時だけ（laptop の panel だけ、または外部だけ）。両方灯っていれば copy の道。
- lease と lease の間（greeter → session）は firmware の surface（text console）が一瞬見える（bootfb と同じ）。
- 割り込みが来ない時は 1 ms の poll で latch を確かめるので動くが、待ちが最大 1 ms 粗くなる。

## 残り

T1 の結果と SV7 の素の起動の結果を見て、Q1 が既定を変えるか判断する。p003（backlight）以降は p002 の実機の結果の後（Q1 の指示）。
