<!-- awesome-plan project=zedbsd record=ws051-p004b -->

# ws051-p004b: USB-C の DP への出力（display の UAPI の claim・present）

Phase ID: `ws051-p004b`
Parent: [WS051](../ws.md)
Status: in-progress（2026-10-07 P1: ws113-p011a の merge（9036a7ad4）の後に code。D4+D5 を実装と host 試験。続きは D1〜D3・D6・D7）
Phase disposition: normal
Queue: q847（P1）の続き。承認: ユーザー 2026-10-07「UCSIとDP alt modeってもう動いてるんですか？シェーダコンパイラより優先してほしいです」（Q1 経由）。設計の先行と code の順は Q1 2026-10-07。

## 目的

Keiland が libvulkan の Display の拡張から display の UAPI（`GPU_DISPLAY_CLAIM`・`MODE`・`PRESENT`）で USB-C の DP の display（TC1・TC2 の DP-alt）を
選んだ時、i915 がその port で link training・modeset・scanout をして画面を出す。GOP の出力先以外へ自分の判断で scanout しない（Guardrail の規則）。

## 前提（2026-10-07 の main c7ead0ec2 を読んだ事実）

- 出力は 1 つ。resident の run（`modeset.c` の `drv_i915_lcd_kernel_resident_run`）が `display->output`（`internal.h` の `struct i915_display_output`:
  `hdmi`・`none`・`state`）で eDP（DDI A）か HDMI（DDI B、pipe B・transcoder B、DPLL 0、空の pool から）を点ける。最初の present で window に入り modeset、
  release の後 10 秒 hold して stop の道で消す（`present.c`）。
- p011a（P2、設計）: claim の対象が resident でない接続済みの connector の時、lease が無ければ worker が window を出て `display->output` を対象に替え、
  connector ごとの ID・generation を進めて lease を渡す（点けるのは最初の present）。点けられない interface（DP・USB-C の DP-alt）は `EOPNOTSUPP`
  （「WS051 の後」）。失敗は元の resident に戻して `EIO`。release の hold が lease 無しで終われば GOP の出力に戻す。resident の connector が抜けたら present は
  `ENXIO`、GOP の出力へ戻す。
- p004a（merge 済み c7ead0ec2）: TC の port ごとの外部 DP の object と sink の cache（`dp-ext.h` の `struct i915_dp_ext_sink`: DPCD、downstream、共通の rate の
  表と最大、lane の最大（sink・LTTPR・VBT・FIA）、EDID、HDMI の有無、downstream の TMDS・dot clock）、`drv_i915_dp_ext_configure_converter`、
  `drv_i915_dp_ext_mode_valid`。TC の AUX の転送は本物（aux.c の Type-C 分岐）。hotplug の DP の detect で EDID は connector の slot と topology に入る。
- p003（cleared）: DKL PLL の計算・enable・disable・readout（`drv_i915_dkl_pll_calc`・`i915_dkl_pll_funcs`・`drv_i915_dkl_pll_describe`）、TBT PLL の記述、
  `drv_i915_icl_compute_tc_phy_dplls`・`drv_i915_icl_update_active_dpll`、DDI の TC の clock（`i915_icl_ddi_tc_*`）、DKL の buffer translation と
  signal levels、`icl_program_mg_dp_mode`、FIA の lane 数、`intel_tc_port_get_link`・`put_link` の hook。`drv_i915_lcd_ms_bind_encoder` は TC の PHY に TC の
  clock の hook を付ける。lcd の world の DPLL の pool は combo の 2 つだけ（`clock.c` の `drv_i915_lcd_dpll_pool_bind`）。
- link training（`dp.c`）: `drv_i915_dp_start_link_train` は Linux の通り LTTPR を non-transparent にし（`i915_dp_init_lttpr_and_dprx_caps`）、全 PHY を
  train する。失敗の fallback は未移植（`i915_dp_schedule_fallback_link_training` は `intel_dp_get_link_train_fallback_values` の STEP で「値が無い」を返し、
  modeset retry の work を STEP として記録するだけ）。
- N1 の takeover（`takeover.c` の `drv_i915_n1_readout`）の registry は bound の 1 つの encoder（panel か HDMI）と connector だけを読む。
- 新しい出力の DPCD は modeset の emit の hook（`i915_lcd_emit` の `dpcd_read`・`dpcd_write`・`read_dpcd_caps`）経由で、今は eDP の AUX に固定
  （`modeset.c:1924` の `drv_i915_edp_emit_*`）。

