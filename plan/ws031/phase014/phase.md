# WS031 p014 計画: モデルビューア（mview）を i915 で動かし、i915 のシェーダーを一通り動かす

p013 の mview（Venus で PASS）を QEMU＋passthrough（Latitude 5330）の i915 で動かす。ビューアが要る機能から順に i915 の
Vulkan executor（`src/drivers/gpu/i915/render/`）と SPIR-V compiler（`src/drivers/gpu/i915/compiler/`）を拡張し、各段で
小さな試験 shader を実機で確かめてから先へ進む。その後、ビューア以外の shader も一通り通るよう compiler を広げる。

## 現状との差（2026-09-22 調査）
mview の shader は意図して単純（行列型なし、push constant のみ、varying 2、頂点入力 3）。それでも次が足りない。

| 区分 | mview が使うもの | i915 の現状 |
| --- | --- | --- |
| 描画命令 | `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint32） | 未実装（ENOTSUP） |
| 動的 state | `vkCmdSetViewport`/`vkCmdSetScissor`、dynamic state | 未実装（pipeline で警告） |
| 転送 | `vkCmdCopyBuffer` | 未実装 |
| texture | mip 付き RGBA8（`vkCmdBlitImage` で mip 生成）、trilinear sampler | 1 mip・LOD 0 固定・bilinear まで |
| push constant | fragment stage（offset 112 の material 色） | vertex stage だけ |
| command buffer | 1 frame で約 70 操作、upload で数百 | 1 command buffer 64 操作まで |
| blending | blend pipeline（qs40 は blend material なし、生成は必要） | 生成は通るが無視（XXX） |
| compiler（VS） | `normalize`、`max`（GLSL.std.450） | Sin/Cos/InverseSqrt のみ |
| compiler（FS cutout） | `FOrdLessThan`、`OpSelectionMerge`/`OpBranchConditional`、`OpKill` | 1 basic block のみ、discard なし |
| 検証 | 画面の取得 | passthrough は VNC がない（カメラのみ） |

## 段階
各段は「実機の小試験 PASS → 回帰（vkdemo offscreen `7523debe…05ff`、display `94615464…19b1` ended PASS、wltest、ktest）→ 次へ」。

### A0. capture display（試験 build 専用、ユーザー承認 2026-09-22）
iGPU に専用 VRAM はなく、描画結果は guest RAM にある。`I915_TEST_CAPTURE=y` の build では i915 は LCD を初期化せず
（modeset・panel 電源なし）、display node に「取り込み用の仮想 display」を出す（query/mode/claim は従来どおり）。present は GPU copy で
物理連続の capture 領域（複数 slot、header に frame 番号・寸法・hash）へ写し、領域の guest 物理 address を起動時に 1 度 log する。
host は `run-parity-vk.sh` に QMP socket・`usb-tablet`・`usb-kbd` を足し、`pmemsave` で領域を読んで PPM にする。p013 の Venus 試験
（`mview-qemu.py`）と同じ操作列・同じ 6 検査を i915 で行い、Venus 画像とも比べる。以後の段はこの経路で確かめ、実 LCD 経路
（`vkloop-hw.sh display`、ended PASS）は各段の区切りと Phase の最後にカメラで確かめる。

### A. 実行器の基礎（mview の描画命令）
1. `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint16/uint32、`3DSTATE_INDEX_BUFFER`、`3DPRIMITIVE` の random access）。
2. `vkCmdSetViewport`/`vkCmdSetScissor` と dynamic state（`3DSTATE_VIEWPORT_STATE_POINTERS_*`/`SCISSOR`）。
3. `vkCmdCopyBuffer`（blitter か既存の GPU copy 経路）。
4. command buffer の操作数の上限を撤廃（可変長の op 列）。descriptor set の割当上限（1 回 8）も確認して必要なら緩める。
5. fragment stage の push constant（`3DSTATE_CONSTANT_PS`）。
- 小試験: 索引付き三角形、viewport の一部だけに描く、buffer copy の byte 一致、70 操作の command buffer。

### B. texture と mip
1. mip 付き image の配置（linear のまま mip ごとの offset、`RENDER_SURFACE_STATE` の MIP count/LOD）、mip 間の `vkCmdBlitImage`。
2. sampler の mipmap mode（nearest/linear）、min/max LOD、LOD bias。
3. format 特性の報告（RGBA8 の BLIT/LINEAR、D32 の attachment）が実装と一致すること。
- 小試験: 各 mip を別色で塗った texture を縮小して描き、選ばれた mip の色を readback で確認。

