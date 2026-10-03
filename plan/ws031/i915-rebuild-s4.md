# i915 再構築 S4 — 表示の分担・名前・順序

[実施計画](i915-rebuild-plan.md)、[共通規則](i915-rebuild-rules.md)（§6 表示）、[設計](i915-refactoring-design.md) §2・§3.4・§5.5、
[レビュー](i915-refactoring-review.md) A02・A09・A14、[関数台帳](i915-refactoring-functions.md)。
旧ソース: `src/drivers/gpu/i915-old/parity/`（読むだけ）。新: `src/drivers/gpu/i915/display/`。
作成日: 2026-09-22。本書は S4 の基盤（共有ヘッダ）と、関数移植を並行で進めるための分担・名前・順序を固定する。

## 1. 調査で分かったこと（A14 の実測）

旧表示の全 87 翻訳単位（lcd/、dp/、vbt/、表示の parity/*.c、probe.c、legacy_shim.c、irq.c）を
`clang -E -dM`／`-E`／`-M`（本番構成 `-DPARITY_RESIDENT=1 -DPARITY_RESIDENT_DISPLAY=1`）で展開し、
TU ごとのマクロ定義・型定義・include・file-scope 変数（`llvm-nm -S`）を突き合わせた。

### 1.1 Linux 環境は 5 種類ある

旧 Linux 移植コードは、TU ごとに別の compat header 群の上でコンパイルされていた。同じ Linux 型名が環境ごとに別 layout を持つ。

| 環境（新ヘッダ） | 旧 compat | 旧 TU |
| --- | --- | --- |
| modeset（`modeset-internal.h`、上に `watermark-internal.h`・`takeover-internal.h`） | `lcd_compat.h` ＋ `lcd_modeset/seq/plane/flip/dp/wm_compat.h`、`n1_compat.h`、`parity_lcd_modeset_int.h` | lcd の `intel_*_port.c`・`skl_*_port.c`・`drm_dp_*`・`drm_modes*`・`drm_edid_mode_port.c`（計 29）、`parity_lcd_modeset.c`、`parity_lcd_calc.c`、`parity_lcd_observe.c`、`parity_lcd_regs.c` |
| dp（`dp-internal.h`、`vbt.h` の上） | `dp_compat.h`（`vbt_compat.h` を含む） | `dp/parity_edp.c`、`intel_pps_port.c`、`intel_dp_aux_port.c`、`drm_dp_helper_port.c`、`drm_edid_port.c` |
| vbt（`vbt.h`） | `vbt_compat.h` | `vbt/intel_bios_port.c` |
| hotplug（`hotplug-internal.h`） | `hpd_compat.h` | `intel_hotplug_port.c`、`intel_hotplug_irq_port.c`、`intel_ddi_hotplug_port.c`、`intel_dp_connected_port.c`、`intel_gmbus_port.c`、`intel_hdmi_detect_port.c`、`drm_probe_detect_port.c`、`drm_connector_status_port.c` |
| opregion（`opregion-internal.h`） | `opregion_compat.h` | `intel_opregion_port.c`、`intel_acpi_port.c` |
| 中立（`internal.h` だけ） | なし | `power_domains.c`、`cdclk.c`、`display_core.c`、`display_nogem.c`、`display_state.c`、`dmc.c`、`dram_bw.c`、`combo_phy.c`、`bios.c`、`vga.c`、`pch.c`、`native_*.c`、`opregion_*.c`、`driver_probe.c`、`drm_device.c`、`resident_display.c`、`legacy_shim.c`、`probe.c`、`irq.c`、`parity_lcd_kernel.c`、`parity_lcd_show.c`、`parity_lcd_trace.c`、`scanout.c`、`dp/parity_dp_kernel.c` |

型 layout の衝突（26 名）: `struct drm_i915_private`（5 種）、`struct intel_connector`（4）、`struct drm_device`（4）、
`struct intel_dp`・`intel_digital_port`・`intel_encoder`・`drm_encoder`・`drm_connector`・`drm_connector_state`・`work_struct`（各 3）、
`struct drm_display_mode`・`edid`・`drm_edid`・`drm_dp_aux`・`intel_panel`・`intel_hdmi`・`delayed_work`・`mutex`、
`enum pipe`・`intel_display_power_domain`（3）・`drm_dp_dual_mode_type`、`typedef u64/s64/s8/ktime_t/intel_wakeref_t`。
特に modeset 環境の `struct mutex { int which; }` はカーネルの `struct mutex` と同名で、`internal.h`（`<kern/lock.h>`）と共存できない。

**結論**: 一つの TU は最大一つの Linux 環境しか include しない。各環境ヘッダは `I915_DISPLAY_LINUX_WORLD` で自分を名乗り、
別環境と同じ TU に入ると `#error` で止まる（`internal.h` の冒頭に規約）。layout の統一（A14 の「単一定義へ統合」）は
移動の後の別段階とする（§9）。

### 1.2 意味の違う同名マクロ（A14）

`clang -dM` の比較で、展開の違う名前が 191、表記だけの違い（`1<<31`／`1u<<31`、`_MMIO(x)`／`xu` 等）を除くと **95**。
マクロと関数で食い違う名前がさらに 8（`to_i915`、`dp_to_dig_port`、`dp_to_i915`、`intel_de_rmw`、`intel_phy_is_tc`、
`intel_port_to_phy`、`kmemdup`、`str_on_off`）。扱い:

| 区分 | 名前 | 新しい扱い |
| --- | --- | --- |
| 表記違いで同じ値・同じ意味 | `REG_FIELD_PREP`／`REG_FIELD_GET`（除算とシフト）、`_PICK_EVEN_2RANGES`（`BUILD_BUG_ON_ZERO`=0 の有無）、`_MMIO_PORT`（`_PORT`=`_PICK_EVEN`）、`container_of`、`INT_MAX`・`UINT_MAX`、`i915_mmio_reg_offset/valid`（マクロと inline） | `internal.h` の Linux base に一つだけ定義（理由をコメントに記録） |
| 評価回数だけ違う | `min`（hotplug は二重評価。使用 3 箇所は GMBUS の長さで、第 2 引数は定数か表示版数を読むだけの `gmbus_max_xfer_size()`） | `internal.h` の一回評価版に統合 |
| 型の綴りだけ違う | `u64`／`s64`（hotplug は `unsigned long long`／`long long`） | `uint64_t`／`int64_t` に統合。hotplug の `%llx` 等の書式は移植時に確認（hotplug 担当） |
| 意味が違う | 残り全部: ログ（`drm_dbg_kms`・`drm_err`・`drm_WARN*`・`WARN_ON`・`MISSING_CASE`・`DRM_DEBUG_KMS`・`parity_vbt_log`）、MMIO（`intel_de_read/write/_fw/posting_read/wait_for_register`、`intel_de_rmw`）、待ち・時間（`msleep`・`usleep_range`・`wait_for`・`wait_for_us`・`jiffies`・`msecs_to_jiffies`）、lock（`mutex_lock/unlock`・`spin_lock_irq`・`lockdep_assert_held`・`drm_modeset_lock`）、電源（`intel_display_power_get/put/put_async`）、work（`INIT_WORK`・`INIT_DELAYED_WORK`・`queue_work`・`queue_delayed_work`・`cancel_*_sync`）、機種（`DISPLAY_VER`・`IS_ALDERLAKE_P`・`IS_TIGERLAKE`・`IS_DISPLAY_VER`・`HAS_DDI`・`DISPLAY_RUNTIME_INFO`）、iterator（`for_each_intel_crtc_in_pipe_mask`・`for_each_intel_encoder`・`for_each_intel_connector_iter`）、接続（`drm_connector_get/put`・`drm_connector_list_iter_*`・`intel_attached_encoder`・`to_intel_connector`・`enc_to_dig_port`・`intel_digital_port_connected`）、その他（`clamp`・`clamp_t`・`kfree`・`ssize_t`・`intel_has_quirk`・`intel_tc_port_in_tbt_alt_mode`・`DP_PANEL_REPLAY_ENABLE`・`PANEL_REPLAY_CONFIG`・`PARITY_I915_EXTRA_*`） | **Linux 名では定義しない**。環境ごとに明示名 `i915_<env>_<name>()`（関数にできるもの）／`I915_<ENV>_<NAME>`（式・条件を取るもの）。旧 global（`parity_lcd_cur_i915`、`parity_lcd_wm`、`parity_hpd_irq_saved`、n1 registry）で装置を見つけていたものは明示引数を取る（A09）。環境接頭辞: `lcd`、`wm`、`takeover`、`dp`、`vbt`、`hpd`、`opregion`。対応表は各環境ヘッダの担当報告（§2.2） |
| errno | `EINVAL`=22 等を Linux 番号に再定義（lcd／hotplug／dp で番号の組も違う） | 再定義しない。関数境界は zedBSD の正の errno（規則 §3）。Linux の数値が観測される所（ログ、Linux dump との比較、Linux hook の戻り）だけ `I915_<ENV>_E<NAME>` 定数を使う |
| 別 TU では定数、ここでは関数 | `DISPLAY_VER`: modeset は `parity_lcd_display_ver()`（装置から）、dp／vbt は 13 固定 | 明示名で両方残す。dp／vbt の固定 13 は Tiger Lake で誤る（§8 未決 3） |

iterator の実際の意味（使用箇所での展開を確認）:

| 旧マクロ | 旧 TU:行 | 使用箇所で効いていた定義 |
| --- | --- | --- |
| `for_each_intel_crtc_in_pipe_mask` | `intel_ddi_port.c:1654`、`intel_display_port.c:857`、`intel_modeset_setup_port.c:771` | n1 registry 走査。ただし mask は定数 0（big joiner なし）で本体は走らない |
| 同 | `intel_modeset_setup_port.c:88,193,280–289` | n1 registry 走査（takeover の本当の走査） |
| 同 | `skl_watermark_port.c:1998` | `parity_lcd_wm` が選んだ一つの crtc |
| 同（`lcd_modeset_compat.h:97` の空走査） | — | **どの使用箇所でも効いていない**（同ファイル末尾の `n1_compat.h` が上書き）。A14 の「modeset で空走査」は実際には「registry 走査・mask 0」 |

同じ理由で、watermark 版の `for_each_intel_plane_on_crtc`・`intel_atomic_get_crtc_state` と、`lcd_seq_compat.h` の `intel_crtc_for_pipe`（NULL 版）も
どの使用箇所でも効いていなかった（`n1_compat.h` が先に定義）。新ヘッダでは明示名で残し、コメントに「効いていなかった」と記した。

### 1.3 旧コードの食い違い（直さずに XXX で残す。規則 §4）

1. `display_nogem.c:585–600` の `DVO_PORT_*` が Linux（`intel_vbt_defs.h`）と違う: DPG 16/15、HDMIG 15/16、DPH 20/17、
   HDMIH 16/18、DPI 21/19、HDMII 17/20（旧／Linux）。ポート G 以降の VBT child を誤判定する。ADL-P の A/B/TC1–4 では到達しない見込み。
2. `display_nogem.c:1367` の `DISABLE_DPT_CLK_GATING (1u << 22)` を `TRANS_CMTG_CHICKEN` に書く。Linux v6.8 は `REG_BIT(1)`
   （`lcd_mreg_i915_reg.h:103` も bit 1）。ADL-P の WA（`adlp_cmtg_clock_gating_wa` 相当）で別 bit を立てている。
3. hotplug 環境の `enum intel_display_power_domain` は `{ DISPLAY_CORE, GMBUS }`（GMBUS=1）で、他環境の値（GMBUS≠1）と違う。
   `parity_hotplug_glue.inc:196,204` が明示的に中立 enum へ変換しているので実害はない（中立 enum と Linux v6.8 enum は
   `_Static_assert` で全値一致を確認済み）。
4. dp／vbt 環境の `DISPLAY_VER` は 13 固定、`IS_TIGERLAKE` は 0 固定。Tiger Lake（display 12）で DP／VBT の分岐が ADL-P 扱いになる。

## 2. 作ったヘッダ

### 2.1 `display/internal.h`（どの環境にも依存しない）

1. **Linux base**: `u8`…`s64`、`i915_reg_t`、`_MMIO`、`INVALID_MMIO_REG`、`REG_BIT`、`REG_GENMASK`、`REG_FIELD_PREP/GET`、`BIT`、
   `_PICK_EVEN(_2RANGES)`、`_MMIO_PORT`、`ARRAY_SIZE`、`DIV_ROUND_UP`、`container_of`、`min/max/min_t/max_t`、
   `i915_mmio_reg_offset/valid/equal()`。全環境で同じ意味だったものだけ（§1.2）。
2. **表示の中立な型**: 旧の中立ヘッダ（`power_domains.h`、`cdclk.h`、`display_core.h`、`dmc.h`、`dram_bw.h`、`display_state.h`、
   `pch.h`、`display_nogem.h`、`driver_probe.h`、`drm_device.h`、`vga.h`、`combo_phy.h`、`vbt/parity_vbt.h`、`bios.h`、
   `opregion_service.h`、`opregion_vbt.h`、`native_precheck.h`、`dp/parity_edp.h`、`lcd/parity_lcd_{ops,calc,observe,trace,modeset,show,kernel}.h`、
   `lcd/scanout.h`、`dp/parity_dp_kernel.h`、`lcd/parity_hotplug.h`、`lcd/parity_opregion.h`、`lcd/parity_n1.h`、`resident*.h`）の
   型・enum・定数を、所有ファイル別の節に並べた（関数宣言は含めない。§4 の各担当が所有ヘッダに書く）。
   改名は機械的: `struct parity_x`→`struct i915_x`、`PARITY_X`→`I915_X`、`osdep_x`→`i915_x`、
   `parity_kworkqueue/kwork/kdelayed/ktimerq/kcompletion`→`i915_workqueue/work/delayed_work/timer_queue/completion`。フィールド名は不変。
   試験 build の knob（`PARITY_LCDB_*`、`PARITY_HDMIB/DUAL_WINDOW_MS`、`PARITY_RESIDENT(_DISPLAY)`）は持ち込まない。
3. **旧 `irq.h` の表示半分**: `struct i915_irq_vblank`（旧 `parity_irq_vblank`）と `struct i915_display_irq`（旧 `parity_irq_dev` の表示フィールド。
   GT 半分は S1 の `struct i915_irq_dev`）。`i915_irq_display_ops` の context はこれ。
4. **resident 表示の型**: `i915_resident_display`（旧 `resident_display.c` の `rd`）、`i915_present_window`（旧 `legacy_shim.c` の `shim` の表示フィールド）、
   `i915_lcd_kernel`（旧 `parity_lcd_kernel.c` の `struct lcd_kernel`、`lk`）。
5. **`struct i915_display`**（device が所有、§3）。

### 2.2 環境ヘッダ（private、互いに排他）

| ヘッダ | 環境 | 中身 | 環境の状態構造体 |
| --- | --- | --- | --- |
| `modeset-internal.h` | modeset | `lcd_compat.h` ほか modeset 系 compat の型・定数・hook（明示名） | `struct i915_lcd_world` |
| `watermark-internal.h` | modeset の上 | `lcd_wm_compat.h`（選択 pipe の走査は明示引数） | `struct i915_wm_world` |
| `takeover-internal.h` | modeset の上 | `n1_compat.h`（registry 走査は明示） | `struct i915_takeover_world` |
| `vbt.h` | vbt | `vbt_compat.h`（`struct drm_i915_private` は定義しない。VBT parser の layout は `vbt.c` の file-local 型） | `struct i915_vbt_world`（`vbt.c` 内で定義） |
| `dp-internal.h` | dp（`vbt.h` の上） | `dp_compat.h`。DP の `struct drm_i915_private` は旧の追加メンバを同じ位置に明記 | `struct i915_dp_world` |
| `hotplug-internal.h` | hotplug | `hpd_compat.h` | `struct i915_hpd_world` |
| `opregion-internal.h` | opregion | `opregion_compat.h` | `struct i915_opregion_world` |

各ヘッダの Linux 名 → 明示名の対応、状態構造体、改名した型は §2.4。各定義の意味と旧の所在は各ヘッダのコメントにある。

### 2.3 `data/display-*.inc`（65 本）

旧の Linux 抽出ヘッダ（`lcd_mreg_*.h`、`lcd_*_regs.h`、`lcd_*_types.h`、`lcd_*_enum.h`、`hpd_*.h`、`edid_ref_types.h`、`lcd_drm_fourcc.h`、
`lcd_i915_fixed.h`、`lcd_ref_inlines.h`、`dp/drm_dp.h`、`dp/intel_pps*.h`、`dp/intel_dp_aux*.h`、`dp/dp_ref_types.h`、
`vbt/intel_bios.h`、`vbt/intel_vbt_defs.h`、`vbt/vbt_ref_types.h`、`opreg_*.h`）を 1 対 1 で移した。
名前は `display-<旧 basename から lcd_ を除き _→->.inc`（例 `lcd_mreg_display.h`→`display-mreg-display.inc`）。
定義は一字も変えない（include guard の改名と、`intel_vbt_defs.h` の `vbt_compat.h` include の除去だけ）。
MIT notice と抽出元（Linux v6.8.12、ファイル、sha256、生成器）を保持し、notice を別ファイルに預けていた 10 本には
その notice を複写した。S1 の `data/i915-*.inc`（v6.19、`_MMIO(x)`→`(x)` 等に書換え）と違い、**`_MMIO`・`REG_BIT` を残す**
（Linux 本文が `i915_reg_t` と `.reg` を前提にするため）。旧生成器（`plan/ws031/handover/tools/port_lcd_calc.py`、
`port_dp_aux_pps.py`、`port_intel_bios.py`）の出力先は旧パスのまま（§8 未決 6）。

ヘッダ内 inline（`lcd_i915_fixed.h` の fixed16、`lcd_ref_inlines.h`、`lcd_dp_helper_inlines.h`、`lcd_link_training_inlines.h`、
`lcd_wm_ddb_types.h`、`lcd_drm_plane_defs.h`、`lcd_drm_fourcc.h`）も当面 data に原文のまま置く。
台帳の移動先（internal.h／dp.c／plane.c／state.c／watermark.c）への移動は、使う側の担当が本文を規約化するときに行う。

### 2.4 環境ヘッダの内容（担当報告の要点）

全ヘッダはカーネルと同じフラグ（`-Wall -Wextra -Werror`）で compile を確認した（`/tmp/disp-hdr-check.sh`: 各環境 7 通りが通り、
別環境の組 20 通りが `#error` で止まる）。旧 compat の `#pragma GCC diagnostic ignored`（unused-parameter/variable/function、
sign-compare、missing-field-initializers、for-loop-analysis、GCC の maybe-uninitialized）は持ち込んでいない。移植する関数は警告なしで通す。

**明示名（主なもの）**。接頭辞のほかは Linux 名をそのまま残す。環境をまたいで同じ意味のもの（dp と vbt のログ以外の機種判定など）は vbt 名を dp でも使う。

| Linux 名 | modeset | dp（vbt） | hotplug | opregion |
| --- | --- | --- | --- | --- |
| `intel_de_read/write/rmw/posting_read/wait_for_register` | `i915_lcd_intel_de_*()`（emit hook） | `i915_dp_intel_de_*()`（`i915_dp_env`） | `i915_hpd_intel_de_*()`（world から装置） | — |
| `mutex_lock/unlock` | `I915_LCD_MUTEX_LOCK(i915, m)`（旧 `parity_lcd_cur_i915`） | `I915_DP_MUTEX_LOCK(m)` | カーネル mutex のまま | — |
| `msleep`・`usleep_range`・`udelay` | `i915_lcd_msleep(i915, …)` ほか | `i915_dp_msleep()` ほか | — | `I915_OPREGION_MSLEEP`（未移植の記録、引数を評価しない） |
| `wait_for`・`wait_for_us` | `I915_LCD_WAIT_FOR(i915, …)` | — | `I915_HPD_WAIT_FOR` | `I915_OPREGION_WAIT_FOR` |
| `spin_lock_irq/unlock_irq` | `I915_LCD_SPIN_*`（no-op） | — | `i915_hpd_spin_lock_irq(lock, &saved)`（旧 `parity_hpd_irq_saved`） | — |
| ログ `drm_dbg_kms`・`drm_err`・`drm_WARN(_ON)`・`WARN_ON`・`MISSING_CASE` | `I915_LCD_*` | `I915_VBT_*`／`I915_DP_*`（数える counter が違う） | `I915_HPD_*` | `I915_OPREGION_*` |
| `DISPLAY_VER`・`IS_ALDERLAKE_P`・`IS_TIGERLAKE` | `I915_LCD_DISPLAY_VER(i915)`（装置の値） | `i915_vbt_display_ver()`（13 固定） | `i915_hpd_display_ver(i915)` | — |
| iterator | `I915_LCD_FOR_EACH_INTEL_CRTC_IN_PIPE_MASK`（旧の空走査。どこでも効いていない） | — | `I915_HPD_FOR_EACH_INTEL_ENCODER(world, i, e)`、`…_CONNECTOR_ITER` | `I915_OPREGION_FOR_EACH_INTEL_CONNECTOR_ITER` |
| （watermark 層） | `I915_WM_FOR_EACH_INTEL_CRTC_IN_PIPE_MASK(wm, crtc, mask)`（旧 `parity_lcd_wm`） | | | |
| （takeover 層） | `I915_TAKEOVER_FOR_EACH_INTEL_CRTC_IN_PIPE_MASK(takeover, crtc, i, mask)`、`…_ENCODER`、`…_CONNECTOR_ITER` | | | |
| errno | `I915_LCD_EINVAL` 22 ほか（正の値）。`I915_LCD_ETIMEDOUT`・`I915_LCD_EIO` は旧 `parity_lcd_ops.h` の負の値（-110、-5）のまま | `I915_DP_E*`（=`I915_EDP_E*`） | `I915_HPD_EIO` 5、`_ENXIO` 6、`_EAGAIN` 11、`_EDEADLK` 35、`_ETIMEDOUT` 110 | `I915_OPREGION_ENOTSUPP` 524 |

旧 global を暗黙に使っていた hook・step（未移植 callee の記録 40 余り、`drm_for_each_encoder_mask`、`schedule_timeout`、`local_irq_*`、
DBUF の accessor、n1 registry の accessor など）は、装置・world・watermark 文脈を明示引数に取る形にした。

**カーネル型との衝突で改名した型**: modeset の `struct mutex { int which; }` → `struct i915_lcd_lock_selector`（同 layout。
実際に変数として宣言していた所はなく、`display.dpll.lock` 等は無名 `{ int which; }`）、`wait_queue_head_t` → `struct i915_wq`、
dp の `struct parity_dp_mutex` → `struct i915_dp_mutex`（`mutex_init` はカーネル関数名なので `i915_dp_mutex_init()`）。
無名だった構造体に名前を付けたもの（world に保持するため）: `i915_dp_display_runtime`、`i915_dp_edp`、`i915_hpd_instance`、
`i915_hpd_edid_slot`、`i915_opregion_map`、`i915_opregion_hal_map`、`i915_opregion_backlight_target`、`i915_opregion_worker_stats`、
`i915_lcd_wm_ctx`、`i915_lcd_modeset`、`i915_n1_registry`。

**layout の確認**: 旧 compat と新ヘッダを両方 compile して sizeof/offset を比べ、全型一致（例 modeset: `drm_i915_private` 216、
`intel_crtc_state` 4152、modeset object 6592。dp: `drm_i915_private` 144（`display.pps` 88、`dp_env` 120）、`intel_dp` 344）。

**環境の状態構造体**

| 構造体 | 所在 | sizeof | 旧変数の合計 | 中身 |
| --- | --- | ---: | ---: | --- |
| `i915_lcd_world` | modeset-internal.h | 32832 | 32811 | `parity_lcd_modeset.c` の `ms_*` pool と `parity_lcd_ver`（初期値 13）、`commit_disable.off_state`、`intel_display_port.c` の `parity_lcd_cur_i915`・`parity_lcd_only_encoder`・emit static、`intel_ddi_port.c`、`intel_dpll_port.c` の pool、`intel_crtc_port.c`、`skl_plane_port.c`、`drm_edid_mode_port.c`、`parity_lcd_calc.c`、`parity_lcd_regs.c` の表（37 個） |
| `i915_wm_world` | watermark-internal.h | 72 | 65 | `parity_lcd_dbuf_dev`、`_valid`、`parity_lcd_wm` |
| `i915_takeover_world` | takeover-internal.h | 19448 | 19448 | `n1`（`struct i915_n1_registry`） |
| `i915_dp_world` | dp-internal.h | 848 | 848 | `edp`、`dp_log_level`、`dp_log_errors` |
| `i915_vbt_world` | vbt.c（file-local、下記） | 49560 | 49556 | `vbt_i915`、`vbt_arena`（48 KiB）ほか 9 個 |
| `i915_hpd_world` | hotplug-internal.h | 19696 | 19636 | `hpd`、`hpd_i915`、work queue 2 本、ports/conns、GMBUS bus、EDID slot（22 個）。model 用 4 個を含む（S5 で外す） |
| `i915_opregion_world` | opregion-internal.h | 704 | 689 | `parity_opregion_*` 20 個。`backend`="NONE"、`policy`=`acpi_backlight_vendor` の初期値を所有者が入れる |

差は 1 byte に詰められていた static int と padding だけ。VBT parser の `struct drm_i915_private`（DP の追加メンバなし、sizeof 88）と
`struct i915_vbt_world` は `vbt.c` の先頭に置く（`vbt.h` と `#define _INTEL_BIOS_PRIVATE` + `data/display-intel-vbt-defs.inc` の後）:

```c
struct drm_i915_private {
	struct drm_device drm;
	struct {
		struct intel_vbt_data vbt;
		struct {
			int edp_vswing;
		} params;
	} display;
};

#define I915_VBT_ARENA_BYTES (48u * 1024u)

struct i915_vbt_world {
	struct drm_i915_private vbt_i915;
	uint8_t vbt_arena[I915_VBT_ARENA_BYTES];
	unsigned vbt_arena_used;
	unsigned vbt_arena_peak;
	unsigned vbt_alloc_failures;
	const void *vbt_provider_bytes;
	struct i915_vbt *vbt_live;
	int vbt_log_level;
	unsigned vbt_log_errors;
	struct intel_panel i915_vbt_init_panel_panel;
};
```

**data の扱いで決めたこと**: `display-mreg-reg-defs.inc`（`_PICK_EVEN_2RANGES` だけ）は `internal.h` の同値の定義と字面が違うので
include しない。`display-ddi-regs.inc`、`display-dp-msa.inc`、`display-mreg-backlight/color/dmc/dmc-c/hdmi-dip.inc` は旧でも .c が直接
include していたので、使うファイルが include する（modeset 環境で compile 確認済み）。`HAS_HW_SAGV_WM`・`HAS_MBUS_JOINING`・`HAS_MSO`
（`display-mreg-display-device.inc`）と `for_each_pipe`（`display-mreg-display.inc`）は Linux 名の `DISPLAY_VER`・`IS_ALDERLAKE_P`・
`DISPLAY_RUNTIME_INFO` を使うので、明示名だけの環境では展開できない。使う担当（P3、P4、P8）が呼出し箇所で明示名に書き下す。

**移植時に直す箇所として見つかったもの**（挙動は変えない。規則 §4 の XXX）:
- hotplug の `u64` を `%llu` に渡す 2 箇所（`intel_hotplug_port.c:239`、`parity_hdmi_detect_glue.inc:85`）は cast が要る（型の綴りが変わったため）。
- hotplug 環境では Linux の `ENXIO`(6) とカーネルの `ENOENT`(6) が同じ値で、`parity_hpd_flush_reenable` の `-ENOENT` が `-ENXIO` に読める。
  `EBUSY` は hotplug ではカーネルの 17 のまま（Linux 16 ではない）。
- hotplug の `mutex_is_locked` は `mutex_owned`（呼出し側が持っているか）へ写されていて、Linux の「誰かが持っているか」と違う。`drm_WARN_ONCE` は毎回出る。
- takeover の readout 用 macro の一部（N1 の step、`intel_display_power_get_if_enabled`・`_in_set`・`_put_all_in_set`）は Linux の `i915` 引数を
  無視して global の装置を使っていた。`with_intel_display_power_if_enabled` は global で取り、引数の装置へ返す。明示形は `cur_i915` と `i915`
  を別引数にして旧のとおり写した（両者が別物になり得る）。
- `display_nogem.c` の `DVO_PORT_*`・`DISABLE_DPT_CLK_GATING`（§1.3）。OpRegion の panel type stub は OpRegion があっても -ENODEV を返す（旧のまま）。
- work API の差: `parity_kdelayed_flush`（hotplug の model hook）に対応する関数が `../workqueue.h` にない。OpRegion glue は
  `I915_WORK_RUNNING`、`drv_i915_work_pending`、`drv_i915_flush_work` へ写す。
- 旧の static 前方宣言（`i915_hpd_poll_init_work` ほか）は header に置けない（`-Wunused-function`）ので、各 .c の前方宣言節へ。
- `intel_opregion_port.c` の約 100 個のマクロ（OpRegion の offset、`MBOX_*`、`ASLE_*`、`ASLC_*`、`SWSCI_*`）は環境ではなく Linux 本文なので
  `opregion.c` に残す（`data/` へ移すかは P2 が決める）。

**ヘッダが宣言だけしている関数**: 環境の glue 関数（旧 `parity_dp_power_get`、`parity_hpd_read`、`parity_n1_crtc_at` など）の宣言は環境ヘッダにあり、
名前は規則 §2 の `drv_i915_*`（例 `drv_i915_dp_power_get`、`drv_i915_hpd_read`、`drv_i915_n1_crtc_at`、`drv_i915_lcd_display_ver`）。
定義は該当パッケージ。環境の型を取らない中立の宣言（`parity_vbt.h`、`parity_edp.h` の関数、`drv_i915_vbt_emit`・`drv_i915_vbt_fmtcheck`、
`drv_i915_register_acpi_notifier`）は所有パッケージの公開ヘッダに書く（§6）。

## 3. `struct i915_display` の構成

device が一つ持つ（`struct i915_device` に `struct i915_display *display` を統合担当が足す。表示を持たない構成は NULL）。

| 群 | フィールド（旧名のまま、`parity_`→`i915_`） | 旧の所在 |
| --- | --- | --- |
| 所有者 | `device` | 新設 |
| probe の static 局所 | `drm_dev`、`vbt_state`、`vga_client`、`power_domains`、`pmdemand`、`cdclk`、`pwc`、`edp_dev`、`dcore`、`dmc_dev`、`dstate`、`pch`、`nogem`、`dprobe`、`dram_info`、`bw_state`、`irq_vblank`、`dmc_wq`・`modeset_wq`・`flip_wq`、`opd`・`p2_opd`、`n0`・`n0_mmio`、`rctx`・`rlcd` | `probe.c:106–239,683,878,2433–2454` |
| probe の局所フラグ | `opregion_present`、`opregion_vbt_present`、`display_core_inited`、`hpd_started`（旧 file static `parity_hpd_started`） | `probe.c` |
| 割込み表示半分 | `irq`（`struct i915_display_irq`） | 旧 `irqdev` の表示フィールド |
| resident 表示 | `rd`、`window`、`lk`、`lcdb_locks`・`lcdb_locks_live`、`resident_buf[2]`、`resident_serve`・`resident_serve_ctx`、`resident_front`、`resident_up`、`resident_run_env`・`resident_run_rep`、`fill_cfg_pn`、`i915_lcd_reg_trace`、`show_trace`・`show_retained`・`show_retained_hw`・`show_gpu_retained`・`show_gpu_retained_gm`・`show_gpu_retained_why` | `resident_display.c`、`legacy_shim.c`、`parity_lcd_kernel.c`、`parity_lcd_show.c` |
| 中立ファイルの file-scope | `i915_vbt_buf`・`opregion_vbt_buf`・`opregion_vbt_size`・`pinned_vendor`・`pinned_device`（bios.c）、`g_dmc_arena`・`g_dmc_arena_used`（dmc.c）、`dc_trace`（display_core.c）、`n0_last`・`read_opregion_copy/vcopy`・`i915_opregion_read_data_opcopy/vbtcopy`（native_precheck.c）、`chain_lock`・`chain_lock_live`・`chain_head`（opregion_service.c）、`next_isa_bridge_cursor`（pch.c）、`g_vga_io`（vga.c） | 各ファイル |
| 環境の状態 | `lcd_world`、`wm_world`、`takeover_world`、`dp_world`、`vbt_world`、`hpd_world`、`opregion_world`（不完全型へのポインタ。環境の所有ファイルが確保・解放） | 環境 TU の file-scope 変数（§2.2） |

各フィールドのコメントに「何を持つか・何が守るか」を書いた。原則: 起動・停止の worker だけが書く。例外は各コメント
（`power_domains` は自身の lock、`rd` は `rd.mutex`、`irq_vblank` は `irq_vblank.lock`、resident 系は serving thread、
`chain_head` は `chain_lock`）。

**本番に入れないもの（tests/display）**: `lcdb_summary`・`lcdb_scanout`、`lcdc_*`・`lcdd_*`・`lcdg_*`・`lcdo_*`・`n1_*`（parity_lcd_kernel.c のシナリオ）、
`parity_*_test_*` の hook（display_state.c、dmc.c）、`g_bridge_test`（pch.c）、`g_vblank_worker_fail_pipe`（drm_device.c）、
ktest／fake HW／fwtest の全変数。

**挙動上の差（一点だけ）**: 旧は static 領域（起動時 0、装置一つ）、新は device 所有。環境の状態は確保が要るので、
`drv_i915_display_init` に確保失敗の経路が一つ増える（ENOMEM で表示なしとして続ける。§8 未決 2）。

## 4. 移動先ファイルの環境分割（台帳からの変更）

台帳の移動先には、二つの Linux 環境の関数を一つのファイルへ集めるものがある。環境の layout を統一するまで
一つの TU にできないので、**環境ごとに別ファイル**にする（統一後に台帳どおり一つへ戻す）。

| 台帳の移動先 | 新ファイル（環境） | 旧ソース |
| --- | --- | --- |
| `dp.c` | `dp.c`（modeset） | `drm_dp_link_port.c`、`intel_dp_link_training_port.c`、`intel_link_port.c`、`drm_dp_bw_port.c`、`lcd_dp_compat.h`・`lcd_dp_helper_inlines.h`・`lcd_link_training_inlines.h` の inline |
| | `dp-sink.c`（dp） | `dp/parity_edp.c`、`dp/drm_dp_helper_port.c`（＋`parity_drm_dp_glue.inc`）、`dp/parity_dp_kernel.c`（中立） |
| | `hotplug.c`（hotplug） | `intel_dp_connected_port.c`（1 関数） |
| `panel.c` | `panel.c`（dp） | `dp/intel_pps_port.c` |
| | `panel-backlight.c`（modeset） | `intel_backlight_port.c`、`parity_backlight_glue.inc`、`parity_lcd_modeset.c` の brightness／backlight 3 関数 |
| | `opregion.c`（opregion） | `intel_acpi_port.c`（2 関数） |
| `edid.c` | `edid.c`（modeset） | `drm_edid_mode_port.c`、`drm_modes_hv_port.c`、`drm_modes_port.c`、`parity_edid_mode_glue.inc` |
| | `edid-read.c`（dp） | `dp/drm_edid_port.c`、`parity_drm_edid_glue.inc` |
| `hdmi.c` | `hdmi.c`（hotplug） | `intel_hdmi_detect_port.c`、`parity_hdmi_detect_glue.inc` |
| | `hdmi-mode.c`（modeset） | `intel_hdmi_mode_port.c`、`parity_hdmi_mode_glue.inc` |
| `irq.c`（表示半分） | `display/interrupts.c`（中立） | 旧 `irq.c` の表示関数（display irq reset/postinstall/handler、de mask、power-well hook、drain、vblank get/put/wait） |

それ以外の移動先は一つの環境（＋中立コード）に収まる: modeset 環境 = power、clock、phy、dmc、state、watermark、takeover、
diagnostics、modeset、pipe、plane、vblank、color、ddi、dp、edid、hdmi-mode、panel-backlight。dp 環境 = dp-sink、aux、panel、edid-read。
vbt 環境 = vbt。hotplug 環境 = hotplug、gmbus、hdmi。opregion 環境 = opregion。中立 = display、present、scanout、interrupts。
中立ファイルは環境ヘッダを include しない。

台帳の試験側分類の訂正（本番が呼んでいるため本番に残す）: `lcd_pattern.c` の `parity_lcd_pattern_fill/verify`
（`parity_lcd_show.c` の本番経路が呼ぶ）→ `diagnostics.c`、`parity_lcd_modeset_discard_model`（同）→ `modeset.c`。

## 5. 作業パッケージ

規模は台帳の旧関数数。全パッケージの共通: 1 関数ずつ規約化して移す（規則 §1–§5）、`plan/ws031/tests/i915-cc.sh` で 1 ファイルずつ確認、
移した関数の「旧ファイル:旧関数 → 新ファイル:新関数」を報告に含める。

| # | パッケージ | 新ファイル（環境） | 旧ソース | 規模 | 使うヘッダ |
| --- | --- | --- | --- | ---: | --- |
| P1 | 電源・クロック・PHY・DMC | `power.c`、`clock.c`、`phy.c`、`dmc.c`（modeset） | `power_domains.c`、`display_core.c`、`intel_display_power_set_port.c`、`driver_probe.c` の電源部、`cdclk.c`、`intel_cdclk_port.c`、`intel_dpll_port.c`＋`parity_cdclk_glue.inc`・`parity_dpll_glue.inc`、`combo_phy.c`、`intel_combo_phy_port.c`、`intel_ddi_buf_trans_port.c`＋`parity_buf_trans_glue.inc`、`dmc.c`、`intel_dmc_port.c`、`parity_lcd_kernel.c` の `k_power_*` | 215 | internal、modeset-internal |
| P2 | VBT・OpRegion | `vbt.c`（vbt）、`opregion.c`（opregion） | `bios.c`、`opregion_vbt.c`、`vbt/intel_bios_port.c`＋`parity_vbt_glue.inc`、`intel_opregion_port.c`＋`parity_opregion_glue.inc`、`opregion_service.c`、`intel_acpi_port.c` | 194 | internal、vbt、opregion-internal |
| P3 | 状態・watermark | `state.c`、`watermark.c`（modeset＋wm） | `display_state.c`、`dram_bw.c`、`parity_lcd_calc.c`、`intel_bw_port.c`＋`parity_bw_glue.inc`、`skl_watermark_port.c`＋`parity_wm_glue.inc`、`intel_wm_port.c`、`parity_lcd_kernel.c` の `panel_mode/size_mm` | 145 | internal、modeset-/watermark-internal |
| P4 | takeover・診断 | `takeover.c`（modeset＋takeover）、`diagnostics.c`（modeset） | `display_nogem.c`、`intel_modeset_setup_port.c`＋`parity_modeset_setup_glue.inc`、`native_precheck.c`、`native_decide.c`、`vga.c`、`parity_lcd_trace.c`、`parity_lcd_observe.c`、`parity_lcd_regs.c`、`lcd_pattern.c` の fill/verify、`parity_lcd_kernel.c` の `log_*`・`n1_*` 本番部 | 176 | internal、modeset-/takeover-/watermark-internal |
| P5 | hotplug・GMBUS・HDMI 検出 | `hotplug.c`、`gmbus.c`、`hdmi.c`（hotplug） | `intel_hotplug_port.c`、`intel_hotplug_irq_port.c`、`intel_ddi_hotplug_port.c`、`intel_dp_connected_port.c`、`drm_probe_detect_port.c`、`drm_connector_status_port.c`、`parity_hotplug_glue.inc`・`parity_ddi_hotplug_glue.inc`、`intel_gmbus_port.c`＋`parity_gmbus_glue.inc`、`intel_hdmi_detect_port.c`＋`parity_hdmi_detect_glue.inc`、`driver_probe.c` の hotplug 部、`resident_display.c` の `rd_events` | 123 | internal、hotplug-internal |
| P6 | eDP sink・AUX・PPS | `dp-sink.c`、`aux.c`、`panel.c`、`edid-read.c`（dp） | `dp/parity_edp.c`、`dp/drm_dp_helper_port.c`、`dp/parity_dp_kernel.c`、`dp/intel_dp_aux_port.c`＋`parity_dp_aux_glue.inc`、`dp/intel_pps_port.c`、`dp/drm_edid_port.c`＋glue、`parity_lcd_kernel.c` の `k_dpcd_*` | 152 | internal、vbt、dp-internal |
| P7 | DDI・DP link・HDMI mode・EDID mode | `ddi.c`、`dp.c`、`hdmi-mode.c`、`edid.c`（modeset） | `intel_ddi_port.c`＋`parity_ddi_emit_glue.inc`、`drm_dp_link_port.c`、`intel_dp_link_training_port.c`、`intel_link_port.c`、`drm_dp_bw_port.c`、`intel_hdmi_mode_port.c`＋glue、`drm_edid_mode_port.c`、`drm_modes*_port.c`＋`parity_edid_mode_glue.inc` | 204 | internal、modeset-internal |
| P8 | pipe・plane・vblank・color・backlight | `pipe.c`、`plane.c`、`vblank.c`、`color.c`、`panel-backlight.c`（modeset） | `intel_display_port.c`＋`parity_display_emit_glue.inc`、`intel_crtc_port.c`、`intel_vrr_port.c`、`skl_plane_port.c`＋`parity_plane_emit_glue.inc`、`intel_atomic_plane_port.c`＋glue、`intel_vblank_port.c`、`parity_flip_glue.inc`、`intel_color_port.c`＋glue、`intel_backlight_port.c`＋glue、`parity_lcd_kernel.c` の `k_vblank_*`・`k_*_event` | 195 | internal、modeset-/watermark-internal |
| P9 | modeset・resident 表示・割込み・起動列 | `modeset.c`（modeset）、`scanout.c`、`present.c`、`display.c`、`interrupts.c`（中立）、統合: `device.c`、`worker.c`、`session.c`、`irq.c` の hook、`i915.h` | `parity_lcd_modeset.c`、`parity_lcd_show.c`、`parity_lcd_kernel.c` の resident 部、`lcd/scanout.c`、`resident_display.c`、`legacy_shim.c` の表示部、`irq.c` の表示半分、`probe.c` の表示段、`drm_device.c` | 130＋統合 | internal、modeset-internal |
| T | 試験（S5） | `tests/display/*`（台帳 B の 16 本） | ktest、fake HW、fwtest、`parity_hpd_test.c`、`lcd_hw_check.c`、`parity_lcd_kernel.c` のシナリオ（`lcd_run_one`、`lcdb/lcdc/lcdd/lcdg/lcdo/lcdr/hdmib/dual/dual_share/n1_run`、`probe_scanout` ほか） | 186 | — |

`parity_lcd_kernel.c`（3349 行）は P1・P3・P4・P6・P8・P9 と T に分かれる（台帳の各行の移動先に従う）。
`k_read32` 等の MMIO 接着は `../mmio.c` の既存関数で置き換え、`k_usleep` は `../sync.c` の既存 wait を使う（台帳）。

## 6. 公開する関数（並行作業のために固定する名前）

- 名前は機械規則: `parity_intel_x`→`drv_i915_x`、`parity_x`→`drv_i915_x`、公開される Linux 名 `intel_x`→`drv_i915_x`、
  その他の Linux 名 `x`（`skl_x`、`icl_x`、`drm_x` …）→`drv_i915_x`。引数・戻り値は旧のまま型名だけ新名（errno は規則 §3 で正に）。
  同じ Linux 名の別実装が二つの環境にある場合は環境名を挟む（`drv_i915_hpd_intel_phy_is_tc` と `drv_i915_lcd_intel_phy_is_tc`、`drv_i915_lcd_drm_mode_set_name` と `drv_i915_vbt_drm_mode_set_name`）。
- 宣言は **所有ファイルの公開ヘッダ `display/<file>.h`**（例 `display/clock.h`）。公開ヘッダは `internal.h` だけを include し、
  中立の型だけで書く（環境の型を引数に取る関数は、同じ環境の担当どうしの間だけで使い、宣言は環境ヘッダ側に置く）。
  `vbt.h` は台帳どおり VBT の Linux 環境なので、`vbt.c` の中立な公開宣言は `display/vbt-parse.h` に置く。
- 設計 §3.4 の H 名（`drv_i915_display_init` ほか）は最終形の入口名。S4 では挙動を変えないので、旧関数を機械名で移し、
  H 名は §7・§8 の入口（display.c と present.c）にだけ使う。
- パッケージをまたいで呼ばれる関数（旧 static 呼出しの越境を含まない、旧 global 関数の呼出し 293 件）の一覧は付録 A。
  各担当は作業の最初に自分の公開ヘッダを書き（付録 A の名前、旧の署名を改名）、他担当はそれを include してよい。

## 7. 起動列（device.c）

S1 の `device.c` は表示段を XXX で飛ばしている（`device.c:253`、`711`、`925`）。S4 では旧 probe の順序を保って、
`display.c` の段階関数を呼ぶ形に戻す（関数名は `display/display.h`）。旧の段と新の呼出し:

| 旧段 | 旧の処理（順序どおり） | 新（device.c から） |
| --- | --- | --- |
| P2.9 | ASLS 読取り、`parity_opregion_read_data`、`parity_opregion_log`、`parity_bios_set_opregion_vbt` | `drv_i915_display_init_opregion(display)`（device.c:711 の XXX の位置） |
| P2 | `parity_dram_detect`、`parity_bw_init_hw` | 同関数の後半（dram_info、bw_state） |
| P3.1–3.5 | `parity_drm_dev_init`、`parity_drm_vblank_init`、`parity_intel_bios_init_ex`（明示 VBT pin 含む）、`parity_intel_vga_register`、`parity_intel_power_domains_init`、`parity_intel_pmdemand_init_early` | `drv_i915_display_init_noirq(display)` 前半 |
| N0 | `parity_vbt_explicit_pin`、`parity_native_precheck`、`parity_native_log`（読むだけ。STOP なら表示なしで以降を飛ばす） | 同関数内 |
| P3.6–3.16 | `parity_intel_init_cdclk_hooks`、`parity_lcd_set_display_ver`、`parity_intel_power_domains_init_hw`、`parity_intel_dmc_init`（dmc_wq）、modeset/flip wq 作成、`mode_config_init`、`cdclk_init`、`color_init`、`dbuf_init`、`bw_init`、`pmdemand_init`、`init_quirks`、`fbc_init` | 同関数後半 |
| P4 | `parity_intel_detect_pch`、`parity_irq_vblank_init`、`parity_intel_irq_install` | `drv_i915_display_irq_bind(display, &gt->irq)` で interim hook を `interrupts.c` の表へ置換してから、S1 の install（device.c:925） |
| P5-0 | survey（診断のみ） | `drv_i915_display_survey(display)`（diagnostics） |
| P5a–d | `parity_intel_display_nogem_front`、`parity_intel_setup_outputs`（`dp_connector_init`＝`parity_edp_device_init_connector`）、`modeset_readout_hw_state`、`modeset_sanitize_hw_state` | `drv_i915_display_init_nogem(display)` |
| （P6 GT） | S1 のまま | — |
| P7 | `parity_intel_display_driver_probe`、`parity_hpd_start`、initial commit の記録、`intel_opregion_register`（VBT_ONLY: runtime 無効）、`parity_i915_driver_register`、`parity_intel_power_domains_enable`（N1 後に延期） | `drv_i915_display_register(display)` |
| resident | forcewake 5 本を持って `rctx`・`rlcd` を組み、`parity_resident_serve` | S1 の `drv_i915_worker_serve()`（§8）。`rlcd` は `display->rlcd` |
| 停止 | `parity_lcd_last_resort_stop`、`parity_hpd_stop`、`parity_i915_driver_unregister`、（GT 停止）、`parity_edp_device_fini`、`parity_intel_display_nogem_fini`、`parity_intel_irq_uninstall`、`parity_intel_display_state_fini`、`parity_intel_dmc_fini`、wq 破棄、`power_domains_driver_remove`、`power_domains_cleanup`、`bios_driver_remove`、`vga_unregister`、`drm_dev_fini`、`parity_lcd_kernel_abandoned()` なら DMA 等を保持 | `drv_i915_display_stop_early`（GT 停止の前）と `drv_i915_display_fini`（後）。abandoned／gpu_retained の判定は S1 の停止へ返す |

forcewake の取得順と逆順解放、`rctx/rlcd` が serving の間ずっと生きること（A02）は、`display` が device 所有になったことで満たす。
enable の帰還後に callback の stack へ依存しない（`lk`・`resident_buf` は device 所有）。

## 8. resident 表示・present 経路（挙動を変えない写し方）

旧の構造（serving loop の反転）をそのまま保つ。機能変更（R2: loop を callback として抱えない）は S4 の後。

```
drv_i915_worker_serve (worker.c, 旧 parity_resident_serve)
  loop: i915_worker_loop(worker, in_display = 0)
        └ PRESENT／PRESENT_BLOB が先頭に来たら ENTER_DISPLAY で戻る（旧 shim_serve）
        drv_i915_present_window(display)            present.c   旧 parity_lcd_kernel_resident_run + shim_display_window
          └ modeset.c: resident_run（preflight、fill_cfg、点灯）→ resident_window
              └ window.display_up = 1; i915_worker_loop(worker, in_display = 1)   ← 表示中はここで全仕事を処理
                   PRESENT       → drv_i915_present_frame（旧 shim_present）
                   PRESENT_BLOB  → drv_i915_present_blob_frame（旧 shim_present_blob; scanout.c の map_panel、render/blit の batch）
                   RELEASE が先頭 → LEAVE_DISPLAY で戻る
                scanout.c: unmap_panel; window.display_up = 0
          └ modeset.c: 停止経路（resident_verify、消灯、解放）
        失敗なら window.display_failed = 1（以後 PRESENT は EIO）
```

| 旧 | 新 | 備考 |
| --- | --- | --- |
| `legacy_shim.c`: `shim_serve(device, in_display)` の表示分岐 | `worker.c`: `i915_worker_loop` に `in_display` 引数と sync kind（BATCH／PRESENT／PRESENT_BLOB／RELEASE）を戻す | P9 が worker.c に追記（S1 の担当と調整） |
| `parity_resident_serve` の ENTER_DISPLAY ループ | `worker.c`: `drv_i915_worker_serve` | 同上 |
| `shim_display_window` | `present.c`: `i915_present_window_serve`（static、serve callback） | |
| `parity_shim_display_present/_blob/_release` | `present.c`: `drv_i915_present`、`drv_i915_present_blob`、`drv_i915_present_release`（sync item を worker へ積んで待つ） | |
| `shim_present`、`shim_present_blob` | `present.c`（worker が表示窓の中で呼ぶ公開関数） | |
| `shim_map_panel`、`shim_unmap_panel` | `scanout.c`: `drv_i915_scanout_map_panel`、`drv_i915_scanout_unmap_panel` | `display->window.map_*` |
| `parity_shim_display_deps` | 廃止（`device->display` を直接） | |
| `resident_display.c`: `rd_init`、`rd_panel`、`rd_query`、`rd_mode`、`rd_claim`、`drv_i915_resident_display_close` | `display.c`（`drv_i915_display_bind_ops`、`drv_i915_display_session_close`） | `session.c:214` の XXX から close を呼ぶ |
| `rd_device_query`、`rd_constraints` | `scanout.c`（scanout sub-ops） | |
| `rd_present`、`rd_wait`、`rd_release`、`rd_release_locked`、`rd_blit_build` | `present.c`（`drv_i915_present_bind_ops`）。`rd_blit_build` は render/blit 契約の呼出し | |
| `rd_events` | `hotplug.c`（固定値のまま） | |
| `parity_shim_blit_source` | `tests/render/readback.c` | |
| `parity_lcd_kernel_resident_run`、`resident_window`、`resident_verify`、`fill_cfg`、`preflight` | `modeset.c` | |
| `parity_lcd_resident_buffer`、`parity_lcd_resident_back` | `scanout.c` | |
| `parity_lcd_resident_flip`、`parity_lcd_modeset_flip` | `present.c` | |
| `k_read32/write32/rmw32/posting_read/wait_reg` | `../mmio.c`・`../sync.c` の既存関数 | |
| `k_dpcd_*`、`k_power_*`、`k_vblank_*`・`k_*_event` | `aux.c`（P6）、`power.c`（P1）、`vblank.c`（P8） | emit hook 表は `modeset.c` が組む |

**試験専用（tests/display、台帳どおり）**: `dp_fake_hw.c`、`edp_ktest.c`、`edp_sync_ktest.c`、`hpd_ktest.c`、`parity_hpd_test.c`、
`lcd_fake_hw.c`、`lcd_modeset_ktest.c`、`lcd_show_ktest.c`、`lcdg_ktest.c`、`scanout_ktest.c`、`lcd_hw_check.c`、`opregion_ktest.c`、
`opregion_fwtest.c`、`parity_lcd_kernel.c` のシナリオ（→ `kernel-scenarios.c`）、`parity_lcd_modeset.c` の model 用 1 関数（§4 の訂正を除く）。
本番から試験への呼出し（付録 A の「tests」節）は S5 で解消する前に、§4 の訂正で本番側へ移す 3 関数以外が残らないことを P9 が確認する。

## 9. 順序

1. **済**: `internal.h`、環境ヘッダ 7 本、`data/display-*.inc` 65 本（本書 §2）。
2. **各パッケージの初日**: 公開ヘッダ `display/<file>.h`（付録 A の名前、旧署名の改名）と、環境の状態構造体を確保・解放する関数
   （`drv_i915_<env>_world_create/destroy`、所有: lcd=P9 `modeset.c`、wm=P3 `watermark.c`、takeover=P4 `takeover.c`、dp=P6 `dp-sink.c`、
   vbt=P2 `vbt.c`、hpd=P5 `hotplug.c`、opregion=P2 `opregion.c`）。
3. P1–P8 は並行。依存は宣言だけ（link は統合時）。P3・P4・P8 は `watermark-internal.h`／`takeover-internal.h` の明示 iterator を使う。
4. P9 は P1–P8 の公開ヘッダが揃ってから `display.c` の段階関数と device.c・worker.c・session.c・irq hook の統合を書く。
5. 統合担当: `platform/amd64/vmunix.mk`、`i915.h` の `display` フィールド、移行表（計画 §5）、E-130 再現（共有 route、GPU copy→flip、40 s、写真）。
6. S4 の後（別段階）: 環境 layout の統一と §4 の分割ファイルの再統合、Linux 名の一括改名、serving loop の解体（R2）。

## 10. 未決事項

1. §4 の環境分割（dp／panel／edid／hdmi を 2〜3 ファイルに分ける）でよいか。代案は layout 統一を S4 の中で先にやること（挙動中立の証明が重い）。
2. 環境状態の確保失敗（ENOMEM）で表示なしとして続けるか、起動失敗にするか。旧は static なので経路がなかった。
3. dp／vbt 環境の `DISPLAY_VER`=13 固定・`IS_TIGERLAKE`=0（§1.3-4）を装置の値にするか（Tiger Lake を支えるなら必要。挙動変更）。
4. §1.3-1・2（`DVO_PORT_*`、`DISABLE_DPT_CLK_GATING`）は XXX で残す。修正は別段階でよいか。
5. `struct i915_lcd_kernel_deps.irq` は旧 `parity_irq_dev *` だった。表示フィールドを `struct i915_display_irq` へ分けたので、
   型を `struct i915_display_irq *` に変える（名前は同じ）。P8・P9 で確認。
6. 旧生成器 3 本の出力先と型名（data/ と環境ヘッダ）を追従させるか、生成を止めて手管理に切り替えるか（R5）。
7. 公開ヘッダを `display/<file>.h` の 1 ファイル 1 本にすると 30 本近くになる。`display/display.h`（device から見える入口）以外を
   `internal.h` の関数節にまとめる案もある（今回は 1 本ずつで固定）。
8. takeover の readout macro が global の装置と引数の装置を混ぜていた箇所（§2.4）を、移動後に一つへ揃えるか。
9. 明示名への書換えで、Linux 本文の差分が大きくなる（`intel_de_read` → `i915_lcd_intel_de_read` 等）。環境 layout を統一した後に
   Linux 名へ戻す（一括改名の段階）か、明示名のまま残すか。

## 付録 A. パッケージをまたいで呼ばれる関数（新名）

旧 global 関数の呼出しを、台帳の移動先（§4 の分割を適用）で集計した。旧 static の越境（台帳 C の 201 辺）はここに含めない:
それは所有ファイル内に呼出し側ごと移すか、各担当が公開に追加して報告する。

#### P1 電源・クロック・PHY・DMC

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `clock.c` | `intel_crtc_compute_min_cdclk`（lcd/intel_cdclk_port.c） | `drv_i915_crtc_compute_min_cdclk` | takeover.c |
| `clock.c` | `intel_disable_shared_dpll`（lcd/intel_dpll_port.c） | `drv_i915_disable_shared_dpll` | pipe.c |
| `clock.c` | `intel_dpll_get_freq`（lcd/intel_dpll_port.c） | `drv_i915_dpll_get_freq` | ddi.c |
| `clock.c` | `intel_dpll_get_hw_state`（lcd/intel_dpll_port.c） | `drv_i915_dpll_get_hw_state` | ddi.c |
| `clock.c` | `intel_dpll_sanitize_state`（lcd/intel_dpll_port.c） | `drv_i915_dpll_sanitize_state` | takeover.c |
| `clock.c` | `intel_enable_shared_dpll`（lcd/intel_dpll_port.c） | `drv_i915_enable_shared_dpll` | ddi.c, pipe.c |
| `clock.c` | `intel_unreference_shared_dpll_crtc`（lcd/intel_dpll_port.c） | `drv_i915_unreference_shared_dpll_crtc` | takeover.c |
| `clock.c` | `parity_icl_dp_combo_pll`（lcd/parity_dpll_glue.inc） | `drv_i915_icl_dp_combo_pll` | state.c |
| `clock.c` | `parity_icl_hdmi_wrpll`（lcd/parity_dpll_glue.inc） | `drv_i915_icl_hdmi_wrpll` | state.c |
| `clock.c` | `parity_intel_max_cdclk_freq`（cdclk.c） | `drv_i915_max_cdclk_freq` | modeset.c |
| `clock.c` | `parity_lcd_dpll_pool_bind`（lcd/parity_dpll_glue.inc） | `drv_i915_lcd_dpll_pool_bind` | takeover.c |
| `clock.c` | `parity_lcd_dplls_reset`（lcd/parity_dpll_glue.inc） | `drv_i915_lcd_dplls_reset` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `clock.c` | `parity_lcd_ms_alloc_pll`（lcd/parity_dpll_glue.inc） | `drv_i915_lcd_ms_alloc_pll` | modeset.c |
| `clock.c` | `parity_lcd_ms_cdclk_check`（lcd/parity_cdclk_glue.inc） | `drv_i915_lcd_ms_cdclk_check` | modeset.c |
| `clock.c` | `parity_lcd_ms_release_pll`（lcd/parity_dpll_glue.inc） | `drv_i915_lcd_ms_release_pll` | modeset.c |
| `dmc.c` | `intel_dmc_disable_pipe`（lcd/intel_dmc_port.c） | `drv_i915_dmc_disable_pipe` | pipe.c |
| `dmc.c` | `intel_dmc_enable_pipe`（lcd/intel_dmc_port.c） | `drv_i915_dmc_enable_pipe` | pipe.c, takeover.c |
| `dmc.c` | `parity_intel_dmc_fini`（dmc.c） | `drv_i915_dmc_fini` | tests/execution/ktest.c |
| `phy.c` | `intel_combo_phy_power_up_lanes`（lcd/intel_combo_phy_port.c） | `drv_i915_combo_phy_power_up_lanes` | ddi.c |
| `phy.c` | `is_hobl_buf_trans`（lcd/intel_ddi_buf_trans_port.c） | `drv_i915_is_hobl_buf_trans` | ddi.c |
| `phy.c` | `parity_lcd_ms_bind_buf_trans`（lcd/parity_buf_trans_glue.inc） | `drv_i915_lcd_ms_bind_buf_trans` | ddi.c |
| `power.c` | `intel_display_power_get_in_set`（lcd/intel_display_power_set_port.c） | `drv_i915_display_power_get_in_set` | pipe.c |
| `power.c` | `intel_display_power_put_mask_in_set`（lcd/intel_display_power_set_port.c） | `drv_i915_display_power_put_mask_in_set` | pipe.c, takeover.c |
| `power.c` | `parity_dbuf_ctl_reg`（display_core.c） | `drv_i915_dbuf_ctl_reg` | tests/lcd-modeset-ktest.c |
| `power.c` | `parity_display_power_async_bind`（power_domains.c） | `drv_i915_display_power_async_bind` | dp.c |
| `power.c` | `parity_display_power_async_work`（power_domains.c） | `drv_i915_display_power_async_work` | dp.c |
| `power.c` | `parity_display_power_flush_work_sync`（power_domains.c） | `drv_i915_display_power_flush_work_sync` | dp.c |
| `power.c` | `parity_display_power_get`（power_domains.c） | `drv_i915_display_power_get` | dp.c, hotplug.c |
| `power.c` | `parity_display_power_put`（power_domains.c） | `drv_i915_display_power_put` | dp.c, hotplug.c |
| `power.c` | `parity_display_power_put_async`（power_domains.c） | `drv_i915_display_power_put_async` | dp.c |
| `power.c` | `parity_gen9_dbuf_slices_update`（display_core.c） | `drv_i915_gen9_dbuf_slices_update` | watermark.c |
| `power.c` | `parity_intel_power_domains_enable`（driver_probe.c） | `drv_i915_power_domains_enable` | tests/kernel-scenarios.c |
| `power.c` | `parity_power_domain_hw_state_on`（power_domains.c） | `drv_i915_power_domain_hw_state_on` | takeover.c |

#### P2 VBT・OpRegion

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `opregion.c` | `intel_acpi_device_id_update`（lcd/intel_acpi_port.c） | `drv_i915_acpi_device_id_update` | opregion.c |
| `opregion.c` | `intel_opregion_setup`（lcd/intel_opregion_port.c） | `drv_i915_opregion_setup` | tests/opregion-ktest.c |
| `opregion.c` | `parity_acpi_notifier_call_chain`（opregion_service.c） | `drv_i915_acpi_notifier_call_chain` | tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_acpi_notifier_count`（opregion_service.c） | `drv_i915_acpi_notifier_count` | tests/opregion-ktest.c |
| `opregion.c` | `parity_acpi_notifier_init`（opregion_service.c） | `drv_i915_acpi_notifier_init` | tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_add_backlight`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_add_backlight` | tests/kernel-scenarios.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_add_connector`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_add_connector` | tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_asle_flush`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_asle_flush` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_cleanup`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_cleanup` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_counters`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_counters` | tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_firmware_setup`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_firmware_setup` | tests/opregion-fwtest.c |
| `opregion.c` | `parity_opregion_gate_counters`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_gate_counters` | tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_gse_entry`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_gse_entry` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_mailbox_backend`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_mailbox_backend` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_mbox_read`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_mbox_read` | tests/opregion-fwtest.c |
| `opregion.c` | `parity_opregion_mbox_write`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_mbox_write` | tests/opregion-fwtest.c |
| `opregion.c` | `parity_opregion_notifier_registered`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_notifier_registered` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_notify_adapter`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_notify_adapter` | tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_register`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_register` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_service_epoch`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_service_epoch` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_service_start`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_service_start` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_set_policy`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_set_policy` | tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_shadow_map`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_shadow_map` | tests/kernel-scenarios.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_shadow_setup`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_shadow_setup` | tests/kernel-scenarios.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_unregister`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_unregister` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_vbt`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_vbt` | tests/opregion-ktest.c |
| `opregion.c` | `parity_opregion_worker_stats_get`（lcd/parity_opregion_glue.inc） | `drv_i915_opregion_worker_stats_get` | tests/kernel-scenarios.c, tests/opregion-fwtest.c, tests/opregion-ktest.c |
| `opregion.c` | `parity_register_acpi_notifier`（opregion_service.c） | `drv_i915_register_acpi_notifier` | tests/opregion-ktest.c |
| `opregion.c` | `parity_unregister_acpi_notifier`（opregion_service.c） | `drv_i915_unregister_acpi_notifier` | tests/opregion-ktest.c |
| `vbt.c` | `intel_bios_hdmi_level_shift`（vbt/intel_bios_port.c） | `drv_i915_bios_hdmi_level_shift` | ddi.c |
| `vbt.c` | `intel_bios_is_valid_vbt`（vbt/intel_bios_port.c） | `drv_i915_bios_is_valid_vbt` | opregion.c |
| `vbt.c` | `parity_opregion_locate_vbt`（opregion_vbt.c） | `drv_i915_opregion_locate_vbt` | takeover.c |
| `vbt.c` | `parity_sha256`（bios.c） | `drv_i915_sha256` | dp.c, takeover.c |
| `vbt.c` | `parity_vbt_emit`（bios.c） | `drv_i915_vbt_emit` | dp.c |
| `vbt.c` | `parity_vbt_encoder_for_port`（vbt/parity_vbt_glue.inc） | `drv_i915_vbt_encoder_for_port` | dp.c, tests/kernel-scenarios.c |
| `vbt.c` | `parity_vbt_init_panel`（vbt/parity_vbt_glue.inc） | `drv_i915_vbt_init_panel` | dp.c, modeset.c |
| `vbt.c` | `parity_vbt_validate`（vbt/parity_vbt_glue.inc） | `drv_i915_vbt_validate` | takeover.c |

#### P3 状態・watermark

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `state.c` | `parity_lcd_compute`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_compute` | dp.c, tests/edp-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `state.c` | `parity_lcd_compute_hdmi`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_compute_hdmi` | tests/kernel-scenarios.c |
| `state.c` | `parity_lcd_emit_cpu_transcoder`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_emit_cpu_transcoder` | dp.c, tests/edp-ktest.c |
| `state.c` | `parity_lcd_emit_ddi`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_emit_ddi` | dp.c, tests/edp-ktest.c |
| `state.c` | `parity_lcd_emit_plane`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_emit_plane` | tests/edp-ktest.c, tests/scanout-hw-check.c |
| `state.c` | `parity_lcd_emit_transcoder`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_emit_transcoder` | dp.c, tests/edp-ktest.c |
| `state.c` | `parity_lcd_error_bind`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_error_bind` | modeset.c |
| `state.c` | `parity_lcd_words_find`（lcd/parity_lcd_calc.c） | `drv_i915_lcd_words_find` | dp.c, tests/edp-ktest.c, tests/scanout-hw-check.c |
| `watermark.c` | `intel_bw_crtc_update`（lcd/intel_bw_port.c） | `drv_i915_bw_crtc_update` | takeover.c |
| `watermark.c` | `intel_dbuf_post_plane_update`（lcd/skl_watermark_port.c） | `drv_i915_dbuf_post_plane_update` | modeset.c |
| `watermark.c` | `intel_dbuf_pre_plane_update`（lcd/skl_watermark_port.c） | `drv_i915_dbuf_pre_plane_update` | modeset.c |
| `watermark.c` | `intel_mbus_dbox_update`（lcd/skl_watermark_port.c） | `drv_i915_mbus_dbox_update` | modeset.c |
| `watermark.c` | `intel_wm_plane_visible`（lcd/intel_wm_port.c） | `drv_i915_wm_plane_visible` | plane.c |
| `watermark.c` | `parity_icl_qgv_bw`（display_state.c） | `drv_i915_icl_qgv_bw` | modeset.c |
| `watermark.c` | `parity_lcd_dbuf_current`（lcd/parity_wm_glue.inc） | `drv_i915_lcd_dbuf_current` | modeset.c |
| `watermark.c` | `parity_lcd_dbuf_forget`（lcd/parity_wm_glue.inc） | `drv_i915_lcd_dbuf_forget` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `watermark.c` | `parity_lcd_dbuf_publish`（lcd/parity_wm_glue.inc） | `drv_i915_lcd_dbuf_publish` | modeset.c |
| `watermark.c` | `parity_lcd_ms_bw_data_rate`（lcd/parity_bw_glue.inc） | `drv_i915_lcd_ms_bw_data_rate` | modeset.c |
| `watermark.c` | `parity_lcd_ms_bw_min_cdclk`（lcd/parity_bw_glue.inc） | `drv_i915_lcd_ms_bw_min_cdclk` | clock.c |
| `watermark.c` | `parity_lcd_ms_wm_compute`（lcd/parity_wm_glue.inc） | `drv_i915_lcd_ms_wm_compute` | modeset.c |
| `watermark.c` | `parity_lcd_ms_wm_compute_off`（lcd/parity_wm_glue.inc） | `drv_i915_lcd_ms_wm_compute_off` | modeset.c |

#### P4 takeover・診断

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `diagnostics.c` | `parity_lcd_kernel_abandoned`（lcd/parity_lcd_kernel.c） | `drv_i915_lcd_kernel_abandoned` | tests/lcdg-ktest.c |
| `diagnostics.c` | `parity_lcd_kernel_gpu_retained`（lcd/parity_lcd_kernel.c） | `drv_i915_lcd_kernel_gpu_retained` | tests/lcdg-ktest.c |
| `diagnostics.c` | `parity_lcd_kernel_summary`（lcd/parity_lcd_kernel.c） | `drv_i915_lcd_kernel_summary` | tests/lcdg-ktest.c |
| `diagnostics.c` | `parity_lcd_observer_frames`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_frames` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_init`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_init` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_point`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_point` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_steady_begin`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_steady_begin` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_steady_end`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_steady_end` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_steady_sample`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_steady_sample` | modeset.c |
| `diagnostics.c` | `parity_lcd_observer_stopped`（lcd/parity_lcd_observe.c） | `drv_i915_lcd_observer_stopped` | modeset.c |
| `diagnostics.c` | `parity_lcd_ref_dbuf_ctl`（lcd/parity_lcd_regs.c） | `drv_i915_lcd_ref_dbuf_ctl` | tests/lcd-modeset-ktest.c |
| `diagnostics.c` | `parity_lcd_reg_by_name`（lcd/parity_lcd_regs.c） | `drv_i915_lcd_reg_by_name` | modeset.c, tests/kernel-scenarios.c, vblank.c |
| `diagnostics.c` | `parity_lcd_trace_init`（lcd/parity_lcd_trace.c） | `drv_i915_lcd_trace_init` | modeset.c, tests/lcd-modeset-ktest.c |
| `diagnostics.c` | `parity_lcd_trace_phase`（lcd/parity_lcd_trace.c） | `drv_i915_lcd_trace_phase` | modeset.c |
| `takeover.c` | `parity_n1_readout`（lcd/parity_modeset_setup_glue.inc） | `drv_i915_n1_readout` | tests/kernel-scenarios.c |
| `takeover.c` | `parity_n1_release`（lcd/parity_modeset_setup_glue.inc） | `drv_i915_n1_release` | tests/kernel-scenarios.c |
| `takeover.c` | `parity_n1_takeover`（lcd/parity_modeset_setup_glue.inc） | `drv_i915_n1_takeover` | tests/kernel-scenarios.c |

#### P5 hotplug・GMBUS・HDMI 検出

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `gmbus.c` | `parity_hpd_gmbus_adapter`（lcd/parity_gmbus_glue.inc） | `drv_i915_hpd_gmbus_adapter` | hotplug.c |
| `gmbus.c` | `parity_hpd_gmbus_forget`（lcd/parity_gmbus_glue.inc） | `drv_i915_hpd_gmbus_forget` | hotplug.c |
| `hdmi.c` | `parity_hpd_edid_bytes`（lcd/parity_hdmi_detect_glue.inc） | `drv_i915_hpd_edid_bytes` | tests/hpd-ktest.c, tests/parity-hpd-test.c |
| `hdmi.c` | `parity_hpd_edid_forget`（lcd/parity_hdmi_detect_glue.inc） | `drv_i915_hpd_edid_forget` | hotplug.c |
| `hdmi.c` | `parity_hpd_edid_info`（lcd/parity_hdmi_detect_glue.inc） | `drv_i915_hpd_edid_info` | hotplug.c, tests/hpd-ktest.c, tests/kernel-scenarios.c, tests/parity-hpd-test.c |
| `hdmi.c` | `parity_hpd_hdmi_connector_funcs`（lcd/parity_hdmi_detect_glue.inc） | `drv_i915_hpd_hdmi_connector_funcs` | hotplug.c |
| `hotplug.c` | `intel_phy_is_tc`（lcd/parity_ddi_hotplug_glue.inc） | `drv_i915_hpd_intel_phy_is_tc` | hotplug 環境の他ファイル（hdmi.c、gmbus.c） |
| `hotplug.c` | `intel_port_to_phy`（lcd/parity_ddi_hotplug_glue.inc） | `drv_i915_hpd_intel_port_to_phy` | hotplug 環境の他ファイル（hdmi.c、gmbus.c） |
| `hotplug.c` | `parity_hpd_connector_name`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_connector_name` | tests/hpd-ktest.c, tests/parity-hpd-test.c |
| `hotplug.c` | `parity_hpd_connector_polled`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_connector_polled` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_connector_status`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_connector_status` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_flush_reenable`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_flush_reenable` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_hotplug_record`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_hotplug_record` | tests/hpd-ktest.c, tests/parity-hpd-test.c |
| `hotplug.c` | `parity_hpd_irq_record`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_irq_record` | tests/hpd-ktest.c, tests/parity-hpd-test.c |
| `hotplug.c` | `parity_hpd_model_allowed`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_model_allowed` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_model_irq`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_model_irq` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_pin_state`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_pin_state` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_probe_connector`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_probe_connector` | tests/hpd-ktest.c, tests/kernel-scenarios.c, tests/parity-hpd-test.c |
| `hotplug.c` | `parity_hpd_retry_bits`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_retry_bits` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_start`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_start` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_stop`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_stop` | tests/hpd-ktest.c |
| `hotplug.c` | `parity_hpd_summary`（lcd/parity_hotplug_glue.inc） | `drv_i915_hpd_summary` | tests/hpd-ktest.c, tests/kernel-scenarios.c, tests/parity-hpd-test.c |
| `hotplug.c` | `parity_intel_hpd_init_pins`（driver_probe.c） | `drv_i915_hpd_init_pins` | tests/hpd-ktest.c |

#### P6 eDP sink・AUX・PPS

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `aux.c` | `parity_intel_dp_aux_init`（dp/parity_dp_aux_glue.inc） | `drv_i915_dp_aux_init` | dp.c |
| `dp-sink.c` | `parity_dp_kernel_bind_sync`（dp/parity_dp_kernel.c） | `drv_i915_dp_kernel_bind_sync` | tests/edp-sync-ktest.c |
| `dp-sink.c` | `parity_dp_kernel_sleep_us`（dp/parity_dp_kernel.c） | `drv_i915_dp_kernel_sleep_us` | sync.c, tests/edp-sync-ktest.c |
| `dp-sink.c` | `parity_dp_kernel_sync_start`（dp/parity_dp_kernel.c） | `drv_i915_dp_kernel_sync_start` | tests/edp-sync-ktest.c |
| `dp-sink.c` | `parity_dp_kernel_sync_stop`（dp/parity_dp_kernel.c） | `drv_i915_dp_kernel_sync_stop` | tests/edp-sync-ktest.c |
| `dp-sink.c` | `parity_drm_dp_aux_init`（dp/parity_drm_dp_glue.inc） | `drv_i915_drm_dp_aux_init` | aux.c |
| `dp-sink.c` | `parity_edp_begin`（dp/parity_edp.c） | `drv_i915_edp_begin` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `dp-sink.c` | `parity_edp_dpcd_read`（dp/parity_edp.c） | `drv_i915_edp_dpcd_read` | aux.c, tests/edp-sync-ktest.c, tests/lcd-fake-hw.c |
| `dp-sink.c` | `parity_edp_dpcd_write`（dp/parity_edp.c） | `drv_i915_edp_dpcd_write` | aux.c, tests/lcd-fake-hw.c |
| `dp-sink.c` | `parity_edp_end`（dp/parity_edp.c） | `drv_i915_edp_end` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `dp-sink.c` | `parity_edp_init_late`（dp/parity_edp.c） | `drv_i915_edp_init_late` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `dp-sink.c` | `parity_edp_panel_op`（dp/parity_edp.c） | `drv_i915_edp_panel_op` | panel.c, tests/lcd-fake-hw.c |
| `dp-sink.c` | `parity_edp_read_dpcd_caps`（dp/parity_edp.c） | `drv_i915_edp_read_dpcd_caps` | aux.c, tests/lcd-fake-hw.c |
| `dp-sink.c` | `parity_edp_snapshot`（dp/parity_edp.c） | `drv_i915_edp_snapshot` | tests/edp-ktest.c, tests/edp-sync-ktest.c |
| `dp-sink.c` | `parity_edp_work_run`（dp/parity_edp.c） | `drv_i915_edp_work_run` | tests/dp-fake-hw.c, tests/edp-sync-ktest.c |
| `edid-read.c` | `parity_drm_edid_read`（dp/parity_drm_edid_glue.inc） | `drv_i915_drm_edid_read` | dp.c, hdmi.c |
| `panel.c` | `intel_pps_backlight_off`（dp/intel_pps_port.c） | `drv_i915_pps_backlight_off` | dp.c |
| `panel.c` | `intel_pps_backlight_on`（dp/intel_pps_port.c） | `drv_i915_pps_backlight_on` | dp.c |
| `panel.c` | `intel_pps_init`（dp/intel_pps_port.c） | `drv_i915_pps_init` | dp.c |
| `panel.c` | `intel_pps_init_late`（dp/intel_pps_port.c） | `drv_i915_pps_init_late` | dp.c |
| `panel.c` | `intel_pps_off`（dp/intel_pps_port.c） | `drv_i915_pps_off` | ddi.c, dp.c |
| `panel.c` | `intel_pps_on`（dp/intel_pps_port.c） | `drv_i915_pps_on` | ddi.c, dp.c |
| `panel.c` | `intel_pps_vdd_off_sync`（dp/intel_pps_port.c） | `drv_i915_pps_vdd_off_sync` | dp.c |
| `panel.c` | `intel_pps_vdd_on`（dp/intel_pps_port.c） | `drv_i915_pps_vdd_on` | ddi.c, dp.c |

#### P7 DDI・DP link・HDMI mode・EDID mode

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `ddi.c` | `hsw_chicken_trans_reg`（lcd/intel_ddi_port.c） | `drv_i915_hsw_chicken_trans_reg` | pipe.c |
| `ddi.c` | `intel_ddi_compute_min_voltage_level`（lcd/intel_ddi_port.c） | `drv_i915_ddi_compute_min_voltage_level` | modeset.c |
| `ddi.c` | `intel_ddi_sanitize_encoder_pll_mapping`（lcd/intel_ddi_port.c） | `drv_i915_ddi_sanitize_encoder_pll_mapping` | takeover.c |
| `ddi.c` | `intel_encoders_disable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_disable` | pipe.c |
| `ddi.c` | `intel_encoders_enable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_enable` | pipe.c |
| `ddi.c` | `intel_encoders_post_disable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_post_disable` | pipe.c |
| `ddi.c` | `intel_encoders_post_pll_disable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_post_pll_disable` | pipe.c |
| `ddi.c` | `intel_encoders_pre_enable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_pre_enable` | pipe.c |
| `ddi.c` | `intel_encoders_pre_pll_enable`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_encoders_pre_pll_enable` | pipe.c |
| `ddi.c` | `parity_ddi_emit`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_ddi_emit` | state.c |
| `ddi.c` | `parity_lcd_ms_bind_encoder`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_lcd_ms_bind_encoder` | modeset.c |
| `ddi.c` | `parity_lcd_ms_bind_readout`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_lcd_ms_bind_readout` | takeover.c |
| `ddi.c` | `parity_lcd_ms_bound_connector`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_lcd_ms_bound_connector` | takeover.c |
| `ddi.c` | `parity_lcd_ms_bound_encoder`（lcd/parity_ddi_emit_glue.inc） | `drv_i915_lcd_ms_bound_encoder` | takeover.c |
| `dp.c` | `drm_dp_channel_eq_ok`（lcd/drm_dp_link_port.c） | `drv_i915_drm_dp_channel_eq_ok` | modeset.c |
| `dp.c` | `drm_dp_clock_recovery_ok`（lcd/drm_dp_link_port.c） | `drv_i915_drm_dp_clock_recovery_ok` | modeset.c |
| `dp.c` | `drm_dp_dpcd_read_phy_link_status`（lcd/drm_dp_link_port.c） | `drv_i915_drm_dp_dpcd_read_phy_link_status` | modeset.c |
| `dp.c` | `intel_dp_is_edp`（lcd/intel_link_port.c） | `drv_i915_dp_is_edp` | panel.c |
| `dp.c` | `intel_dp_is_uhbr`（lcd/intel_link_port.c） | `drv_i915_dp_is_uhbr` | ddi.c |
| `dp.c` | `intel_dp_link_required`（lcd/intel_link_port.c） | `drv_i915_dp_link_required` | state.c |
| `dp.c` | `intel_dp_link_symbol_size`（lcd/intel_link_port.c） | `drv_i915_dp_link_symbol_size` | pipe.c |
| `dp.c` | `intel_dp_max_data_rate`（lcd/intel_link_port.c） | `drv_i915_dp_max_data_rate` | state.c |
| `dp.c` | `intel_dp_needs_vsc_sdp`（lcd/intel_link_port.c） | `drv_i915_dp_needs_vsc_sdp` | ddi.c |
| `dp.c` | `intel_dp_set_infoframes`（lcd/intel_link_port.c） | `drv_i915_dp_set_infoframes` | ddi.c |
| `dp.c` | `intel_dp_set_link_params`（lcd/intel_link_port.c） | `drv_i915_dp_set_link_params` | ddi.c |
| `dp.c` | `intel_dp_set_power`（lcd/intel_link_port.c） | `drv_i915_dp_set_power` | ddi.c |
| `dp.c` | `intel_dp_start_link_train`（lcd/intel_dp_link_training_port.c） | `drv_i915_dp_start_link_train` | ddi.c |
| `dp.c` | `intel_dp_stop_link_train`（lcd/intel_dp_link_training_port.c） | `drv_i915_dp_stop_link_train` | ddi.c |
| `dp.c` | `intel_edp_backlight_off`（lcd/intel_link_port.c） | `drv_i915_edp_backlight_off` | ddi.c, panel.c |
| `dp.c` | `intel_edp_backlight_on`（lcd/intel_link_port.c） | `drv_i915_edp_backlight_on` | ddi.c, panel.c |
| `edid.c` | `drm_mode_copy`（lcd/drm_modes_hv_port.c） | `drv_i915_drm_mode_copy` | pipe.c |
| `edid.c` | `drm_mode_get_hv_timing`（lcd/drm_modes_hv_port.c） | `drv_i915_drm_mode_get_hv_timing` | watermark.c |
| `edid.c` | `drm_mode_init`（lcd/drm_modes_hv_port.c） | `drv_i915_drm_mode_init` | vblank.c |
| `edid.c` | `drm_mode_set_crtcinfo`（lcd/drm_modes_port.c） | `drv_i915_drm_mode_set_crtcinfo` | modeset.c, pipe.c |
| `edid.c` | `drm_mode_set_name`（lcd/parity_edid_mode_glue.inc） | `drv_i915_lcd_drm_mode_set_name`（旧の実名 `parity_lcd_drm_mode_set_name`） | pipe.c |
| `edid.c` | `parity_edid_preferred_mode`（lcd/parity_edid_mode_glue.inc） | `drv_i915_edid_preferred_mode` | state.c |
| `hdmi-mode.c` | `intel_dp_dual_mode_set_tmds_output`（lcd/intel_hdmi_mode_port.c） | `drv_i915_dp_dual_mode_set_tmds_output` | ddi.c |
| `hdmi-mode.c` | `intel_hdmi_handle_sink_scrambling`（lcd/intel_hdmi_mode_port.c） | `drv_i915_hdmi_handle_sink_scrambling` | ddi.c |

#### P8 pipe・plane・vblank・color・backlight

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `color.c` | `intel_color_commit_arm`（lcd/intel_color_port.c） | `drv_i915_color_commit_arm` | pipe.c, takeover.c |
| `color.c` | `intel_color_commit_noarm`（lcd/intel_color_port.c） | `drv_i915_color_commit_noarm` | pipe.c, takeover.c |
| `color.c` | `intel_color_load_luts`（lcd/intel_color_port.c） | `drv_i915_color_load_luts` | pipe.c |
| `color.c` | `parity_lcd_ms_color_check`（lcd/parity_color_glue.inc） | `drv_i915_lcd_ms_color_check` | modeset.c |
| `color.c` | `parity_lcd_ms_color_funcs`（lcd/parity_color_glue.inc） | `drv_i915_lcd_ms_color_funcs` | takeover.c |
| `panel-backlight.c` | `intel_backlight_disable`（lcd/intel_backlight_port.c） | `drv_i915_backlight_disable` | dp.c |
| `panel-backlight.c` | `intel_backlight_enable`（lcd/intel_backlight_port.c） | `drv_i915_backlight_enable` | dp.c |
| `panel-backlight.c` | `intel_backlight_set_acpi`（lcd/intel_backlight_port.c） | `drv_i915_backlight_set_acpi` | opregion.c, tests/opregion-ktest.c |
| `panel-backlight.c` | `parity_lcd_modeset_backlight`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_backlight` | tests/kernel-scenarios.c |
| `panel-backlight.c` | `parity_lcd_modeset_backlight_acpi`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_backlight_acpi` | tests/kernel-scenarios.c |
| `panel-backlight.c` | `parity_lcd_modeset_brightness`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_brightness` | tests/kernel-scenarios.c |
| `panel-backlight.c` | `parity_lcd_ms_backlight_setup`（lcd/parity_backlight_glue.inc） | `drv_i915_lcd_ms_backlight_setup` | modeset.c |
| `panel-backlight.c` | `parity_lcd_ms_user_level`（lcd/parity_backlight_glue.inc） | `drv_i915_lcd_ms_user_level` | modeset.c |
| `pipe.c` | `intel_aux_power_domain`（lcd/intel_display_port.c） | `drv_i915_aux_power_domain` | panel.c |
| `pipe.c` | `intel_cpu_transcoder_get_m1_n1`（lcd/intel_display_port.c） | `drv_i915_cpu_transcoder_get_m1_n1` | ddi.c |
| `pipe.c` | `intel_cpu_transcoder_get_m2_n2`（lcd/intel_display_port.c） | `drv_i915_cpu_transcoder_get_m2_n2` | ddi.c |
| `pipe.c` | `intel_crtc_dotclock`（lcd/intel_display_port.c） | `drv_i915_crtc_dotclock` | ddi.c |
| `pipe.c` | `intel_crtc_get_pipe_config`（lcd/intel_display_port.c） | `drv_i915_crtc_get_pipe_config` | takeover.c |
| `pipe.c` | `intel_crtc_state_reset`（lcd/intel_crtc_port.c） | `drv_i915_crtc_state_reset` | takeover.c |
| `pipe.c` | `intel_disable_transcoder`（lcd/intel_display_port.c） | `drv_i915_disable_transcoder` | ddi.c |
| `pipe.c` | `intel_enable_transcoder`（lcd/intel_display_port.c） | `drv_i915_enable_transcoder` | ddi.c |
| `pipe.c` | `intel_encoder_get_config`（lcd/intel_display_port.c） | `drv_i915_encoder_get_config` | takeover.c |
| `pipe.c` | `intel_link_compute_m_n`（lcd/intel_display_port.c） | `drv_i915_link_compute_m_n` | state.c |
| `pipe.c` | `intel_modeset_get_crtc_power_domains`（lcd/intel_display_port.c） | `drv_i915_modeset_get_crtc_power_domains` | modeset.c, takeover.c |
| `pipe.c` | `intel_modeset_put_crtc_power_domains`（lcd/intel_display_port.c） | `drv_i915_modeset_put_crtc_power_domains` | modeset.c, takeover.c |
| `pipe.c` | `intel_phy_is_combo`（lcd/intel_display_port.c） | `drv_i915_phy_is_combo` | ddi.c |
| `pipe.c` | `intel_phy_is_tc`（lcd/intel_display_port.c） | `drv_i915_lcd_intel_phy_is_tc` | ddi.c |
| `pipe.c` | `intel_pipe_update_end`（lcd/intel_crtc_port.c） | `drv_i915_pipe_update_end` | plane.c, present.c |
| `pipe.c` | `intel_pipe_update_start`（lcd/intel_crtc_port.c） | `drv_i915_pipe_update_start` | plane.c |
| `pipe.c` | `intel_plane_disable_noatomic`（lcd/intel_display_port.c） | `drv_i915_plane_disable_noatomic` | takeover.c, watermark.c |
| `pipe.c` | `intel_plane_fixup_bitmasks`（lcd/intel_display_port.c） | `drv_i915_plane_fixup_bitmasks` | takeover.c |
| `pipe.c` | `intel_port_to_phy`（lcd/intel_display_port.c） | `drv_i915_lcd_intel_port_to_phy` | ddi.c |
| `pipe.c` | `intel_set_plane_visible`（lcd/intel_display_port.c） | `drv_i915_set_plane_visible` | takeover.c |
| `pipe.c` | `parity_display_emit_cpu_transcoder`（lcd/parity_display_emit_glue.inc） | `drv_i915_display_emit_cpu_transcoder` | state.c |
| `pipe.c` | `parity_display_emit_transcoder`（lcd/parity_display_emit_glue.inc） | `drv_i915_display_emit_transcoder` | state.c |
| `pipe.c` | `parity_lcd_ms_crtc_disable`（lcd/parity_display_emit_glue.inc） | `drv_i915_lcd_ms_crtc_disable` | modeset.c |
| `pipe.c` | `parity_lcd_ms_crtc_enable`（lcd/parity_display_emit_glue.inc） | `drv_i915_lcd_ms_crtc_enable` | modeset.c |
| `pipe.c` | `parity_lcd_ms_display_funcs`（lcd/parity_display_emit_glue.inc） | `drv_i915_lcd_ms_display_funcs` | takeover.c |
| `plane.c` | `icl_hdr_plane_mask`（lcd/skl_plane_port.c） | `drv_i915_icl_hdr_plane_mask` | pipe.c |
| `plane.c` | `intel_adjusted_rate`（lcd/intel_atomic_plane_port.c） | `drv_i915_adjusted_rate` | pipe.c |
| `plane.c` | `intel_plane_data_rate`（lcd/intel_atomic_plane_port.c） | `drv_i915_plane_data_rate` | takeover.c |
| `plane.c` | `parity_lcd_ms_plane_data_rates`（lcd/parity_atomic_plane_glue.inc） | `drv_i915_lcd_ms_plane_data_rates` | watermark.c |
| `plane.c` | `parity_lcd_ms_plane_disable`（lcd/parity_plane_emit_glue.inc） | `drv_i915_lcd_ms_plane_disable` | modeset.c |
| `plane.c` | `parity_lcd_ms_plane_min_cdclk`（lcd/parity_plane_emit_glue.inc） | `drv_i915_lcd_ms_plane_min_cdclk` | modeset.c |
| `plane.c` | `parity_lcd_ms_plane_prepare`（lcd/parity_plane_emit_glue.inc） | `drv_i915_lcd_ms_plane_prepare` | modeset.c, present.c |
| `plane.c` | `parity_lcd_ms_plane_update`（lcd/parity_plane_emit_glue.inc） | `drv_i915_lcd_ms_plane_update` | modeset.c |
| `plane.c` | `parity_lcd_ms_plane_update_flip`（lcd/parity_plane_emit_glue.inc） | `drv_i915_lcd_ms_plane_update_flip` | present.c |
| `plane.c` | `parity_plane_emit`（lcd/parity_plane_emit_glue.inc） | `drv_i915_plane_emit` | state.c |
| `vblank.c` | `intel_crtc_update_active_timings`（lcd/intel_vblank_port.c） | `drv_i915_crtc_update_active_timings` | takeover.c |
| `vblank.c` | `intel_get_crtc_scanline`（lcd/intel_vblank_port.c） | `drv_i915_get_crtc_scanline` | pipe.c |
| `vblank.c` | `intel_wait_for_pipe_scanline_moving`（lcd/intel_vblank_port.c） | `drv_i915_wait_for_pipe_scanline_moving` | pipe.c |
| `vblank.c` | `parity_lcd_modeset_evade_window`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_evade_window` | tests/kernel-scenarios.c |
| `vblank.c` | `parity_lcd_ms_active_timings`（lcd/parity_flip_glue.inc） | `drv_i915_lcd_ms_active_timings` | modeset.c |
| `vblank.c` | `parity_lcd_ms_crtc_funcs`（lcd/parity_flip_glue.inc） | `drv_i915_lcd_ms_crtc_funcs` | takeover.c |

#### P9 modeset・scanout・present・display・割込み

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `interrupts.c` | `parity_drm_vblank_get`（irq.c） | `drv_i915_drm_vblank_get` | vblank.c |
| `interrupts.c` | `parity_drm_vblank_put`（irq.c） | `drv_i915_drm_vblank_put` | vblank.c |
| `interrupts.c` | `parity_wait_vblank`（irq.c） | `drv_i915_wait_vblank` | vblank.c |
| `modeset.c` | `parity_lcd_backend_fault`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_backend_fault` | mmio.c, power.c, sync.c, tests/lcd-fake-hw.c, vblank.c |
| `modeset.c` | `parity_lcd_display_ver`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_display_ver` | clock.c, phy.c, watermark.c |
| `modeset.c` | `parity_lcd_modeset_abandoned`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_abandoned` | tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_commit_disable`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_commit_disable` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_commit_enable`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_commit_enable` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_link_status`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_link_status` | tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_plane_disable`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_plane_disable` | tests/kernel-scenarios.c |
| `modeset.c` | `parity_lcd_modeset_plane_released`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_plane_released` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_prepare`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_prepare` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_modeset_retained`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_retained` | diagnostics.c, panel.c, present.c, tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c, tests/modeset.c |
| `modeset.c` | `parity_lcd_modeset_select`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_select` | tests/kernel-scenarios.c |
| `modeset.c` | `parity_lcd_modeset_status`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_status` | tests/kernel-scenarios.c, tests/lcd-modeset-ktest.c |
| `modeset.c` | `parity_lcd_show_discard_gpu_model`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_discard_gpu_model` | tests/lcdg-ktest.c |
| `modeset.c` | `parity_lcd_show_discard_model`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_discard_model` | tests/lcd-show-ktest.c |
| `modeset.c` | `parity_lcd_show_gpu_retained`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_gpu_retained` | diagnostics.c, tests/lcdg-ktest.c |
| `modeset.c` | `parity_lcd_show_prepared`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_prepared` | tests/kernel-scenarios.c, tests/lcd-show-ktest.c |
| `modeset.c` | `parity_lcd_show_retain_gpu`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_retain_gpu` | tests/kernel-scenarios.c |
| `modeset.c` | `parity_lcd_show_retained`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_retained` | diagnostics.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/lcdg-ktest.c |
| `modeset.c` | `parity_lcd_show_run`（lcd/parity_lcd_show.c） | `drv_i915_lcd_show_run` | tests/kernel-scenarios.c, tests/lcd-show-ktest.c |
| `present.c` | `parity_lcd_modeset_flip`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_flip` | tests/kernel-scenarios.c |
| `present.c` | `parity_lcd_resident_flip`（lcd/parity_lcd_kernel.c） | `drv_i915_lcd_resident_flip` | modeset.c |
| `scanout.c` | `parity_scanout_abandon`（lcd/scanout.c） | `drv_i915_scanout_abandon` | modeset.c, tests/kernel-scenarios.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_begin`（lcd/scanout.c） | `drv_i915_scanout_begin` | modeset.c, tests/kernel-scenarios.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_create`（lcd/scanout.c） | `drv_i915_scanout_create` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/lcdg-ktest.c, tests/scanout-hw-check.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_destroy`（lcd/scanout.c） | `drv_i915_scanout_destroy` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/scanout-hw-check.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_end`（lcd/scanout.c） | `drv_i915_scanout_end` | modeset.c, tests/kernel-scenarios.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_pin`（lcd/scanout.c） | `drv_i915_scanout_pin` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/lcdg-ktest.c, tests/scanout-hw-check.c, tests/scanout-ktest.c |
| `scanout.c` | `parity_scanout_publish`（lcd/scanout.c） | `drv_i915_scanout_publish` | modeset.c, present.c, takeover.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/scanout-hw-check.c |
| `scanout.c` | `parity_scanout_unpin`（lcd/scanout.c） | `drv_i915_scanout_unpin` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/scanout-hw-check.c, tests/scanout-ktest.c |

#### tests（本番からの逆依存を含む）

| 新ファイル | 旧名（旧ファイル） | 新名 | 呼出し側 |
| --- | --- | --- | --- |
| `tests/dp-fake-hw.c` | `dp_fake_bind_env`（dp/dp_fake_hw.c） | `drv_i915_dp_fake_bind_env` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/dp-fake-hw.c` | `dp_fake_flush_async`（dp/dp_fake_hw.c） | `drv_i915_dp_fake_flush_async` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/dp-fake-hw.c` | `dp_fake_init`（dp/dp_fake_hw.c） | `drv_i915_dp_fake_init` | tests/edp-ktest.c, tests/edp-sync-ktest.c, tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/dp-fake-hw.c` | `dp_fake_run_due`（dp/dp_fake_hw.c） | `drv_i915_dp_fake_run_due` | tests/edp-ktest.c |
| `tests/dp-fake-hw.c` | `dp_fake_script`（dp/dp_fake_hw.c） | `drv_i915_dp_fake_script` | tests/edp-ktest.c |
| `tests/kernel-scenarios.c` | `parity_lcd_kernel_lcdg_run`（lcd/parity_lcd_kernel.c） | `drv_i915_lcd_kernel_lcdg_run` | tests/lcdg-ktest.c |
| `tests/kernel-scenarios.c` | `parity_lcdg_finish`（lcd/parity_lcd_kernel.c） | `drv_i915_lcdg_finish` | tests/lcdg-ktest.c |
| `tests/lcd-fake-hw.c` | `lcd_fake_init`（lcd/lcd_fake_hw.c） | `drv_i915_lcd_fake_init` | tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/lcd-fake-hw.c` | `lcd_fake_power_refs_total`（lcd/lcd_fake_hw.c） | `drv_i915_lcd_fake_power_refs_total` | tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/lcd-fake-hw.c` | `lcd_fake_reg`（lcd/lcd_fake_hw.c） | `drv_i915_lcd_fake_reg` | tests/lcd-modeset-ktest.c |
| `tests/lcd-fake-hw.c` | `lcd_fake_violations`（lcd/lcd_fake_hw.c） | `drv_i915_lcd_fake_violations` | tests/lcd-modeset-ktest.c, tests/lcd-show-ktest.c |
| `tests/lcd-pattern.c` | `parity_lcd_pattern_fill`（lcd/lcd_pattern.c） | `drv_i915_lcd_pattern_fill` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/scanout-hw-check.c |
| `tests/lcd-pattern.c` | `parity_lcd_pattern_verify`（lcd/lcd_pattern.c） | `drv_i915_lcd_pattern_verify` | modeset.c, tests/kernel-scenarios.c, tests/lcd-show-ktest.c, tests/scanout-hw-check.c |
| `tests/modeset.c` | `parity_lcd_modeset_discard_model`（lcd/parity_lcd_modeset.c） | `drv_i915_lcd_modeset_discard_model` | modeset.c, tests/lcd-modeset-ktest.c |

## 9. 統合担当の決定（2026-09-22）

1. 環境ごとのファイル分割（dp/dp-sink、panel/panel-backlight、edid/edid-read、hdmi/hdmi-mode）は移動の間は採用する。layout の統一は移動後の別段階。
2. 環境状態の確保に失敗したら、表示初期化の失敗として device start を止める（旧 probe が失敗時に全体を teardown したのと同じ扱い）。
3. dp／vbt 環境の表示版数 13 固定は、挙動を保って XXX を付ける（修正は別段階）。
4. DVO_PORT／DISABLE_DPT_CLK_GATING の食い違いは別段階で直す。移動では XXX のみ。
5. panel-run deps の irq を struct i915_display_irq * にする変更は採用。
6. 旧の生成器（port_lcd_calc.py など）は廃止。書き直した表示コードは以後手で管理する（設計 R5 の決定）。出典の manifest は data/provenance に残す。
7. 公開ヘッダはファイルごとに一つ。
8. takeover の装置の食い違い、明示名から Linux 名への復帰は、layout 統一の段階で決める。