## 設計

### D1. 出力の種類（p011a の `display->output` の上に）

`struct i915_display_output` に TC の DP を足す: `dp_ext`（nonzero: TC の port の DP）、`tc_port`、`port`（DDI D+n）、`hpd_connector`（connector の index）、
`state`（mode・link・PLL）、`dkl`（TC PLL の 8 語、`state.pll` は combo の語なので別に）。p011a が `hdmi` の bool を種類の enum（`I915_OUTPUT_EDP`・`HDMI`・
`DP_EXT`）にするなら、その enum に `DP_EXT` を足す（**p011a への依頼 R1**）。

### D2. claim の判断と付け替え（p011a の口をそのまま使う）

p011a の claim の判断の表の「点けられない interface（DP）→ `EOPNOTSUPP`」を次に置き換える:

- connector が DP で Type-C の port（`drv_i915_tc_kern_port_of(port) >= 0`）で、外部 DP の port が bind 済み → 点けられる。それ以外の DP（combo の DP）は
  `EOPNOTSUPP` のまま。
- 付け替えの step（p011a の worker の item の中、window の外）で `i915_output_dp_ext(display, connector)`（新、`output.c`）を呼ぶ: (1) その port を probe し直す
  （`drv_i915_dp_ext_probe`。claim の時の sink を正にする。disconnected → `ENXIO`）、(2) sink の写しを取る（新 `drv_i915_dp_ext_sink_copy(display, port, &sink)`、
  `dp-ext-kern.c`）、(3) mode を選ぶ（下の D3）、(4) link の構成と TC PLL を計算して `display->output.state`・`dkl` に置く。失敗は p011a の通り元の resident へ
  戻して `EIO`（mode が link に入らない時は `ENOSPC`）。
- 付け替えの口が「connector の種類ごとの準備の関数」を呼ぶ形（例 `i915_output_retarget(display, connector)` が eDP・HDMI・DP_EXT へ分岐）であること
  （**p011a への依頼 R2**）。

### D3. mode と link の構成（新 `state.c` の `drv_i915_lcd_compute_dp_ext`、kernel に依存しない計算）

- mode: Keiland が `GPU_DISPLAY_MODE` で選んだ mode があればそれ、無ければ EDID の preferred（`drv_i915_edid_preferred_mode`）。列挙は今の topology の
  preferred の 1 つのまま（mode の列挙の拡大は WS113 の契約の範囲）。
- 検査: downstream の上限（`drv_i915_dp_ext_mode_valid`、branch の TMDS・dot clock）。bpp は EDID の bpc（無ければ 24）を downstream の max_bpc と 8 bpc で
  抑える（HDMI の adapter は 8 bpc RGB、DSC・YCbCr は範囲外）。
- link: Linux 6.8 の `intel_dp_compute_link_config_wide` の順（bpp を上から、rate を低い方から、lane を少ない方から、最初に mode が入る組）。rate は sink の
  cache の `common_rates`（`fallback` の上限で切る、D6）、lane は `max_lanes`（同）。M/N は `drv_i915_link_compute_m_n`、SST の overhead は eDP と同じ。
- PLL: TC PLL n（DKL）の 8 語を `drv_i915_dkl_pll_calc(rate, DP, ref)`（p003 で 5330 の正解値と一致）、TBT PLL は予約だけ（D4）。
- 入らない → `ENOSPC`（偽の成功を返さない）。

### D4. DPLL の pool に TC PLL と TBT PLL（`clock.c`）

- `drv_i915_lcd_dpll_pool_bind` の pool を Linux の `adlp_plls` の並びに広げる: DPLL 0・DPLL 1（combo）、TBT PLL（id 2、`drv_i915_tbt_pll_describe`）、TC PLL 1〜4
  （id 3〜6、`drv_i915_dkl_pll_describe`）。`num_shared_dpll` を 7 に。`drv_i915_lcd_ms_release_pipe`・`drv_i915_lcd_dplls_reset` の「2」の固定を pool の数に。
- 予約: TC の port は Linux の `icl_get_tc_phy_dplls` の通り TBT PLL（port_dpll DEFAULT）と TC PLL n（MG_PHY）の 2 つを取り、`drv_i915_icl_update_active_dpll`
  （p003）が DP-alt で MG_PHY を active にする。run の params は `dpll_id = 3 + tc_port`、`reset_dplls = 1`（1 出力の間は空の pool から、HDMI と同じ）。