### C. compiler: mview の shader
1. GLSL.std.450 の `Normalize`、`FMax`/`FMin`、`FClamp`、`FAbs`、`Sqrt`、`Pow`、`FMix`、`Floor`/`Fract`（EU の math 命令と組合せ）。
2. 比較（`FOrd*`/`FUnord*`/整数比較）→ flag register、`OpSelect`。
3. 構造化制御フロー（`OpSelectionMerge`＋`OpBranchConditional`）を SIMD8 の predication/IF-ELSE-ENDIF で。
4. `OpKill`（discard）: pixel mask を落とし、全 channel が消えたら早期終了。
- 小試験: 各命令を 1 つずつ使う shader を readback で CPU 参照値と比較（`tests/render/` の oracle 方式）。

### D. mview を i915 で動かす
1. 試験環境: `run-parity-vk.sh` に `qemu-xhci`＋`usb-tablet`＋`usb-kbd`、QMP socket（`input-send-event`）を足す。
2. capture display（A0）の画像を Venus の画像と比べる（GPU が違うので完全一致でなく許容差: 平均差・PSNR と形の一致）。
   `R` で初期 frame と画素一致は i915 でも必須。
3. `vkloop-hw.sh mview`（zwl＋mview、QMP で p013 と同じ操作列）とカメラ写真で確認。
- 受入: model_visible・drag_rotates・right_drag_pans・wheel_zooms・keys_turn・reset_restores_first_frame が i915 で真、
  Venus 画像との許容差内、写真確認。

