# WS052 p009 設計: i915 の suspend・resume（S0i3）

Status: 案の第 1 版（2026-10-05、ws052-p009、P1 generation17）。code は Q1 がこの設計を見てから（2026-10-05 Q1）。design-reviewer の敵対的レビューは
未実施。親: [design.md](design.md) の §9（p009）、[phase009](phase009/phase.md)。

## 1. 目的と範囲

p004 の PCI の口（`src/drivers/pci/pci-power.c`、driver の `suspend`・`resume`）に i915 を載せ、S0i3 に入れる状態（GT の RC6、display の DC9、
D3hot）にして、戻ったら **suspend の前の出力先**（eDP の panel、または `display=hdmi` の HDMI。Keiland の lease はそのまま）に画面を戻す。

- 範囲: i915 の `suspend`・`resume`、display の窓（resident run）を出て戻る口、request worker の停止と再開、GT（forcewake・RPS・RC6・engine）、
  display の power（power domain・DC9・DMC の再 load・display の core の再初期化）、割り込み。
- 範囲外: CPU の idle・tick・割り込みの mask（H1〜H4、p006）。runtime PM（画面を点けたままの DC6 などの日常の省電力）。GuC/HuC（使っていない）。
  複数の出力の同時表示（今の driver が 1 画面）。
- **HAL に依らない**: すべて i915 の MMIO と kernel の同期で、`include/hal/hal.h` は要らない。H1〜H4 の承認を待たずに進められる（実機で S0i3 の
  SLP_S0 まで見るのは p006 の後）。

## 2. 今の driver の形（2026-10-05 の main を読んだ）

| 部分 | 今の形 | suspend に関わること |
| --- | --- | --- |
| 起動（`device.c` `drv_i915_device_start`） | start worker の thread が bring-up し、`i915_start_publish` で**全 forcewake domain を持ったまま**（`gt->forcewake_held`）RPS を始め、`drv_i915_worker_serve` で node を publish して request を同じ thread で回す | forcewake を持つ間 GT は RC6 に入らない（S0ix を塞ぐ）。suspend では worker の thread が forcewake を放す |
| request worker（`worker.c`） | `i915_worker_loop` が queue を回し、最初の presentation で `drv_i915_present_window` に入る。`worker->stop` で抜けて unpublish | **止めずに「park」する口**（node を publish したまま、queue を受けても実行しない）が要る |
| 表示（`present.c`・`modeset.c`） | 窓 = `drv_i915_lcd_kernel_resident_run`: 出力を点け（modeset）、2 枚の全画面 buffer に flip、窓の中で request を回し、hold が切れるか shutdown の `hold_ended` で抜けて reference の stop path で消す。lease（`rd->owner`）は窓を出ても残り、次の presentation で窓に入り直す（`display->output` の出力先の選択は残る） | **lease があっても窓を出る口**（suspend の要求）を足せば、resume の後の次の presentation で同じ出力先に点き直る。buffer は作り直し（黒から、次の frame で埋まる） |
| display の power（`display/power.c`） | `drv_i915_power_domains_init_hw`（icl_display_core_init、INIT の参照）、`_enable`（INIT を放す、DC6 を許す）、`_disable`、`drv_i915_gen9_set_dc_state`、allowed の DC に DC9 がある（display ver 11+） | suspend: 窓を出た後に INIT を取り直して wells を整え、DC9 に。resume: DC9 を解き、core を再初期化、DMC を再 load |
| DMC（`display/dmc.c`） | `drv_i915_dmc_load_program` | DC9 で DMC の program RAM は消える。resume で再 load |
| GT の power（`gt-power.c`・`engine.c`） | `drv_i915_gen11_rc6_enable`（`drv_i915_gt_resume` の中）、`drv_i915_rps_start`・`_stop` | RC6 は enable 済み。suspend は forcewake を放し RPS を止めて GT を idle に。resume は `drv_i915_gt_resume`（engine の resume、workaround、MOCS、RC6）と RPS |
| GGTT（`ggtt.c`） | `drv_i915_gt_ggtt_bind` が PTE を書く。PTE は GSM（stolen memory） | S0i3 では memory は self-refresh で残る。D3hot→D0 で GSM の中身は残る見込みだが、確かめる（§6 の判断） |
| 割り込み（`irq.c`） | `drv_i915_irq_install`・`_uninstall`・`_reset`・`_postinstall` | suspend で `_reset`（全 source を mask）、resume で `_postinstall` |