- 順序の制約（p003 の phase.md の「範囲の外」）: pool に TC PLL を足すと N1 の sanitize が、GOP が panel と USB-C を clone した時の TC の pipe の PLL を「使われて
  いない」と見て止めうる。よって D5 の registry の拡大と**同じ commit** で入れる。

### D5. N1 の registry に TC の encoder（`takeover.c`）

- 今の registry は bound の encoder 1 つ。これを「bound の encoder ＋ 宣言された TC の port ごとの readout だけの encoder」に広げる: TC の port ごとに
  `struct intel_digital_port` を registry の中に持ち、`drv_i915_lcd_ms_bind_readout`（`get_hw_state`・TC の `get_config`（`i915_icl_ddi_tc_get_config`）・
  `get_power_domains`・`sync_state`）を付け、`for_each_intel_encoder` の walk（takeover-internal.h）が全部を回る。connector も port ごとに 1 つ（DP）。
- 効果: firmware が TC の port に出していた pipe は、readout で encoder と TC PLL が対応付き、sanitize に止められず、takeover の `intel_crtc_disable_noatomic`
  で Linux の順で止まる（DDI の disable、TC の clock、`put_link`（readout の link を返す）、DKL PLL の disable）。clone でない時は TC の encoder は inactive で
  何もしない。
- GOP が USB-C だけに出していた時の引き継ぎ（判定を「引き継ぐ」に替える、M5）は p004c のまま（ここでは registry と pool だけ）。

### D6. link training の fallback（M3、`dp.c`）

- Linux 6.8 の `intel_dp_get_link_train_fallback_values` を移す: 今の rate が common_rates の最小でなければ一段下の rate（lane はそのまま）、最小なら lane を
  半分にして最大の rate、lane 1 で失敗したら値が無い。新しい上限は外部 DP の object に `max_link_rate`・`max_link_lane_count` として置く（sink の cache とは
  別、hotplug の長い pulse（p005a の detect）で戻す、Linux の `reset_link_params`）。
- Linux は modeset retry の uevent で userspace に modeset させるが、zedBSD の resident の run は driver の中で完結する。よって run が training の結果を見る:
  `i915_dp_schedule_fallback_link_training` が emit の新しい hook `link_train_failed(ctx, rate, lanes)` で失敗を記録 → resident の run は enable の後に記録を見て、
  失敗なら stop の道で消し、上限を下げて D3 を計算し直し、もう 1 度 run する（組の数は rate 8 × lane 3 以下で有限）。mode が入らなくなった・値が無い →
  present に `EIO`、p011a の失敗の道（GOP の出力へ戻す）。Keiland は別の出力を選ぶ（driver は自分で panel に戻して scanout を始めない、resident に戻すだけ）。
- 成功の確かめ: training の後に link status（`drv_i915_drm_dp_dpcd_read_phy_link_status`、既にある）を 1 度読み、CR・EQ・interlane align を log に。

### D7. modeset の DP 側の入力と hook

- `drv_i915_lcd_kernel_fill_cfg` の DP_EXT の分岐（`i915_resident_dp_ext_cfg`、HDMI の `i915_resident_hdmi_cfg` と同じ形）: port（DDI D+n）、pipe・transcoder B
  （HDMI と同じ。1 出力の間、DBUF・watermark の検証済みの組）、`dpll_id`、`aux_ch`（`display->tck.aux_ch[tc_port]`）、saved_port_bits（TC は lane reversal 無し）、
  DP の half の `dpcd`・`downstream_ports`・rate の表を sink の写しから、backlight 無し、`is_edp = 0`。
- DPCD の hook: `drv_i915_lcd_kernel_bind_ops` が出力が DP_EXT なら `ops.dpcd_read`・`dpcd_write`・`read_dpcd_caps` を外部 DP の port の AUX へ（新
  `drv_i915_dp_ext_emit_dpcd_read` ほか、`dp-ext-kern.c`、ctx は run、port は `display->output.tc_port`）。panel の op（PPS・backlight）は呼ばない（Linux の
  `intel_pps_*` は非 eDP で何もしない）。
