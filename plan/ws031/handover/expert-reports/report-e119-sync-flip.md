# WS031 E-119 報告 — GPU停止未確認時の保護・PTE解放契約／TLB・同期flip（LCD-C 第一枠・LCD-D 第二枠 とも実機 PASS）

日付: 2026-09-19　対象: Latitude 5330（ADL-P iGPU、VFIO/QEMU、明示 VBT、10 ms tick、execlists）
HAL・既存 UAPI の変更なし。git の commit／push はしていません。

## 0. 要約

| 項目 | 結果 |
|---|---|
| ① GPU停止未確認時の保護を外側 teardown まで | 実装済み。GPU-free で 未完了 → 失敗分岐 → latch → 再実行拒否 → teardown 保持 → runner `retained=1` を確認 |
| ② PTE 解放を一つの契約に＋正本どおりの GT TLB 無効化 | 実装済み。unmap 全件 → 表を読み直して確認 → TLB → 解放。失敗・再呼出しで二重計上なし。T1〜T3 の harness の残り PTE も撤去（fixture は変更なし） |
| ③ show_prepared: 「表示へ渡していない」と「停止確認」を区別 | `display_acquired` を追加。GPU 描画後に prepare が拒否される場合を試験済み |
| 小修正 | vblank 計数は lock 下の snapshot で読む。IRQ uninstall のログは実際に走ったときだけ出す |
| 同期 flip（正本 `intel_pipe_update_start/end` を一式で移植） | host 115/0、GPU-free ktest 535/0（最終 source） |
| **LCD-C 第一枠（CPU で用意した A/B を同期 flip）** | **実機 PASS**: modeset 1 回のまま A→B→A→B→A、4/4 で世代・旧／新 surface・live の遷移が一致、写真 6 枚すべて期待どおり、停止確認後に両 buffer を回収 |
| **LCD-D 第二枠（表示していない側を GPU で描き直して flip）** | **実機 PASS**: GPU 描画 9/9（毎回 2,073,600 画素一致）、flip 8/8、variant ごとの image hash が buffer・回を問わず同一、写真 10 枚すべて期待どおり、A/B の RT PTE 2025/2025 を scratch へ戻して TLB 無効化（11 回、timeout 0）の後に回収 |
| 回帰 sweep（最終 source） | **13/13 PASS**（8 mode ＋ LCD-B／R／G／C／D、各 ktest 535/0、runner はすべて probe=COMPLETE cleanup=1） |

## 1. 接続 ①: GPU停止未確認 → device の保持状態 → 外側 teardown

`parity_lcdg_finish()`（lcd/parity_lcd_kernel.c）が LCD-G の成功・失敗どちらの経路でも最後の判断をします:

1. `submitted && !gpu_done` → `parity_fhd_render_keep()` が、未完了の request が使いうるものをすべて keep にします
   （texture、shared state、batch、timeline page、RT、context の ring と state＝`parity_lrc_keep()`）。
   scanout は abandon、`parity_lcd_show_retain_gpu(gm, why)` で device に GPU latch を立てます。
2. 表示を取得したのに停止を確認できていない → 何も解放しません（show_prepared が abandon と latch を済ませています）。
3. それ以外 → `parity_fhd_render_release()`（§2）。失敗したら abandon と GPU latch。成功した場合だけ unpin／destroy。

外側（probe.c の teardown）: `parity_lcd_kernel_gpu_retained()` が真なら **engines の release と ppgtt／gt_mem の fini を行わず**
（vm、ページ表、GT object がすべて残ります）、`resources_retained=1` を記録します。runner は `lcd_retained=1` を報告します。
GPU latch を捨てられるのは gm を fini した model の場合だけです（`parity_lcd_show_discard_gpu_model(gm, gm_finalised)`）。

GPU-free（lcdg_ktest）: FIN-GPU（未完了 → keep と latch）、FIN-REFUSE（latch 中の再実行は -1、summary.retained=1）、
FIN-TEARDOWN（gm fini は keep した 4 object 以上を残す）、FIN-NOTSHOWN（表示へ渡す前の失敗 → 回収）、FIN-DISCARD。

## 2. 接続 ②: PTE 解放の契約と TLB（`parity_fhd_render_release()`, gt_tlb.c）

