# WS031 E-114／E-115 前半 報告: 一枚表示の経路 — step の実本体への接続と、model 上の統合試験

2026-09-19。台帳 `plan/ws031/results-ws031.md` E-114、E-115 前半。path は `agent-1:~/zedBSD/` 基点。
制約の維持: GPU = vfio-pci、HAL（10 ms tick）非変更、execlists、累積修正保持、git commit／push なし。**実機への表示 register 書込みは今回も 0 件**（実機は未実行。LCD-B はまだです）。

## 0. 結論
| 提出物 | 状態 |
|---|---|
| 接続結果 | 経路上の未移植 step **45 → 1**。enable 列は **step 0**。残りは plane の watermark／DDB（`skl_write_plane_wm`）だけ |
| 統合経路 | 一つの modeset object の上で、正本の `hsw_crtc_enable` → plane noarm／arm → `icl_plane_disable_arm` → `hsw_crtc_disable` が同じ state と buffer で走る。disable は enable の逆順ではなく**正本の disable 側の呼出し元**から取り込み |
| 試験 | host 統合試験 **47/0**（正常開始／停止、前半失敗 3 種、arm 後の停止異常、範囲外の拒否）。本番関数＋register／sink model |
| 実機 | **未実行**。残り = watermark／DDB、commit 外側（CRTC power domain／DC_OFF／CDCLK／DBUF）、kernel binding、GPU-free ktest |

## 1. 構成 — recorder を手順再生器にしていません
```
同じ正本由来の呼出し元・callee・state（parity/lcd の生成 file）
    │  parity_lcd_ops.h の hook: register / wait / sleep / DPCD / panel power / power domain / lock / error
    ├─ host・GPU-free: lcd_fake_hw.c（PLL lock、DDI idle、DP_TP、TRANSCONF state、scanline、frame counter、plane arm）
    │                  ＋ dp_fake_hw の AUX の先の sink が link training に応答
    ├─ 実機: kernel binding（次）— MMIO、常駐 eDP の AUX／PPS、power_domains.c、実 mutex
    └─ recorder（parity_lcd_trace.c）: どちらの backend にも重ねられる実行 log。再生はしない
```
- **panel power と DPCD は常駐 eDP が実行**します（`parity_edp_panel_op` = 正本の `intel_pps_on/off/vdd_on/backlight_on/off`、`parity_edp_dpcd_write`、`parity_edp_read_dpcd_caps`）。PPS は作り直していません。
- MMIO に出ない状態も同じ object にあります: 採用 rate／lane、`intel_dp->DP`（0x80000002 = Linux の DDI_BUF_CTL_A dump）、選択 PLL と active mask、保有 wakeref（DDI IO、AUX）、training 結果、`crtc->active`、plane armed。診断はそこから読みます。

## 2. step の区分（「同名関数なし」を残り作業数にしない）
| 区分 | 内容 |
|---|---|
| 既存本体へ接続 | PPS、DPCD／AUX → 常駐 eDP。power domain → ops（実機では `power_domains.c`） |
| 正本の本体を取り込み | shared DPLL enable／disable（combo）、DDI clock、main-link AUX power、transcoder clock、combo PHY signal level＋ADL-P buf trans 表、lane power、MSO、**link training 45 関数＋DRM helper 24**、sink D0／source OUI、transcoder enable／disable、pipe misc／chicken／linetime、DMC pipe、infoframes、**PWM backlight 26 関数**、**colour 10 関数**、disable 側一式、plane disable |
| 早期 return → **GUARD** 21 個 | 正本の条件を保持（Type-C、DPCD < 1.3／非 branch、DSC off、FEC off、big joiner、PCON、panel fitter、DP 2.0、port sync、HDCP 要求なし、PSR off、SDP 無効 …）。**成立しなければ error** になり、その run は成功扱いになりません |
| 接続しないと**決定**（理由つきで log に位置が残る） | underrun reporting（割込み未配線。PIPESTATUS は LCD-B で試験側が読む）、`intel_crtc_vblank_on/off`（DRM の software 管理、register 操作なし） |
| 正本で無操作と確認 | `intel_initial_watermarks`（`skl_wm_funcs` に hook が無い）、quirk（`intel_quirks[]` に 0x46a8 の entry なし） |
| **未解決** | `skl_write_plane_wm`（watermark／DDB） |