- `intel_dp_configure_protocol_converter`（`modeset-internal.h` の GUARD、branch で error）を本物に: emit の新しい hook `dp_configure_converter(ctx)` →
  `drv_i915_dp_ext_configure_converter`（p004a）。
- TC の port の link: pre_enable の `intel_tc_port_get_link(dig_port, lane_count)`（p003 の hook、tc.c の get_link）で PHY を lane 数で取り、FIA の lane 数・
  DP_MODE（p003）、post_disable の `put_link`（p002b）で返す。scanout の間は link が PHY を保つ（p004a の probe は link を足して返すだけで干渉しない）。
- power: DDI_IO_TCn と DDI_LANES の domain は今の encoder の domain の計算、AUX_USBC は Linux の `intel_ddi_get_power_domains` の TC の分岐（DP-alt で AUX の
  domain を持つ）を確かめて足す。

### D8. 抜けと事象（p005a・p005b との境）

- 抜けの検出と `GPU_DISPLAY_EVENT_CHANGE` は p005a（hotplug の長い pulse → detect → topology）。scanout 中の TC の connector が抜けた時の停止（present を
  `ENXIO`、window を出る、GOP の出力に戻す）は p011a の「切断」の口（resident の connector の切断）を使う。その口が topology の connector の状態の変化から
  呼ばれること（**p011a への依頼 R3**: 切断の判定を connector の種類に依らない形に。TC の DP の connector の切断も同じ口に届く）。IRQ_HPD の retrain は p005b。

## p011a への依頼（Q1 経由で P2 へ）

- R1: `display->output` の種類を enum にするなら `DP_EXT` を足せる形に（`hdmi` の bool の追加の分岐でなく）。
- R2: 付け替えの step を connector の種類ごとの準備の関数への分岐にし、DP の分岐は今は `EOPNOTSUPP` を返す 1 か所に（p004b がそこを埋める）。
- R3: 切断（resident の connector が抜けた）の判定を connector の種類に依らない形に（topology の connected の変化）。
- R4: resident の run の params の選び方（`i915_resident_hdmi_params` の分岐）を出力の種類で分ける 1 か所に（p004b が DP_EXT の params と cfg を足す）。

## 試験（code の後）

- host: D3（mode・bpp・rate・lane の選び方を Linux の順と 5330 の DP-2（HBR2 x4、1920x1280 164.36 MHz → 162000 x4 か 270000 x2 …の最初の組）で、ENOSPC）、
  D6（fallback の値の列、組が尽きる所）、D4（pool の並びと id、`dpll_id` から TC PLL n）、claim の判断の表の DP の行（p011a の host 試験に足す）。
- kernel の build（warning 0）、`I915_TESTS=y` の execution・display・display_ktest の build。ktest: P5A-DPLL の pool の数、N1 の registry の encoder の数。
- 実機（5330 の素の起動、TC2 に DP の monitor、ユーザー）: native の probe（`userland/tests/display-control` か p011a の probe）で DP-2 を claim・present → monitor に
  絵、eDP は消灯（1 出力）。release の 10 秒後に eDP（GOP）へ戻る。monitor を抜くと present が `ENXIO`、eDP へ戻る（p005a の後）。USB-C→HDMI の adapter
  （branch）でも同じ。可能なら pin D（2 lane）の adapter。

## 依存

p004a（cleared の後）、ws113-p011a の merge（R1〜R4）。p011（同時 2 出力）は不要（1 出力の付け替えで出す。同時の 2 つは p011 の後に TC の出力も
その資源の検査に乗る）。p004c（GOP が USB-C の時の引き継ぎ）はこの後。

## 記録

### 2026-10-07 D4+D5（同じ commit、base main c4a1bc8ef）

- D4（`clock.c`・`modeset-internal.h`）: pool を `I915_LCD_DPLL_POOL_SIZE` 7（DPLL 0・1、TBT PLL、TC PLL 1〜4、adlp_plls の並び、place = id）に。
  `drv_i915_lcd_dpll_pool_bind` は TBT を `drv_i915_tbt_pll_describe`、TC を `drv_i915_dkl_pll_describe` で、`num_shared_dpll` 7。
  `drv_i915_lcd_ms_release_pipe`・`drv_i915_lcd_dplls_reset` の「2」を pool の大きさに。combo の割り当て（`drv_i915_lcd_ms_alloc_pll` の mask）は不変。