```
if released                 → 0（二回目以降は何もしない）
if submitted && !gpu_done   → -EBUSY（何も解放しない）
1. fhd_maps(): state / batch / texture / RT の全 mapping（3 + RT 2025 page）→ scratch へ
2. この呼出しで表を読み直す: maps_scratch == maps_total でなければ -EIO
   （計数は毎回その呼出しの確認値で、合算しない。first_unreleased_va を記録。ownership は残す）
3. parity_gt_invalidate_tlb_full()   失敗 → その rc（何も解放しない）
4. ここで初めて texture と draw object を解放、x->rt = 0、released = 1
```

`parity_gt_invalidate_tlb_full()` は正本の `mmio_invalidate_full()` に沿っています:
FORCEWAKE_ALL の get、GT reset との直列化、全 engine（software の park は power-down ではないため、parked flag では省略しません）、
gen12 の register（RCS 0xced8、VD 0xcedc＊、VE 0xcee0＊、BLT 0xcee4、COMPCTX 0xcf04、＊は masked）、done = BIT(instance)、
100 µs／4 ms の待ち、ADL-P WA の OA 0xceec=1、`seqno += 2`。計数: invalidations／engines_invalidated／timeouts／time_faults／fw_failures。

T1〜T3 の harness: `eu_scrub_fixture_ptes()` が fixture の 6 page を release の最初で scratch へ戻します（fixture の内容は不変）。

GPU-free: TLB-REGS／TLB-FULL／TLB-STUCK（done が立たない → timeout、何も解放しない）、REL-BUSY、REL-TLB、
REL-RETRY（1 page 残し → -EIO、確認値 7/7 で 14 にならない、再呼出しで成功）、REL-ONCE。

実機（LCD-G、最終 sweep）: 「LCD-G release: mappings back to scratch 2028/2028 (first left 0x0) TLB rc=0 (invalidations 1, engines 5, timeouts 0) release calls 1」。

## 3. ③ と小修正

- `display_acquired` は commit enable の直前に立てます。立っていなければ「表示へ渡していない」＝停止確認は不要で、所有者は回収できます。
  G-NOTSTARTED（qgv 帯域 0 で prepare を拒否）→ acquired=0、書込み 0、PINNED、所有者が回収。
- vblank の count は `vbl_snapshot()`（IRQ lock 下）で読みます（wait の loop と count_seen）。
- IRQ uninstall のログは実際に走った場合だけ出します。走らなかった場合は「NOT run … irq_attached=1 resources_retained=1」。
- 「同じ backing」の確認は代表 page（最初・中間・最後）として記録します。全 page 版は後で追加できます。

## 4. 同期 flip（plane だけの更新）

**正本から生成**（port_lcd_calc.py、reference text のまま）: `intel_pipe_update_start`／`intel_pipe_update_end`、
`intel_crtc_get_vblank_counter`、`intel_crtc_needs_vblank_work`、`intel_mode_vblank_start`、`intel_crtc_vblank_evade_scanlines`、
`g4x_get_vblank_counter`、`__intel_get_crtc_scanline`、`intel_get_crtc_scanline`、`intel_crtc_scanline_offset`、
`intel_crtc_update_active_timings`（`intel_enable_crtc` の位置で呼びます）、`I915_MODE_FLAG_*`。

**流れ**（`parity_lcd_ms_plane_update_flip` → `parity_lcd_modeset_flip`）:
noarm → `intel_pipe_update_start`（vblank evasion、100 µs、`drm_crtc_vblank_get`、IRQ off）→ arm（PLANE_SURF の書込み）
→ `intel_pipe_update_end`（event を arm して event 側の vblank 参照を取る、atomic update failure の検査）。
commit_tail と同じく、DC_OFF を flip の前後で保持し、flip-done の後に 17 ms の async put で返します（model が DC_OFF なしの arm を検出したため追加）。

