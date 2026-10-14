<!-- awesome-plan project=zedbsd record=ws237-p004 -->
# ws237-p004: 段 (3) mode set と外部の display（DP・HDMI、hotplug）

Status: in-progress — register の順と計算は実装・host で確かめ済み（aa3250e12・cafce9894）。node（呼び出し側）は未着手。2026-10-14 Q1 の指示で BUG-312 を先にするため中断。
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