- D5（`takeover.c`・`takeover-internal.h`・`ddi.c`）: registry の walk を「bound の encoder ＋ VBT が宣言した TC の port ごとの readout の encoder」に
  （`encoders[]`・`connectors[]`・`encoder_count`）。TC の port の object は takeover world の `tc_ms[4]`（`struct i915_lcd_modeset`、DDI の hook は
  `ms` の container を前提にするため）で、encoder は INTEL_OUTPUT_DDI・port TC1+n・lanes/IO の power domain・AUX channel、connector は DisplayPort。
  hook は新 `drv_i915_lcd_ms_bind_port_hooks`（`drv_i915_lcd_ms_bind_encoder` から world の bound の部分を分けた）と `drv_i915_lcd_ms_bind_readout`。
- design との差（D5 を成り立たせるために足した物）:
  1. takeover の noatomic の disable の encoder の walk: 今の `drv_i915_encoders_disable`・`_post_disable`・`_post_pll_disable` は world の bound の
     （eDP の）encoder の hook を、どの crtc にも走らせていた。`struct intel_atomic_state` に `encoder_at`・`walk` を足し、takeover の state は
     registry の walk を名乗る → crtc に載る encoder（Linux の old state の connector の walk と同じ意味）の hook を、その encoder の object の
     connector state で走らせる。walk の無い state（modeset の object の）は今のまま。
  2. AUX の routing: DPCD の macro は aux を評価していなかった（全部が backend の＝eDP の AUX）。`struct i915_lcd_aux_emit`（internal.h）と
     object の `aux_emit`（cfg の `aux_emit` から）を足し、DPCD の helper は aux の container の object が名乗る hook を使う（NULL なら今の
     backend）。TC の readout の object は `drv_i915_dp_ext_aux_emit`（dp-ext-kern.c、port の AUX、port の停止の後は -EIO）を、無ければ全部を
     断る hook を持つ。これで takeover の disable の `DP_SET_POWER` D3 が eDP でなく TC の sink に行く。D7 の「bind_ops の DPCD の hook を出力が
     DP_EXT なら外部 DP へ」はこの object ごとの routing で置き換える（resident の run の DP_EXT の cfg に `aux_emit`）。
  3. `bind_port_hooks` の encoder の power domain: TC の port は `POWER_DOMAIN_PORT_DDI_LANES_TC1 + n`（今の「LANES_A + port」は TC で LANES_D 以降を
     指していた。combo は不変）。
- 未対応で残す（記録）: TC の readout の encoder の `sync_state` の `intel_tc_port_sanitize_mode` は今の step のまま（tc.c の readout が firmware の
  DDI buffer の有効な port に link 1 を持つ: p002b）。takeover の crtc の power domain の確かめ（pipe.c の encoder mask の walk は world の only encoder
  だけ）に TC の lanes の domain は入らない（takeover の間は INIT の参照が well を保つ。Linux との差は参照の数だけ）。

| コマンド（D4+D5） | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |
| `I915_TESTS=y I915_TEST_SET=display`・`execution` の vmunix | 成功 warning 0 |
| `sh plan/ws051/tests/host-dkl.sh` | 51 checks 0 failures（新 `test_pool`: 7 PLL、id = place、名前、hook、pipe の release、reset と再 bind） |
| `sh plan/ws051/tests/host-n1-tc.sh`（新） | 22 checks 0 failures（registry の walk、state の walk の disable が crtc の encoder だけを object の connector state で、walk の無い state は bound、DPCD の routing） |
| `sh plan/ws051/tests/host-dpext.sh`・`host-tc.sh`・`host-vbt-pll.sh` | PASS（82・90+34・3） |
| `python3 plan/tools/style-check.py`（変えた file の前後） | 同じ数か減（clock 40、ddi 107、takeover 59、dp-ext-kern 1、modeset-internal.h 10→9）、新しい試験 0 |
| `git diff --check` | 問題無し |

- 試験の直し: `host-dkl-stubs.c` に `drv_i915_worker_wake` と boot の parameter の 3 つ（ws113-p011a の後に present.c・output.c が link に入った）。
  同じ理由で `plan/ws084/tests/run-native-decide-host-test.sh` と `plan/ws118/tests/run-tgl-display-host-test.sh` も link で落ちる（P1 の範囲外、Q1 へ）。
- 未実施: 実機（clone の takeover、T1 は WS の最後）。