**完了条件**: event が完了し（その pipe の新しい vblank IRQ で、frame counter が arm 時点から進んでいる）、**かつ PLANE_SURFLIVE == 新 surface**。
- event なし → TIMEOUT（stuck、event の vblank 参照は保持。後から完了しうるため）
- event はあるが live が旧 surface → NOT_LATCHED（stuck）
- stuck／pending 中の flip、同じ buffer への flip、別 pipe の event は拒否
- 不確実なときは両 buffer を保持し、それ以降の flip は出しません。停止経路は走り、停止を確認してから両方を解放します。

**適応**（記録済み）: DRM の vblank wait queue と schedule_timeout → ops->vblank_sleep（その pipe の次の vblank IRQ まで）。
`drm_crtc_arm_vblank_event` → pipe ごとに event 1 個。`local_irq_*` → 入れ子に対応した `kern_irq_disable/enable`。
vblank_time_lock などの lock は所有者 1 人・直列の経路なので no-op。ktime／tracepoint／evasion 統計は持ちません（debug 用）。

**model**（lcd_fake_hw）: SURFLIVE は PLANE_SURF の書込みとは別に次の frame で latch。fault として early event、never latch、no vblank を用意。
host section I: B,A,B,A すべて DONE かつ live==new、arm はすべて区間内（IRQ off、均衡）、event 4 個、同じ buffer は拒否、
STALE（早すぎる event）→ NOT_LATCHED で以後拒否、NOT-LIVE、TIMEOUT（参照保持）、停止経路 OK → **115/0**。

### 4.1 実機 LCD-C（`-DPARITY_LCDC_TEST=1`, run-parity-ref-240）

buffer: A surf 0xfdfc0000（GGTT page 1040320、2025 page＋guard 168）、B surf 0xfe900000（page 1042688）、重なりなし。
pattern A=121、B=122（表示される数字で区別）。各画面を 8 秒保持しました。

| gen | 旧 → 新 surface | live 前 → 後 | frame | event | update errors | 結果 | 写真 |
|---|---|---|---|---|---|---|---|
| – | （A を表示） | – | – | – | – | – | 0: 121 ✓ |
| 1 | A → B | 0xfdfc0000 → 0xfe900000 | 539 → 540 | 0 | 0 | DONE | 1: 122 ✓ |
| 2 | B → A | 0xfe900000 → 0xfdfc0000 | 1025 → 1026 | 0 | 0 | DONE | 2: 121 ✓ |
| 3 | A → B | 0xfdfc0000 → 0xfe900000 | 1511 → 1512 | 0 | 0 | DONE | 3: 122 ✓ |
| 4 | B → A | 0xfe900000 → 0xfdfc0000 | 1997 → 1998 | 0 | 0 | DONE | 4: 121 ✓ |
| 停止 | – | – | – | – | – | 停止確認 | 5: 消灯 ✓ |

- 各 flip は arm の次の frame（+1）で live が新 surface になりました。flip の間隔は 486 frame（約 8.1 秒、60 Hz）。
- 各 flip が完了してから、古い buffer を所有者へ返しています（`parity_scanout_end`）。
- verdict: PASS（flip 4/4、modeset 1 回、停止確認、両 buffer 解放、停止後の画素 A・B とも誤り 0、保持中の power 参照 0、anomaly なし）。
  runner: `lcd_test=PASS lcd_retained=0 cleanup=1`。teardown: IRQ uninstall が実行され irq_count=67 handled=67。
- vblank sleeps 0: 4 回とも更新開始時の scanline が evasion の範囲外で、待たずに arm しました（範囲内に入った場合の sleep 経路は実機ではまだ通っていません）。
- 撮影の注記: 最初の試行（各画面 3 秒保持）でも verdict は PASS でしたが、カメラの遅れ（約 3〜4 秒）のため写真が 1 枚ずつずれていました
  （122,122,121,121）。記録上は失敗扱いとせず、保持を 8 秒に延ばして撮り直した上の結果を正としています。最初の試行の log も保存しています。

## 5. 第二枠: 表示していない側を GPU で描き直して flip（LCD-D, `-DPARITY_LCDD_TEST=1`）