## 3. suspend（`i915_suspend`、PCI の driver の op）

1. **窓を出る**: `display->window.suspend_request = 1` を立て worker を起こす。窓の serve（`i915_present_window_serve`）はそれを見て窓を抜け、
   resident run は reference の stop path で出力を消し buffer を放す（hold の終わりと同じ道）。lease は残す。
2. **worker を park する**: worker は窓を出た後、`suspend_request` を見て (a) 実行中の request を終え、(b) RPS を止め（`drv_i915_rps_stop`）、
   (c) 全 forcewake を放し（`i915_forcewake_put_all(gt, gt->forcewake_held)`、`forcewake_held` を保存）、(d) `parked = 1` を立てて resume の
   合図まで眠る。park 中に来た request は queue に溜まり、presentation する thread は待つ（S0i3 では user は freeze されている。devices だけの
   mode では compositor が待つ）。suspend の op は `parked` を有限時間（2 秒）待ち、来なければ要求を取り消して EBUSY（中止の理由: 「i915 が idle に
   ならない」）。
3. **GT を idle に**: engine が idle（`RING_HEAD == RING_TAIL`、CSB が空）であることを確かめる。forcewake を放した GT は RC6 に入る
   （`GEN6_GT_CORE_STATUS` の RC6 を読んで log に出す。入らなくても中止はしない: 実機の確認で見る）。
4. **割り込みを止める**: `drv_i915_irq_reset`（display・GT の全 source を mask と clear）。handler は残す（MSI は PCI の口が保存・復元）。
5. **display の power を落とす**: 窓を出た後の wells は display の参照だけ。`drv_i915_power_domains_disable`（INIT を取り直す、Linux の
   `intel_display_driver_suspend` の後の `intel_power_domains_suspend` に当たる）→ wells を INIT の分以外は off にして、**DC9**
   （`drv_i915_gen9_set_dc_state(pwc, I915_DC_STATE_EN_DC9)`、前提: DC6/DC5 を解き、PG1 以外の well が off、DBUF・CDCLK の状態は §6 の判断）。
   PCH の **Wa_14010685332**（ADP: `SOUTH_CHICKEN1` の `SBCLK_RUN_REFCLK_DIS` を立てる。S0ix の時だけ、resume で戻す）。
6. op は 0 を返し、PCI の口が構成を保存して D3hot、ACPI の `_PS3`（GFX0）へ。

失敗した段があれば、その段までを逆に戻し（DC9 を解く、wells、割り込み、worker の unpark）、PCI の口の中止の規則で前の device も戻る。

## 4. resume（`i915_resume`）

1. PCI の口が D0 と構成（MSI、BAR、command）を戻した後に呼ばれる。
2. **display の core**: DC9 を解く（`drv_i915_gen9_set_dc_state(pwc, 0)`）、Wa_14010685332 を戻す、`drv_i915_power_domains_init_hw(dc, 1)`
   （icl_display_core_init: PCH reset handshake、PG1、CDCLK、DBUF、combo PHY）、**DMC の再 load**（`drv_i915_dmc_load_program`）、wells を
   software の数に合わせる（`drv_i915_power_well_sync_hw`）、`drv_i915_power_domains_enable`（INIT を放す、DC6 を許す）。
3. **GGTT**: §6 の判断に従う（残っていれば何もしない、または `gt->mem` の記録から PTE を書き直し `drv_i915_gt_ggtt_flush`）。
4. **GT**: `drv_i915_gt_resume`（engine の resume、workaround、MOCS、RC6 の enable）。fence の register を書き直す（32 本、`I915_START_FENCES`）。
5. **割り込み**: `drv_i915_irq_postinstall`。
6. **worker を起こす**: `parked` を解き、worker は forcewake を取り直し（保存した `forcewake_held`）、RPS を始め（`drv_i915_rps_start`）、
   `suspend_request` を下ろして queue を回す。次の presentation（compositor は frame を出し続ける）で窓に入り、`display->output` の出力先
   （panel か HDMI）に resident run が点き直す（modeset、link training、panel の power・backlight）。
