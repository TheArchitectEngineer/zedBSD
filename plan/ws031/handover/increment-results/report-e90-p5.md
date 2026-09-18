# WS031 第E-90報：**P5 完成** — 実機で `intel_display_driver_probe_nogem()` 完走、frontier は P6

P5-b / P5-c / P5-d を完了しました。**HAL は変更していません。** GPU-free **243/0**、実機 `attach end: reached=P3 outcome=BLOCKED where=i915_gem_init err=0`。

**判断①（readout+sanitize 優先）の範囲で P5 が閉じました。** P5-0 で予測したとおり、フル modeset disable は不要でした（`crtc_disable_noatomic needed: 0`）。

## 1. 実機結果

```
P5b setup_outputs: vbt_children=3 ddi_init=3 encoders=2 skipped=1 crt_present=0
P5b encoder[0]: port=A phy=0 tc=0 clk=1 pd=17 dvo=0x00 dev_type=0x1004 dp=1 hdmi=0
P5b encoder[1]: port=B phy=1 tc=0 clk=1 pd=18 dvo=0x01 dev_type=0x14   dp=1 hdmi=1
P5b ddi skip[0]: port=2 reason=3
i915: parity WARN Platform does not support port C

P5c [CRTC:A..D] hw state readout: disabled ×4
P5c [ENCODER port A/B] hw state readout: disabled pipe_mask=0x0 mst=0
P5c DPLL0/1/TBT/TC1..4 hw state readout: pipe_mask 0x0, on 0
P5c readout: crtcs=4 active_pipes=0x0 planes_visible=0 encoders_linked=0 dplls_on=0

i915: parity [ENCODER port A] is disabled with an ungated DDI clock, gate it
P5d sanitize: vblank_resets=4 dmc_pipes=0 vblank_on=0 fbc_deact=0 enc_clk_gated=1
              dplls_disabled=0 wells_disabled=0 cmtg_wa=0 early_was=0 wm_read=1
              (crtc_disable_noatomic needed: 0)

attach end: reached=P3 outcome=BLOCKED where=i915_gem_init err=0
```

**注目すべき2点：**

1. **sanitize が実機で実際に1件是正しました。** PORT_A は encoder が disabled なのに DDI クロックが ungated で残っており、正本どおり gate しました（`enc_clk_gated=1`）。P5-0 の予測は「全部 no-op」でしたが、**DDI クロックだけは予測から漏れていた実在の不整合**でした。これは「BIOS/pre-OS が残した状態を driver が正す」という P5 の存在意義そのものが実機で1回働いた例です。
2. **P5-c の readout が P5-0 サーベイを独立に裏付けました**（全 CRTC/encoder/PLL が disabled）。別経路・別コードで同じ結論に到達しています。

## 2. 正本どおりの帰結として報告すべきこと

**VBT の3子デバイスのうち1つは正当に棄却されます。** `init_vbt_missing_defaults()` は PORT_A/B/C の3件を生成しますが、**ADL-P の port_mask に PORT_C は含まれません**。したがって3件目は `assert_port_valid()` で弾かれ、正本も `drm_WARN` を出します。**encoder は2本**（A = eDP 相当 `dev_type=0x1004`、B = DP+HDMI `0x14`）が正解です。実機ログの `WARN Platform does not support port C` はバグではなく、正本と同じ挙動です。

## 3. 実装で取り違えやすかった箇所（すべて正本で確認）

- **`dvo_port_to_port` は ver>=13 で xelpd マップ**を使い、TC ポートは `HDMIF..HDMII / DPF..DPI` を取ります。pre-xelpd の `HDMIC/HDMID` と取り違えると TC が誤マップされます。
- **`TC_CLK_OFF(tc_port)` のビット位置は連続ではありません**（TC_PORT_4 以降が bit21 から再開）。等間隔と仮定すると静かに壊れます。
- **`TRANSCONF` は PIPE オフセット**（`0x70008 + pipe*0x1000`）で、transcoder オフセットではありません。DSI transcoder のレジスタも等間隔ではない（0x6b000 / 0x6b800）ので専用テーブルにしています。
- xe_lpd の panel transcoder は **DSI_0/DSI_1 のみ**（`TRANSCODER_EDP` は runtime mask に無い）。
- `intel_sanitize_plane_mapping` は **ver>=4 で return**、`adlp_cmtg_clock_gating_wa` は **ADL-P A0..B0 かつ DPLL0 のみ**（D0 の本機では不発）。
- **`power_domains_sanitize_state` は fresh な `is_enabled()` 読み、`__intel_display_power_is_enabled` は cached** ——正本のこの使い分けをそのまま保持しています。

## 4. 明示的に「未実装」として記録したもの（静かな成功にしていません）

- **`intel_crtc_disable_noatomic`（フル modeset disable）**：到達条件は「active かつ encoder 無し」のみ。本機では発生しませんが、発生したら黙って無視せず **WARN + フラグ**を立てます。
- **readout の詳細フィールド**：color config / DSC / VRR / bigjoiner / scaler 詳細 / output_format / linetime / pixel_multiplier / framestart_delay。いずれも sanitize の判断に入りません。
- **connector 不在**（判断①）ゆえ、connector 起点の sanitize 分岐は発火しません。
- P5-b のスコープ外：DRM encoder 登録 / DP・HDMI サブ初期化 / AUX / HPD / connector 生成。
- `intel_crtc_initial_plane_config`（BIOS fb 引き継ぎ）は判断①によりスキップ。`ilk_wm_sanitize` は ver>=9 guard で正本も return。

## 5. 試験

GPU-free **229 → 243 / 0 failures**。特に：
- **P5C-READOUT に「pipe B active」fixture** を用意し、timings/PIPESRC/plane visible/encoder link/DPLL on を実測値で固定（実機は静止しているので、この経路は fixture でしか踏めません）。
- **電源 OFF を「inactive」と誤読しない**ことを別チェックで固定（power-gated と記録されること）。
- P5D-ACTIVE：active pipe では DMC+vblank を入れ、**未実装の disable_noatomic を偽装せずフラグを立てる**ことを固定。

## 6. 現在地と次

- 台帳 E-90 追記済み。frontier は **P6 `i915_gem_init` → `intel_gt_init`**。
- P6 はご承認済みの判断②に従い **execlists 先行**（`enable_guc=0`）。**WA表 / MOCS / SSEU / RC6 / context image** という、保留中の EU hang の本命候補が集まる段です。
- P6 は工程表で ≈3〜4 DMC と見積もっています。P5 と同様に小増分へ分割して進めます。

ご指示がなければ P6 の計画に着手します。
