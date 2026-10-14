<!-- awesome-plan project=zedbsd record=ws237-p004 -->
# ws237-p004: 段 (3) mode set と外部の display（DP・HDMI、hotplug）

Status: in-progress — register の順と計算は実装・host で確かめ済み（aa3250e12・cafce9894）。node（呼び出し側）は未着手。2026-10-14 Q1 の指示で BUG-312 を先にするため中断（P2）。同日 P3 が引き継ぎ、偽の IO（hardware の model）を共有の fixture に切り出したところで、Q1 の優先の変更（WS037 の compiler へ）により再び中断。
Disposition: normal
Parent: [WS237](../ws.md)
Queue: Q1 の指示（2026-10-14、P2: p003 → p004 → p005、既定は copy のまま、boot の語 `i915.gen9.display=modeset` で有効）
依存: [p002](../phase002/phase.md)（readout・flip の buffer）、[p003](../phase003/phase.md)

## 済んだもの（どれも実行時にはまだ呼ばれない。既定は copy のまま）

| file | 内容 | 出典（MIT、Linux 6.8.12） |
| --- | --- | --- |
| `intel/display-regs-gen9-modeset.h` | p004 の register・DPCD の定数 | i915_reg.h、drm_dp.h の値 |
| `display/gen9-mode.[ch]` | CD clock、EDID からの mode、DP の link と M/N、DPLL の語（DP・WRPLL）、DDI buffer の表、training の調整、watermark・DDB | intel_cdclk.c、intel_display.c、intel_dpll_mgr.c、intel_ddi_buf_trans.c、intel_dp_link_training.c、skl_watermark.c |
| `display/gen9-io.[ch]` | register の読み書きと待ちの表（host の model と差し替える） | — |
| `display/gen9-ddc.[ch]` | AUX（DPCD、I2C-over-AUX の EDID）、GMBUS の EDID（WA #0868） | intel_dp_aux.c、intel_gmbus.c |
| `display/gen9-link.[ch]` | DDI buffer、DP の link training（CR・EQ、TPS3） | intel_ddi.c、intel_dp_link_training.c |
| `display/gen9-modeset.[ch]` | PW2・DDI IO の power well、DPLL の on/off、外部の出力の on/off（hsw_crtc_enable の順、HDMI の WA #1143）、pipe の WM、plane、vblank、hotplug の検出 | intel_display_power_well.c、intel_dpll_mgr.c、intel_display.c、intel_ddi.c、skl_universal_plane.c、i915_irq.c |

試験:

- `plan/ws237/tests/run-gen9-mode-host-test.sh`: 71 件。期待値は、Linux の計算を別に書いた Python の model から出した。WRPLL 148.5 MHz は 0x80400173/0x3a5。
- `plan/ws237/tests/run-gen9-modeset-host-test.sh`: 80 件。AUX・GMBUS・power well・DPLL・training・DP と HDMI の出力の on/off・WM を、hardware の model で試す。
- amd64 の vmunix の build は warning 0（`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws237-p002 build/ws237-p002/vmunix`）。

## 再開の手順（決めた設計）

1. `display/gen9-node.c` を書く。
   - 出力の一覧: port B・C・D を SFUSE_STRAP で見る。AUX で DPCD が読めれば DP、読めなければ GMBUS の EDID で HDMI。
   - hotplug: 1 秒ごとの kthread で SDEISR を見る。変化したら sequence を進めて `poll_notify()`。
   - display id: 1 = panel、1 + port = 外部。
   - 点灯と消灯: 外部の id の claim で点灯し、release で消灯する。pipe は panel 以外の 2 本、DPLL は panel が使わない物。
   - lease の番号は panel と重ならない範囲から取る。
   - 外部の flip: 各出力に buffer 2 枚。SURFLIVE を tick ごとに見る（割り込みは使わない）。
   - latency: PCODE（`GEN9_PCODE_READ_MEM_LATENCY`）から。KBL 以降で IPC が on なら +4 us（skl_wm_latency）。
   - DDB の付け替え: 点灯は pipe の昇順、消灯は降順。変わる pipe ごとに SURF を同じ値で arm して vblank を待つ。
   - panel の pipe の plane 2・3: 無効なら BUF_CFG を 0 に。
2. `gen9-flip.c` を変える。
   - modeset の段では、display window を外部の buffer 4 枚（4096x2160）の分だけ大きく取る。
   - readout を node に渡す accessor を足す。
3. `bootfb.c` を変える。
   - query の数を 1 + 外部の数にする。
   - id ≥ 2 と node の lease は node へ渡す。
   - events は node の sequence にする。
   - session の close・fini を node へ回す。
4. node の host 試験、build、commit。SV7 での確かめの手順（段の語ごと）を ws.md に書く。

## 2026-10-14 P3 の引き継ぎと中断（再開点）

済み（host だけ）:

