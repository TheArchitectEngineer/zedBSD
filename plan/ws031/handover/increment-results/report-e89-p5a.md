# WS031 第E-89報：P5-0 サーベイ（リスク解消）＋P4 の実ギャップ修正＋P5-a 完了

計画の P5-0 → P4補強 → P5-a まで進みました。**HAL は変更していません。** GPU-free **223/0**、実機 frontier = `intel_setup_outputs`。

## 1. P5-0 実機サーベイ — 計画のリスク2が実測で解消しました

診断のみ（電源ゲート付き読出し、状態変更なし）で1回実行した結果です。

```
pipe A..D: TRANSCONF=0x00000000  TRANS_DDI_FUNC_CTL=0x00030000  PLANE_CTL(1)=0x00000008
DDI A/B/TC1..TC4: DDI_BUF_CTL=0x00000080
PLL DPLL0/1, TBT, TC1..4: ENABLE=0x00000000
wells: total=30 on=8 on_unused=0
```

**enable ビット（いずれも b31）が全部 clear** です。つまりこのデバイスは完全に静止しています。帰結：

- `intel_sanitize_crtc` は全 pipe で `!hw.active` により早期 return。→ **`intel_crtc_disable_noatomic` / `hsw_crtc_disable`（フル modeset disable、+2〜3 DMC）は不要**。**判断①の範囲で P5 が完結する見通し**です。
- `sanitize_dpll_state` は全 PLL off で return、`power_domains_sanitize_state` は `on_unused=0` で対象なし。→ **P5-d で実 HW を OFF にする操作は発生しない見込み**（計画で最大リスクとしていた箇所です）。
- on=8 wells は always_on + PW_1 + PW_2 + PW_A..D + DC_off で、いずれも INIT 参照が保持しているため refcount>0。整合しています。

## 2. P4 に実ギャップを見つけて修正しました（HAL非変更）

計画で予告した補強ですが、実際にはもっと明確な**バグ**でした。P4 は `GEN8_PIPE_VBLANK` などを IER で **enable 済み**なのに、ハンドラが DE_PIPE / DE_PORT / DE_MISC / DE_HPD / SDE の IIR を読まず ack していませんでした。いずれかの source が上がれば **master 線が立ちっぱなし＝割込みストーム**になる状態です。

`gen11_display_irq_handler` → `gen8_de_irq_handler` を移植しました：DISPLAY_INT_CTL を 0 に gate → 各 IIR を read → **非ゼロなら同値を write-back して ack** → 種別を計数 → DISPLAY_INT_CTL 再 enable。master bit が立っているのに IIR が 0 の場合は正本同様「lied」として計数し ack しません。bottom half（vblank/flip/underrun 報告/HPD/AUX）は P5+ の範囲なので、**ack して計数するだけ**であることをコードに明記しています。

## 3. P5-a（probe_nogem 前段）

`intel_wm_init` 〜 `intel_vga_disable` を正本順に実装。**device 表から確定した値（推測していません）**：

| 項目 | 確定値 | 注意点 |
|---|---|---|
| num_scalers[pipe] | 2 | ver>=11 |
| num_sprites[pipe] | **4** | ver>=13。**1 pipe = primary + 4 sprite + cursor = 6 plane** |
| has_hti | **無し** | xe_lpd は設定せず（RKL/ADL-S のみ）→ `intel_hti_init` は no-op、HDPORT_STATE を読まない |
| HAS_HW_SAGV_WM | 真 | → **wm num_levels = 6**（8 ではない） |
| adlp_plls | **7本** | DPLL0/1(combo) + TBT + TC1..4(dkl) |
| intel_ddi_crt_present | false | ver>=9 |

その他：wm latency は PCODE `0x6` を data0=0/1 で**2回**読んで 8 段復号 → `adjust_wm_latency`（0段落ち／read_latency 加算／16GB DIMM WA）。gmbus は ICP pin 表 9件 + GMBUS0/4 reset、**i2c adapter は core 不在のため作らず「未実装」を記録**（hdcp component / acpi fwnode も同様）。`intel_vga_disable` は正本と同じく vga.c 側へ（legacy IO アクセサがそこに閉じているため）。VGA 定数は実カーネル `video/vga.h` で確認しました。

### 実機結果

```
P5a wm: levels=6 latency=3/54/83/102/147/147/144/144 valid=1 sagv_status=1 block_time=35us
P5a objects: crtcs=4 planes/crtc=6 scalers=2 dplls=7(mgr=1) gmbus_pins=9 pps_base=0x61200 gmbus_base=0xc0000
P5a hw: max_cdclk=652800 nssc_ref=38400 adlp_wa=1 hti_read=0 vga_already_off=1 vga_disabled=0 writes=2
attach end: reached=P3 outcome=BLOCKED where=intel_setup_outputs err=0
```

- **wm latency は実機 PCODE の実値**です。level 6,7 が 144 のまま＝raw level0 が 0 で read_latency(3) が **level 0..5 にだけ**加算された＝**num_levels=6 のゲートが実機で効いている**実証になりました。
- `vga_already_off=1`：VGA_DISP_DISABLE は既に立っており正本同様 early return（rombar=0 で GOP が IGD を駆動していないことと整合）。
- `writes=2` は gmbus reset のみ。**ADL-P の WA 2件は対象ビットが既に目的の状態**で書込み不要でした（正本 `intel_uncore_rmw` も差分なしなら書きません）。読み値が all-ones でないことは、clear 側が無書込みで済んだ事実から逆に裏付けられます。

## 4. 試験

GPU-free **212 → 223 / 0 failures**（IRQ-ACK 5件、P5-a 11件）。
- IRQ-ACK：全 source 同時 assert の decode、ack が「読んだ IIR 値の write-back」であること、gate off→on 順、lied 経路、未 assert source を触らないこと。
- P5A：WM(2取引＋adjust 3規則) / DPLL(7本、ver>=14 では表を捏造しない) / CRTC(6 plane・plane_ids_mask=0x9f) / MAXCDCLK / WA(2 rmw、非ADL-P は無書込み) / VGA(既 disable なら IO に触れない／未 disable なら get→out→in→out→put の後にレジスタ)。

**自分で見つけて直した試験側の誤り**：IRQ-ACK でセットアップの書込みが書込みトレースに残り誤検出していました（`f.wt_n=0` の位置）。コードではなく試験の問題でした。

## 5. 次

P5-b（`intel_setup_outputs` = `intel_ddi_init` の判定部＋encoder レコードまで）→ P5-c（readout）→ P5-d（sanitize）。P5-0 の結果により P5-d は当初想定より軽くなる見込みです。

ご指示がなければこのまま P5-b に進みます。