### 5.1 実装
- **VA**: A = 0x100800000（既存）、B = 0x101000000（新設 `I915_TEX_FHD_RT_B_VA`。A の終端は 0x100fe9000）。VA 配置表は 5 項目になり、重複と alignment の検査を GPU-free で行います（G-VA、RTMAP-LAYOUT）。
- **RT の常時 map**: `parity_fhd_rt_map()` は object 自身の全 page を 1 回だけ insert し、最初・中間・最後の page の walk を確認します。
  `parity_fhd_rt_unmap()` の契約は §2 と同じです: 全 PTE を scratch へ戻す → 呼出しごとに表を読み直す（合算しない）→ TLB → released=1。
  released になるまで所有者は object を解放しません。
- **renderer**: `parity_fhd_render_run_ex(…, rt, rt_va, variant, rt_premapped)`。既存の `parity_fhd_render_run()` は (RT_VA, 0, 0) の wrapper なので、LCD-G の動作は変わりません。
  premapped の場合、描画ごとの release は自分の 3 PTE（state／batch／texture）と TLB と自分の object だけを扱い、RT の map は残ります。
  期待画像と verify は variant に従います（texture の variant 0〜3。偶数・奇数の周期 2 と variant の周期 4 がずれているため、描画されずに残った古い内容は必ず不一致になります）。
- **流れ**: A を GPU で描く（variant 0）→ 表示 → 各 round r=1..8: 表示していない側（状態が PINNED であることを確認。IN_USE なら拒否）へ variant r%4 を描く
  → retire と park → clflush → 全画素比較 → 描画の release（3 PTE と TLB）→ `scanout_begin` → flip（§4 の完了条件）→ 旧 front を `scanout_end`（次の描画先）。
- **失敗時**: GPU 未完了なら描画 object を keep して GPU latch、以後は描画も flip もしません。描画の release 失敗も latch します。flip 失敗なら両 buffer を使用中のまま残し、停止経路に任せます。
  停止を確認できなければ両方を abandon します（map も保持）。確認できれば RT の unmap→TLB の後で、unpin／destroy します。
- forcewake は LCD-G と同じく、run 全体の前後で保持します。

### 5.2 実機結果（run-parity-ref-240、各画面 6 秒保持）

| round | 描画先（表示していない側） | variant | 画素 | hash | 描画の release | flip gen: live 前→後 | frame | 写真 |
|---|---|---|---|---|---|---|---|---|
| 0 | A（表示前） | 0 | 2073600/2073600 | 8de54ee9… | 3/3, TLB 0 | – | – | v0 ✓ |
| 1 | B | 1 | 全一致 | 14dde5a8… | 3/3, TLB 0 | 1: A→B | 437→438 | v1 ✓ |
| 2 | A | 2 | 全一致 | c84455e3… | 3/3, TLB 0 | 2: B→A | 819→820 | v2 ✓ |
| 3 | B | 3 | 全一致 | 0e2b92c2… | 3/3, TLB 0 | 3: A→B | 1202→1203 | v3 ✓ |
| 4 | A | 0 | 全一致 | 8de54ee9… | 3/3, TLB 0 | 4: B→A | 1585→1586 | v0 ✓ |
| 5 | B | 1 | 全一致 | 14dde5a8… | 3/3, TLB 0 | 5: A→B | 1968→1969 | v1 ✓ |
| 6 | A | 2 | 全一致 | c84455e3… | 3/3, TLB 0 | 6: B→A | 2351→2352 | v2 ✓ |
| 7 | B | 3 | 全一致 | 0e2b92c2… | 3/3, TLB 0 | 7: A→B | 2734→2735 | v3 ✓ |
| 8 | A | 0 | 全一致 | 8de54ee9… | 3/3, TLB 0 | 8: B→A | 3118→3119 | v0 ✓ |
| 停止 | – | – | – | – | – | 停止確認 | – | 消灯 ✓ |

- すべての描画で markers a5a50001／c5c50003／d7a3f00d、ps c0ffee01、stale 0、texture・guard 無変更、RT の walk（最初・中間・最後）OK。
- 同じ variant の hash は、描いた buffer（A か B）や回に関係なく同一でした（例: v1 は B の round 1 と 5、v0 は A の round 0・4・8）。
- すべての flip で event rc=0、update errors 0、arm の次の frame で live が新 surface、結果は DONE。
- 最後: 「A target PTEs back to scratch 2025/2025 rc=0 | B 2025/2025 rc=0 | TLB invalidations 11 (timeouts 0)」（描画 9 回＋RT 2 本）。
  verdict PASS（描画 9/9、flip 8/8、modeset 1 回、停止確認、unmap 済み、両 buffer 解放、停止後の画素 A・B とも誤り 0、保持中の power 参照 0、anomaly なし）。
  runner `lcd_test=PASS lcd_retained=0 cleanup=1`、run 後の ktest 535/0。