ご指摘のとおり「機能を使わない ≠ 無操作」の例が出ました: `intel_ddi_mso_configure` は MSO 不使用でも splitter bit を clear する RMW を出し、colour も LUT／CTM なしで BOTTOM_COLOR／GAMMA_MODE／CSC_MODE = 0 を**書き**、`intel_dp_set_infoframes` は DIP の enable bit を落とす write を出します。いずれも本体を走らせています。

## 3. link training
保存した write 列の再生ではなく、通常の本体が sink と対話します。fake sink は「要求する swing／pre-emphasis に達したら CR／EQ を報告する」受信側で、level は実行時に応答から決まります（試験 B3: swing 2／pre-emphasis 1 を要求 → その値で成立）。
- `intel_dp_stop_link_train` が立てる `link_trained` は証拠にしていません（試験 B1 で、失敗しても flag が立つことを確認）。enable 後に **sink の DPCD 0x202〜 を読み**、CR／EQ／symbol lock／alignment を判定します。
- **fallback は未移植**: 正本が要求した時点で error "FALLBACK requested" を記録し、別の rate／lane 数を裏で試しません。
- 対象 panel の事実（採取済み DPCD）: rev 1.1、HBR×2、enhanced framing なし、eDP rev < 1.4（rate select なし）。Linux 設定後は lane 0／1 とも swing 1、status 0x77、align 1。

## 4. 統合試験（`sh plan/ws031/tests/run-lcd-modeset-host-test.sh`、ASan/UBSan）
- **A 正常**: prepare は何も触らない → enable（sink 0x77／align 1。DDI_BUF_CTL、TRANS_DDI_FUNC_CTL 0x8a210002、**TRANS_CLK_SEL_A 0x10000000**、DPLL0 0xcc…／CFGCR0・1、TRANSCONF、**PWM 0x17700** が Linux dump と一致）→ 順序（PLL lock < panel power < DDI clock < training < PIPESRC < M/N < transcoder enable < PPS backlight）→ plane arm 1 回（PLANE_CTL 0x94000000、今回の buffer の GGTT 位置）→ frame counter 進行 → plane 停止（**modeset 自身は buffer を解放可能と宣言しない**）→ disable → 読戻し（pipe off、DDI idle、PLL off、clock gate、sink D3、panel／PWM off）→ 参照返却 → eDP 終了後に保有 0。model が数える順序違反 0。
- **B 前半失敗**: PLL unlock／CR 不成立 → 最初の error がその事実、成功を作らない。正本どおり pipe は有効化される（黒画面）ので disable が必要で、取得済み分が返る。
- **C arm 後の異常**: pipe が止まらない → 正本の wait が error、disable は成功扱いにならず、**buffer は「表示中の可能性あり」のまま**、次の modeset は −16。
- 範囲外（Type-C、tiled fb、hook の欠けた backend）は何も触る前に −22。
実行 log: `handover/increment-results/e115-lcd-modeset-host-trace.txt`（enable 137 操作・step 0）。E-113 の表（`notes/lcd-enable-sequence.md`）には「置換済み」の追記を入れました。

## 5. 生成方式
generator を表駆動にしました（`tools/port_lcd_modeset.json`: 追加関数、新規生成 file、register の root macro）。register 定義は root＋同じ header 内の参照 macro の closure を file 順に抽出、不足 symbol は compile error から自動で root に追加（`tools/lcd-e114/find_missing.py`）。`check_generated.sh` は generator の全出力を再生成して byte 比較（全一致）。既存の lcd 56/0、dp 72/0、kernel build（-Werror）通過。

## 6. 次（実機前の条件は増やしません）
1. watermark／DDB: `skl_watermark.c` の計算部（約 1300 行）を取り込み。入力（WM latency、DBUF slice、CDCLK）は通常初期化が既に保持。比較先 = Linux dump の PLANE_WM 0x80004010／PLANE_BUF_CFG 0x0fdb0000。
2. commit 外側: CRTC power domain（PIPE_A／TRANSCODER_A）、DC_OFF、CDCLK が mode に足りることの確認、DBUF。
3. kernel binding（`parity_lcd_kernel.c`）→ GPU-free ktest（**実 scanout object** と結合: IN_USE 中の解放拒否、停止未確認なら abandon）。
4. 実機前判定（未解決 step 0／対象外の根拠／scanout pin 済み／開始時 hardware 状態／fake callback 無し）→ **LCD-B 実機 1 回**（buffer 読戻し、frame counter、PIPESTATUS、link status、写真、停止と回収）。
compiler 側の拡張と大きな整理は LCD-B まで後ろに置いています（既存の拒否・回帰試験は保持）。