- hardware の model を `src/drivers/gpu/i915/tests/display/gen9-fake-hw.[ch]` に切り出した（`i915_gen9_fake`・`i915_gen9_fake_io`・`_get`・`_set`・`_reset`・`_add_dp_sink`・`_add_hdmi_sink`・`_written_at`）。`host-gen9-modeset-test.c` はこれを使い、`run-gen9-modeset-host-test.sh` は `gen9-fake-hw.c` も build する。`sh plan/ws237/tests/run-gen9-modeset-host-test.sh` → `gen9_modeset_host_test: 80 checks, 0 failures`（切り出しの前と同じ）。
- baseline の build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws237-p004 build/ws237-p004/vmunix` rc 0、warning 0（source の変更の前）。

P3 が決めた設計（上の「再開の手順」を細かくした物、未実装）:

1. 純粋な部分（io の表の上、host で試す）を新しい `display/gen9-outputs.[ch]`、kernel の glue（kthread、buffer、lease、display の op）を `display/gen9-node.[ch]` に分ける（tc.c・tc-kern.c と同じ分け方）。両方を `platform/amd64/vmunix.mk` に足す。
2. node は flip が使える時だけ始める（panel の pipe・transcoder・DPLL・htotal・pixel clock を readout から取る。pixel clock が測れない＝`refresh_measured` が 0 なら外部を点けない、log）。`struct i915_skl_readout` に `htotal`・`port`・`dpll` を足し、`drv_i915_gen9_flip_panel()` の accessor で渡す。
3. 外部の pipe は panel の pipe 以外、DPLL は 1〜3 で panel の DPLL 以外。transcoder は pipe と同じ番号。
4. 最大の pixel clock = min(今の CD clock（`drv_i915_skl_cdclk_khz`、CD clock は変えない）, HDMI 300000 / DP は sink の rate × lanes ÷ 3)。4096×2160 を超える mode は使わない（display window の予算）。
5. latency: PCODE `GEN9_PCODE_READ_MEM_LATENCY`（0x6、data0 = 0 で level 0〜3、1 で 4〜7、各 8 bit）→ Linux 6.8.12 `skl_watermark.c` 3299〜3397 行（MIT、sha256 477dd46f…96f7）の adjust_wm_latency: 0 の level より上を 0、level 0 が 0 なら有効な level に +2、level 0 に +1（`intel_dram.c` 669 行の DIMM 不明時の既定、sha256 da8d8c6b…0b50）、KBL・CFL・CML で `DISP_ARB_CTL2`（0x45004）bit 3（IPC）が立っていれば 0 でない level に +4（同 712〜732 行）。`drv_i915_skl_wm_latency()` として host で試す。
6. DDB の付け替え（`drv_i915_skl_wm_compute` は pipe の番号順に幅の重みで分ける）: 点灯は既に灯っている pipe を昇順に書き直し（WM・DDB を書き、PLANE_SURF を今の値で書き戻して arm、`CURBASE`（0x70084 + 0x1000×pipe、i915_reg.h 3060 行）も今の値で書き戻して cursor の DDB を arm、vblank を待つ）→ 新しい pipe の WM → `drv_i915_skl_output_on`。消灯は `drv_i915_skl_output_off` の後に残りを降順に書き直す。灯っている pipe の plane 2・3（CTL 0x70280・0x70380、BUF_CFG 0x7037c・0x7047c、SURF 0x7029c・0x7039c、i915_reg.h 3585〜3749 行）が無効なら BUF_CFG を 0 にして SURF の書き戻しで arm。
7. 排他: node の mutex → `rd->mutex` の順だけ（panel の SURF の書き戻しが panel の flip と競合しないよう、点灯・消灯の間は rd の mutex も取る）。rd の mutex を持ったまま node の mutex を取らない。
8. display id: 1 = panel、2〜4 = port B〜D（1 + port）。query の数 = 1 + 繋がっている外部の数。外部の generation は接続ごとに進める。外部の lease は `((uint64_t)port << 56) | 連番` で panel と重ならない。events は node の sequence（初期値 1、接続の変化ごとに +1 して `poll_notify()`）。
9. hotplug: kthread が 1 秒ごとに SDEISR（`drv_i915_skl_ports_connected`）を読み、変化した port だけを probe（PW2 を上げ（`drv_i915_skl_pw2_on`、上げたら下げない）、DPCD が読めれば DP、読めなければ GMBUS の EDID で HDMI）。抜けた port が点いていれば消灯して lease を無効にする。
10. 外部の present: claim で buffer 2 枚（`drv_i915_scanout_create`・`_pin`、mode の大きさ）、最初の present で点灯、GPU の blit → SURF を書く、SURFLIVE を tick ごとに読んで latch を待つ（割り込みは使わない）。release・session close で消灯して buffer を unpin・destroy（plane が読んでいる物は abandon）。display window は modeset の段で `drv_i915_skl_flip_window_pages(4096*2160*4)` × 2 を足して取る。
11. fake hw に足す物: PLANE_SURFLIVE（読むと同じ pipe の PLANE_SURF の値）。node の host 試験 `tests/display/host-gen9-outputs-test.c`・`plan/ws237/tests/run-gen9-outputs-host-test.sh`（probe の DP・HDMI・無し、pipe と DPLL の割り当て、latency の解読、点灯・消灯の DDB の順、hotplug の変化）。

残り: 上の 1〜11、`bootfb.c` のつなぎ込み（query・mode・claim・release・present・wait・events・session close・fini を id ≥ 2 で node へ）、build（warning 0）、p005、ws.md に SV7 の素の起動の段ごとの確かめの手順。