- 写真: `e119-photos/lcdd-r0-A-v0.jpg` 〜 `lcdd-r9-after.jpg`、一覧は `lcdd-sheet.jpg`。4 種類の画像が順に出て、同じ variant 同士（r0=r4=r8、r1=r5、r2=r6、r3=r7）は同じ画像でした。

GPU-free（lcdg_ktest に 6 件追加）: RTMAP-LAYOUT、RTMAP-MAP、RTMAP-DRAW（premapped の描画 release は自分の 3 PTE と 3 object だけで、RT の 4 PTE は残る）、
RTMAP-TLB（scratch 済みでも TLB 失敗なら released にならない）、RTMAP-RETRY（4/4 で 8 にならない。3 回目は何もしない）、RTMAP-VA（配置表外の VA は拒否）。

## 6. 試験

- host: lcd-modeset **115/0**（+10: section I）、lcd 56/0、dp 72/0。check_generated: 再現可能。
- GPU-free ktest: **535/0**（E-118 比 +20: lcdg_ktest 19 件、G-NOTSTARTED。G-VA は配置表 5 項目に更新）。
- 回帰（`sweep_e119f.sh`、最終 source、round 50 の後）: **13/13 PASS** — EU-REPEAT 5/5、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4、TEX＋明示 VBT、AUX＋SCANOUT、LCD-B、LCD-R 3/3、LCD-G（2073600/2073600、release 2028/2028 TLB rc=0）、LCD-C 4/4（frame と live の遷移は単独 run と同一）、LCD-D（描画 9/9、flip 8/8、2025/2025×2、TLB 11 回 timeouts 0）。各 run の ktest は 535/0。
  round 50 の前の source でも 12/12（LCD-D を除く）を確認しています（`sweep_e119.sh`、ktest 529/0）。
- 注記: どの run の log にも「TLB invalidation did not complete in 4ms」が 8 行出ます。これは実機試験の後で走る GPU-free ktest（fake の register が done を返さない TLB-STUCK／REL-TLB／RTMAP-TLB）が意図的に出しているもので、GPU なしの run にも同じ 8 行があります。実機の release 行はすべて timeouts 0 でした。

## 7. 成果物

- review diff（E-118 tree との差分）: `increment-results/e119-review-changes.patch`
- 累積 patch: `increment-results/e97-e119-changes.patch`（2bf790a4 に当てると現在の source と一致することを確認。写真は含めていません）
- 実機 log: `increment-results/e119-run-parity-hw-lcdc-first.log`（LCD-C 3 秒保持の最初の試行）、`e119-run-parity-hw-lcdd.log`（LCD-D 単独 run）、最終 sweep の各 log `e119f-run-parity-hw-*.log`（最初の sweep は `e119-run-parity-hw-*.log`）
- 写真: `increment-results/e119-photos/`（`lcdc2-*.jpg` LCD-C、`lcdd-*.jpg` と `lcdd-sheet.jpg` LCD-D）
- 作業 script: `handover/tools/lcd-e119/`（round42〜50）、`sweep_e119.sh`、`sweep_e119f.sh`

## 8. 未解決と注記

- LCD-D は描画ごとに context／batch／state／texture を作り直しています（毎回 release 契約と TLB を通すため）。同じ context で繰り返し submit する形はまだです。
- vblank evasion の sleep 経路は実機でまだ通っていません（LCD-C で vblank sleeps 0。LCD-D の log には sleep 回数を出していません）。
- 「同じ backing」の全 page 検証は代表 page で記録しています（全 page 版は後で追加できます）。
- flip の event は、pipe ごとに 1 個の自前の記録で、DRM の event queue ではありません（適応として記録）。
- IMR bit 17/18、PIPESTATUS bit 30/29 は引き続き未解決です。