7. 窓への再入が失敗したら `window.display_failed` になり presentation は失敗し続ける（今の動き）。p009 では resume の後の最初の窓の結果を
   log に出し、UAT で見る。

## 5. 確かめ方

- **host の試験**（QEMU に i915 は無い）: worker の park の状態機械（窓の中・外、lease の有無、park の timeout での中止）を `tests/` の既存の
  fake（`tests/execution/ktest-*`、`tests/display/dp-fake-hw.c`）の形で: DC9 の前提の検査、DC の register の書き順、Wa の bit。
- **build**: vmunix の link（warning 0）。
- **5330 の UAT**（ユーザーの実機、p004 の `sleepctl devices`）: 5330 では i915（0:02.0）は bus の順で最初に suspend され、後の LPSS-I2C などで
  p005 まで中止になるため、**中止の巻き戻しで i915 の suspend→resume が必ず通る**。確かめること: (1) 画面が消えて戻る、(2) 同じ出力先（panel、
  または HDMI）に Keiland の画面が戻る、(3) GPU の描画（compositor・アプリ）が続く、(4) dmesg に RC6 の residency の増加と DC9 の入出。
  p006 の後に S0i3 で SLP_S0 の residency。
- 失敗の解析は gdbstub ではなく（実機）、dmesg の i915 の trace（`drv_i915_trace`）。

## 6. 判断が要る点（Q1 経由、技術判断は案を付ける）

1. **GGTT の書き直し**: S0i3 で GSM の中身は残る（memory の self-refresh）。案: 書き直さず、resume で数個の PTE を読み直して比べ（`drv_i915_gt_ggtt_read_pte`）、
   違えば `gt->mem` の記録から書き直す。Linux は常に書き直す（`i915_ggtt_resume`）。
2. **DBUF・CDCLK の DC9 の前**: Linux の `bxt_enable_dc9` の前提（`assert_can_enable_dc9`: PG2 off、interrupts off、PCH の power の状態）に合わせ、
   CDCLK は resume の core init で作り直す。案: Linux の順に揃える。
3. **park 中の request**: queue に溜めて待たせる（失敗にしない）案。devices だけの mode では compositor が数百 ms 止まる。
4. **出力先の記憶**: 今は `display->output`（boot の `display=hdmi` と、Keiland の指示の分）だけで、resident run の modeset の値は窓を出ると消える。
   案: 窓に入り直す時に同じ選択から計算し直す（mode も EDID から同じものが選ばれる）。同じでない時の扱い（resume の間に monitor が外れた）は
   hotplug が無い今は「次の窓の失敗」として log に出す。
5. **Phase の分け方**: 案は 3 つの commit: (a) worker の park と窓の suspend の要求（host の試験）、(b) GT と割り込みと display の power と DC9・DMC
   （fake の試験）、(c) i915 の `suspend`・`resume` の op の結線と UAT の手順。

## Q1 の判断（2026-10-05、§6 の 5 つ）

1. GGTT: **resume で毎回書き直す**（Linux と同じ。標本の照合で済ませる案は採らない。書き直しの費用は小さく、取りこぼしの危険を避ける）。
2. DC9 の前提: 案のとおり Linux の assert_can_enable_dc9 の順に合わせる。
3. park 中の要求: 案のとおり queue に入れて待たせる。
4. 出力の記憶: 案のとおり同じ選び方で再計算。sleep の間に外部の monitor が抜かれた時は、失敗の記録に加え、Guardrail の scanout の規則（firmware の画面が無い時は内蔵の panel と判断できる出力、2026-10-05 ユーザー）に合わせて内蔵の panel を点けることを試み、点かなくても起動・resume に影響させない。
5. commit の分け方: 案のとおり (a)(b)(c) の 3 段。