### E. blending と「一通りのシェーダー」
1. color blending（`BLEND_STATE`/`3DSTATE_PS_BLEND`、src/dst factor、op、write mask）。mview の blend pipeline を試験 model で確認。
2. compiler を一般化: `OpTypeMatrix`/`OpMatrixTimesVector`/`OpMatrixTimesMatrix`/`OpTranspose`、`FDiv`/`FMod`、整数演算、
   `OpLoopMerge` のループ（有界ループ・break/continue）、varying/頂点入力の上限拡大（16 まで）、register spill（scratch）、
   uniform buffer（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`、binding table 経由の読み）、複数 sampler。
3. shader 試験一式（`plan/ws031/tests/shaders/`）: GLSL→SPIR-V（glslc）を i915 で実行し CPU 参照と比較する表形式の試験。
   mview にも「行列 uniform＋per-pixel 光源」版の shader を追加して実機で使う。

### F. 性能（任意）
draw ごとの同期 batch を 1 command buffer 1 batch にまとめる、同期 present の見直し。mview の fps を記録。

## 依存・触れないファイル
- 前段: p013（mview、zwl 入力）、E-133（i915 の Wayland）。
- 触れる: `src/drivers/gpu/i915/render/**`、`compiler/**`、`tests/**`、`userland/base/mview/`（`--readback`、追加 shader）、
  `plan/ws031/tests/`、5330 の `~/bigbang/run-parity-vk.sh`（入力 device と QMP）。
- 触れない: UAPI、libvulkan の wire（必要なら事前に提示）、Venus 経路。

## 見積
A0 0.5 日、A 1 日、B 1 日、C 1.5 日、D 1 日、E 3 日、F 任意。D までで「mview が i915 で動く」、E までで「一通りのシェーダー」。

## 進捗
### A0 完了（2026-09-22）
- kernel: `display/capture.c`（`I915_TEST_CAPTURE=y` の時だけ build）。LCD は使わず（display absent 扱い）、present を GPU copy で
  連続 32 MiB の capture 領域（4 slot）へ。layout は capture.c 冒頭。production vmunix は変更前と byte 一致、host 試験 23/0。
- host: `~/bigbang/run-parity-vk.sh` に `QMP=1`（QMP socket・usb-tablet・usb-kbd）と `VK_STOP_RE`、
  `plan/ws031/tests/i915-capture.py`（pmemsave で読んで PPM、scenario 判定）、`vkloop-hw.sh` に `mview` mode と `CAPTURE=<scenario>`。
- 実機: `CAPTURE=vkdemo vkloop-hw.sh display` PASS（取り込んだ画素の RGB hash が vkdemo の報告 `94615464…19b1` と一致）、
  `CAPTURE=wayland vkloop-hw.sh wayland` PASS（wltest の frame を取得、動きあり、WLTEST DONE 600/60、ZWL EXIT error=0）。
### A 完了（2026-09-22）
- executor（`render/`）: `vkCmdBindIndexBuffer`/`vkCmdDrawIndexed`（uint16/uint32、bind offset・firstIndex・vertexOffset・instance、
  `3DSTATE_INDEX_BUFFER`＋`3DPRIMITIVE` RANDOM/Base Vertex）、`vkCmdSetViewport`/`vkCmdSetScissor`（index 0、pipeline の dynamic state で
  pipeline 値を上書き、未設定の draw は拒否）、`vkCmdCopyBuffer`（4096 texel 行の linear RGBA8 GPU copy、4 の倍数でない region は XXX で ENOTSUP）、
  op 列を可変長に（64 から倍々、上限 65536、超過・確保失敗は end が VK_ERROR_OUT_OF_HOST_MEMORY）、fragment の push constant
  （`3DSTATE_CONSTANT_PS` buffer 3、`3DSTATE_PS` Push Constant Enable）。compiler は既に FS の push load に対応しており変更なし。
  genxml.h に Mesa 25.0.7 gen120.xml（sha256 e2452c7d…542e）から INDEX_BUFFER・Vertex Access Type・PS Push Constant Enable。
  mview の他の上限（set 割当 1 回 1、bind 1、pipeline 作成 1 回 1×6、submit 1）は現行上限内で変更なし。
- 実機: `vkloop-hw.sh test vkx`（`tests/render/executor.c`）5/5 PASS: INDEX16・INDEX32・VIEWPORT・COPY（byte 一致）・GRID（134 操作、FS が byte 112 の push 色）。
- 回帰: offscreen `7523debe…05ff`、ktest 383/0（12 skip）、draw 1024/1024、tex 1024/1024、t3 9/9、bl 4/4、`CAPTURE=vkdemo display`
  PASS（capture_equals_presented、`94615464…19b1`）。host: vk fixture 全一覧 PASS、contracts PASS。

### C 完了（2026-09-22）
compiler に GLSL.std.450（Normalize/FMax/FMin/FClamp/FAbs/Sqrt/Pow/FMix/Floor/Fract/Exp2/Log2）、FDiv、比較 12 種・論理・Select、
選択構造の if-conversion（predication）、OpKill（f1.0 の pixel mask、RT write を predicate）。実機 `test vkc` 9/9 PASS。

### D: mview が i915 で動作（正常系 1 パス、2026-09-22）
- capture display（`CAPTURE=mview vkloop-hw.sh mview`）: 6 検査すべて真、`R` で初期 frame と画素一致、Venus 画像との PSNR は
  initial/pan/reset = ∞（完全一致）、rotate/keys 104.8 dB、zoom 107.8 dB（Venus も同じ 5330 の Iris Xe を Mesa anv で使うため）。
- 実 LCD（`vkloop-hw.sh mview`）: 640×480 を 1920×1080 panel に表示、写真確認（`C:\Work\qemu-work\mview-lcd.jpg`）。
- B（mip）は途中の実装のまま tree にあり、mview の経路では問題なし。B の試験（MIP-NEAREST/LINEAR、blit 連鎖）は後回し。

## 後回しの確認・強化（ユーザー指示 2026-09-22: 正常系の疎通を優先、以下は最後に 1 回）
- 回帰は最後に 1 回: offscreen `7523debe…`、display ended PASS（写真）、wltest、ktest、eu/draw/tex/t3/bl、vkx、vkc、CAPTURE=vkdemo。
  （t3・bl は C の後まだ未実行。ktest は A 後 383/0/12、1 件 skip→pass の中身未確認。）
- B の仕上げ: mip の試験（各 level の色、NEAREST/LINEAR、blit 連鎖の box filter）、format 特性の照合。
- 準正常系: uint8 index（XXX で拒否）、4 byte 非整列の CopyBuffer（ENOTSUP）、viewport/scissor index>0、負の viewport 高さ、
  blend material を持つ model、HALT による discard 早期終了、`R` 以外の視点の再現性。
- 異常系: 範囲外 index/offset、command buffer 65536 操作超、descriptor 上限、shader compile 失敗時の pipeline 解放（既知 XXX）、
  PS/2 keyboard の key が zwl に届かない件（p013）、QMP `input-send-event` の `device` 指定で QEMU abort。

## 性能（途中で停止、2026-09-22）
mview `--spin=30`（640×480、16 draw/frame、37k 三角形、実 LCD）。kernel の `i915: perf:` 行で内訳を記録。
| 段階 | fps（30 s 平均） | 内訳 |
| --- | --- | --- |
| 基準（変更前） | 14.87（計測コード入り 14.37） | submit 1 回 28.6 ms（build 0.28 / run 28.3 / GPU 3.8）、約 9.5 batch/submit、2 submit/frame、present 8.8 ms（copy 1.3 / publish 2.4 / flip 4.0） |
| command buffer ごとの 1 batch＋静的資源の再利用＋per-frame 検査の撤去 | 45.53 | submit 1.7 ms（GPU 1.2）、1 batch/submit、present 3.5 ms |
| ＋vsync を待たない（`-DI915_PRESENT_NO_VSYNC=1`） | 46.08（定常 約 48） | — |
- 未解決: no-vsync の run で表示停止が `ended FAIL`（調査前に停止）。起動直後の 1.9 s の frame は残る。
- 未実施: vkdemo offscreen hash と `CAPTURE=mview` の確認、変更の整理。途中の diff は centris `/tmp/perf-pass-stopped.diff`。

### 性能 第 2 回（2026-09-22、Fable で実施）: 14.9 fps → 190 fps（no-vsync）/ 60.4 fps（vsync、60 Hz 上限）
計測は mview `--spin=30`（640×480、16 draw/frame、37k 三角形）。mview の `MVIEW STAGES`（rdtsc、TSC 2.496 GHz）と kernel の `i915: perf:` で内訳。

| 段階 | fps（no-vsync） | 1 frame | 主因 |
| --- | --- | --- | --- |
| 前回の終了時 | 46 | 21.7 ms | — |
| libvulkan: `vkResetFences` ごとの notification 台帳の reap を止める（`sync_notifications_reap_if`、8 件未満は問い合わせない） | **155** | 6.45 ms | reap の ioctl 1 回が平均 4.8 ms（後述の scheduler の性質）× 2 reset/frame × 数件 = 13 ms/frame |
| kernel: marker request（fence 用の空 request）を GPU に送らず即完了 | 162 | 6.18 ms | worker は 1 request ずつ完走させるので marker は到達時点で完了 |
| kernel: flip を worker では arm だけにし、FIFO の待ちは present した thread で行う | 166 / **58.5（vsync）** | 6.0 / 17.1 ms |
| 計測用の一時 diagnostic を除去（最終） | **190 / 60.4（vsync）** | 5.25 / 16.6 ms | vsync 時に worker が vblank 待ちで塞がり render が後ろに並んでいた（submit 14.8 → 4.2 ms） |

そのほか: kernel tick を 1 kHz に（`KERN_CLOCK_HZ`、LAPIC 周期は 10 ms 校正窓を HZ で割る、i915 の raw tick 定数 5 か所を HZ 基準に）。
起動直後の最大 frame 時間 1.9 s → 0.63 s。fps への直接効果は無し。`wayland_wait` の nanosleep(1 ms) を socket の poll に（tick 量子化の除去、効果は小）。
zwl は present 後に client を flush（callback が次の pass まで残らない）。停止経路: buffer A へ戻す flip も settle（`ended PASS` に復帰）。

**見つかった構造（専門家向け）**
1. **scheduler**: thread は CPU に固定、quantum 5 tick、同一 CPU への wakeup は `need_resched` を立てるだけで、running thread は
   次の `kern_preempt_enable`（ほぼ全ての syscall 内）で yield する。i915 worker は CSB を udelay(50 µs) で busy-poll するので
   GPU 実行中（1–3 ms）はその CPU を占有し、同一 CPU に woken された thread は 1–5 ms 待つ（`SCHED_WAKE_LATENCY=1` の
   計測: same-cpu 455 件/5 s が 1–5 ms、cross-cpu 平均 32 µs）。この性質のため「非 block の ioctl」が平均 5 ms かかっていた。
   → worker の完了待ちを IRQ（user interrupt）+ waitq に、wakeup 時の preempt 方針の見直しが本筋。
2. **同期 executor**: 1 frame に GPU round trip が 3 回直列（render 1.8 ms、libvulkan の present job の `vkCmdCopyImage` 1.8 ms、
   zwl の拡大 copy 3.5 ms）で、CPU は各回完了を待つ。非同期 submit（queue して即返し、fence は完了時に signal、state/batch heap の
   多重化）にすれば frame ≈ max(CPU, GPU) ≈ 3 ms 台（300 fps 前後）が見込める。
3. **frame の copy が 2 回**: libvulkan の WSI は app の swapchain image（OPTIMAL）から export 用 shared image（LINEAR）へ毎 frame
   copy する（WS014 の設計）。i915 では両者 linear で同一 layout なので aliasing で 1 回分（≈1.8 ms）消せるが、Venus と共通の
   libvulkan では driver 依存になるため未実施。zwl 側の 640×480→1920×1080 拡大 copy（1.58 ms GPU）は plane scaler か
   zero-copy flip で消せる。
4. GPU 自体の render は 1.3 ms/frame（37k 三角形、640×480）で、まだ fixed overhead 支配。三角形数から見た上限はこれより 10 倍上。

**未解決・後回し**
- `I915_PRESENT_NO_VSYNC` は build option のまま（本来は present mode = MAILBOX/IMMEDIATE で選ぶべき）。
- 今回の変更の回帰（vkx/vkc/ktest/wltest/Venus 側 libvulkan）は最後の 1 回にまとめる。libvulkan の reap 変更は Venus にも効く。
- i915-capture.py: frame の到着は log 行でなく capture 領域の write_count で判定するよう修正済み（serial の混線対策）。
