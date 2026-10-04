<!-- awesome-plan project=zedbsd record=ws052p009 -->

# ws052-p009: i915 の suspend・resume

Phase ID: `ws052-p009`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17。段 (a)・(b)・(c) を実装（host の試験、vmunix の link）。5330 の UAT 待ち）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（p004 から i915 を分けた: QEMU で試せず規模が大きい。設計から、検証は 5330 の UAT）

## 範囲

display の suspend（窓を出る）、request worker の park、GT（forcewake・RPS・RC6・engine）、割り込み、display の power（DC9、DMC の再 load、display の
core の再初期化）、GGTT、resume で suspend の前の出力先（panel か HDMI、Keiland の lease はそのまま）へ戻す。HAL に依らない。

## 受け入れ

- 設計（Q1 のレビュー済み）、build（vmunix の link、warning 0）、host の試験（worker の park の状態機械、DC9 の前提と書き順）。
- 5330 の UAT（p004 の `sleepctl devices`）: 画面が消えて同じ出力先に戻り、GPU の描画が続く。dmesg に RC6 と DC9。

## 設計

[design-p009-i915.md](../design-p009-i915.md)（§6 に判断の点: GGTT の書き直し、DC9 の前提、park 中の request、出力先の記憶、commit の分け方）。

## 依存

- p004（PCI の口、`KERN_SYSTEM_SLEEP` の devices だけの mode）。
- HAL は不要（H1〜H4 の承認を待たない）。

## 段 (a): worker の park と窓の suspend の要求（2026-10-05）

- `src/drivers/gpu/i915/park.c`・`park.h`（新規、amd64 の vmunix に追加）: park の状態と判断（device の IRQ lock の下で呼ぶ、眠らない）。
  `drv_i915_park_begin`（二重は EBUSY、generation を進める）、`drv_i915_park_action`（park が無ければ serve、stop が勝つ、窓の中なら窓を出る、
  外なら park）、`_entered`・`_stays`・`_left`・`_end`（reach 済みかを返す）・`_reached`（generation で自分の park か）。
- `worker.c`: loop の各回の頭で `drv_i915_park_action` を見て、窓の中は `I915_WORKER_SERVE_LEAVE_DISPLAY`（resident run が reference の stop path で
  出力を消す。lease は残る）、外は新しい `I915_WORKER_SERVE_PARK`。眠りの条件に park を足し、起きたら頭で判断する。serve の loop は PARK で
  `i915_worker_park_here`（GT を idle に → parked を立て sync_done を起こす → park が終わるか stop まで眠る → GT を戻す）。待っている request は
  queue に残る（Q1 の判断 3）。`drv_i915_worker_park(device, timeout_ms)`（reach まで待つ、時間切れは park を取り消して EBUSY、serving でなければ
  ENODEV）、`drv_i915_worker_unpark(device)`。
- `device.c`: `drv_i915_device_park_gt`（RPS を止め、全 forcewake を放して `gt->forcewake_parked` に覚える。GT が RC6 に入れる）、
  `drv_i915_device_unpark_gt`（forcewake を取り直し RPS を始める）。`gt.h` に `forcewake_parked`。
- まだ suspend の op には結線していない（段 (c)）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| park の host の試験（ASan/UBSan） | `make -C plan/ws052/tests i915-park && build/ws052/host/i915-park` | PASS（窓の中→窓を出る→park→reach→resume→serve、窓の外は直ちに park、二重は EBUSY、stop が勝つ・park 中の stop で出る、取り消し後の遅い worker は park しない、generation の取り違え無し） |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| 規約 | `style-check.py`（park.c・park.h・試験、worker.c・device.c は新しい指摘 0） | PASS |
| 実機 | — | 未実施（段 (c) の後、5330 の UAT） |

残りと危険: 同じ lease のまま窓を出て入り直す道は今まで無かった（hold が切れて次の lease が入る道はある）。panel の buffer の map は窓を出る時に
外し、frame ごとに map し直す作りなので通る見込みだが、段 (c) の UAT で確かめる。

## 段 (b): 割り込み・display の power・DC9・DMC・GGTT・GT（2026-10-05）

- `display/dc9.c`・`dc9.h`（新規、amd64 の vmunix に追加）: DC9 の判断（眠らない、host で試験）。入る: DC9 が既に立つなら EALREADY、DC5・DC6 が
  有効か PW_2 が on なら EBUSY、割り込みが有効なら EINVAL（Linux の `assert_can_enable_dc9` の順。Linux は警告だけだが、ここでは拒否する）。出る:
  PW_2 か DC5・DC6 なら EBUSY。PCH の Wa_14010685332（`SOUTH_CHICKEN1` の `SBCLK_RUN_REFCLK_DIS`、bit 7）を入る時に立て、出る時に下ろす（他の
  bit は保つ。TGP・ADP の PCH は Linux の CNP〜DG1 の範囲で、この driver の対象は全部当たる）。
- `display/power.c`: `drv_i915_power_domains_suspend`（`intel_power_domains_suspend`: INIT を放し、遅れた put を済ませ、wells を照合し、
  `icl_display_core_uninit`: DC 状態を解く → DBUF の全 slice を off → CDCLK を bypass で PLL off → PW_1 を off → combo PHY を下ろす）、
  `drv_i915_display_dc9_enter`・`_leave`（判断の後に検証付きの DC 状態の書き込みと Wa）。`internal.h` の display core に `core_suspended`。
- `display/clock.c`: `drv_i915_cdclk_uninit_hw`（bypass、vco 0、その電圧。resume の init_hw は PLL off を見て作り直す）。
  `display/phy.c`: `drv_i915_combo_phy_uninit`（PHY_MISC の DE IO comp を power down、COMP_INIT を下ろす）。
  `display/dmc.c`: `drv_i915_dmc_resume`（保った payload から program を再 load、無ければ ENOENT）。
- `display/display.c`: `drv_i915_display_suspend_begin`（INIT を取り直す）、`_suspend_end`（core を下ろし DC9、拒否なら core を戻して報告）、
  `_resume_begin`（DC9 を出る → `drv_i915_power_domains_init_hw(dc, 1)` → DMC の再 load）、`_resume_end`（INIT を放し DC6 を許す）。
  `display->suspend_dc9`。
- `ggtt.c`: `drv_i915_gt_ggtt_restore`（Q1 の判断 1: 毎回全部書き直す。両 window の全 page を scratch → pool の bind 済みの object の全 page →
  借りた firmware の範囲）。`memory.h` の `foreign[4]` に借りた範囲を bind の時に覚え、unbind で忘れる。
- `device.c`: `drv_i915_device_suspend_hw`（park の後: display の begin → forcewake を取って割り込みを reset → display の end（DC9）。拒否なら
  割り込みを戻して報告）、`drv_i915_device_resume_hw`（unpark の前: display の resume begin → GGTT の書き直し → forcewake を取り GT の resume
  （engine・workaround・MOCS・RC6）→ fence の clear → 割り込みの postinstall → forcewake を放す → display の resume end）。
- まだ suspend の op には結線していない（段 (c)）。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| DC9 の判断の host の試験 | `make -C plan/ws052/tests i915-dc9 && build/ws052/host/i915-dc9` | PASS（入る・出るの全条件、PCH の bit の立て下ろしと他の bit の保持） |
| GGTT の書き直しの host の試験（ASan/UBSan） | `make -C plan/ws052/tests i915-ggtt && build/ws052/host/i915-ggtt` | PASS（GT window の 2 object・guard 付きの display object・借りた範囲を bind → table を壊す → restore で全 entry が一致、unbind した object と返した範囲は scratch、借りた範囲の記録が消える、未準備の memory は 0） |
| park の host の試験 | `build/ws052/host/i915-park` | PASS |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| 規約 | `style-check.py`（新しい file と試験は 0、変えた device.c・display.c・ggtt.c・clock.c・dmc.c・phy.c・power.c は新しい指摘 0） | PASS |
| 実機 | — | 未実施（段 (c) の後、5330 の UAT） |

残り（段 (c) で扱う）:
- i915 の `suspend`・`resume` の op の結線: suspend = `drv_i915_worker_park` → `drv_i915_device_suspend_hw`（失敗なら unpark して中止の理由に）、
  resume = `drv_i915_device_resume_hw` → `drv_i915_worker_unpark`。
- **UAT の手順に明記すること（2026-10-05 Q1）**: 窓を出て**同じ lease のまま**窓に入り直す道は新しい（今まで hold が切れて次の lease が入る道だけ
  だった）。手順: Keiland の session で画面を出したまま `sleepctl devices` → 画面が消えて戻り、同じ session のまま描画が続く（login し直さない）。
- DMC の program の無効化（Linux の `intel_dmc_disable_program`）は core の uninit で行っていない（DC9 で消え、resume で再 load する）。
- hotplug・opregion の suspend・resume は扱っていない（今の driver が 1 出力で hotplug が無い）。monitor が sleep の間に抜かれた時の内蔵の panel
  への切り替え（Q1 の判断 4）は段 (c) の窓の再入で扱う。

## 段 (c): suspend・resume の op の結線、決定 4、UAT の手順（2026-10-05）

- `i915.c`: PCI の driver の `suspend`・`resume` の欄に `i915_suspend`・`i915_resume`。
  - suspend: `drv_i915_worker_park(device, 2000 ms)` → `drv_i915_device_suspend_hw`。park しない（EBUSY）なら中止、hardware が下りない
    （DC9 の拒否など）なら `drv_i915_worker_unpark` して中止。どちらも pci-power が原因の device として `pci 0000:00:02.0 i915` を返す。serving で
    ない device（start が無い・失敗した）は保つ状態が無いので 0、start の実行中は EBUSY。`device->suspended` を立てる。
  - resume: `suspended` の時だけ `drv_i915_device_resume_hw` → `drv_i915_worker_unpark`（hardware の段が失敗しても worker は serve に戻し、失敗を
    返す）。
- `display/present.c`（決定 4）: resume の後の最初の窓（`display->window.after_resume`、`drv_i915_display_resume_end` が立てる）で HDMI の
  resident run が失敗したら、sleep の間に抜かれたとみなして log を出し、`display->output.hdmi = 0` にして内蔵の panel で 1 度だけ試す。点かなければ
  今までどおり `display_failed`（presentation が失敗し続ける）で、resume そのものには影響しない。
- 呼び出し元: p004 の `KERN_SYSTEM_SLEEP` の devices だけの mode（`sleepctl devices`）。p006 の S0i3 の入口・出口も同じ口を使う。

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| vmunix の link | `make vmunix` | PASS、warning 0 |
| host の試験（park・dc9・ggtt・pci-power） | `make -C plan/ws052/tests i915-park i915-dc9 i915-ggtt pci-power` と各実行 | PASS（段 (c) の結線は host の試験の対象外: 実機の UAT で見る） |
| 規約 | `style-check.py`（i915.c・present.c・display.c は新しい指摘 0） | PASS |
| QEMU | — | 対象外（QEMU に i915 は無い。p004 の試験は i915 の無い q35） |
| 実機（5330） | 下の UAT | 未実施 |

### 5330 の UAT の手順（ws159-p005 にそのまま追記できる形）

前提:
- image に `sleepctl` が要る。ws159 の `plan/ws159/tests/config-amd64-uat.mk` に `ZEDBSD_USER_PROGRAMS += sleepctl` を足す（WS159 の file なので
  Q1 が足す）。
- `sleepctl devices` は全 device を suspend して直ちに resume する（CPU の深い idle・S0i3 には入らない。それは p006）。
- p005（止めて入る）の前なので、suspend は **suspend の口の無い最初の driver**（5330 では LPSS-I2C の touchpad・HDA・Wi-Fi のどれか）で中止し、
  それより前に suspend された device は巻き戻しで resume される。i915（`0000:00:02.0`）は bus 0 の先頭なので、bridge の先の device（NVMe など）の
  後、bus 0 の他の device より前に suspend される。中止の device が i915 より後なら、i915 の suspend→resume は巻き戻しで必ず通る。
- **新しい道（2026-10-05 Q1 の注記）**: 窓を出て**同じ lease のまま**窓に入り直すのは、今回初めて通る道である（今までは hold が切れて次の lease が
  入る道だけ）。画面を出したままの session で行い、login し直さずに描画が続くことを確かめる。

## 4. sleep の device の suspend・resume（WS052 p004・p009）

| # | 操作 | 期待 | 証拠 |
| --- | --- | --- | --- |
| 4.1 | 内蔵の panel で kei で login し、Terminal と、動き続ける app（Gears など）を開いておく。Terminal で `sudo sleepctl devices` | 画面が一瞬消えて**同じ panel に同じ session が戻る**（greeter に戻らない、login し直さない）。Gears が動き続け、Terminal に打てる。Terminal の行は `sleep result=21 resume=0 device=pci 0000:00:XX.X NAME`（21 = EOPNOTSUPP、NAME は suspend の口の無い driver。p005 の後は `result=0`）。`device=` が i915 より前（`0000:00:02.0` より前の bus 0 の番号、または bridge の先）なら i915 は通っていないので、その旨を記録 | `sudo dmesg` の `i915: park: the GT may idle`・`i915: DC9: entered`・`i915: suspend: the hardware is down (interrupts off, display in DC9)`・`i915: DC9: left`・`i915: dmc: program loaded again after the resume`・`i915: resume: N GGTT entries written again`・`i915: resume: the hardware is back`・`i915: unpark: the GT serves again`・`i915: resident display: ended PASS`（窓を出た分）、`nvme: suspended`・`nvme: resumed`、`pci: suspend of … failed (error 21)` |
| 4.2 | 4.1 を続けて 5 回 | 毎回同じ。disk の読み書き（Files で file を開く・保存）と network（Browser で頁を開く）が続く | 各回の dmesg の `system: sleep (devices): result … resume …` |
| 4.3 | 4.1 で画面が戻らない、または Gears が止まる | 失敗。電源ボタンの長押しで切る前に、可能なら SSH で `dmesg` を取る（`i915: resident display: the panel did not come up`・`i915: resume: a step failed`・`i915: DC9: not entered/left`・`i915: park: the worker did not park` を探す） | dmesg |
| 4.4 | （HDMI の monitor がある時）firmware が HDMI を点けた起動（HDMI を挿して電源を入れる）で 4.1 | 画面が**HDMI に**戻る（panel に移らない） | dmesg の `i915: display output: HDMI on DDI B …`・`i915: resident display: ended PASS` |

未実施にする項目（p006 の後）: sleep の間に HDMI を抜いて内蔵の panel に移る（決定 4）は、devices だけの mode では sleep が一瞬なので試せない。S0i3 に
入れるようになってから（p006）試す。RC6 の residency の増加と SLP_S0 も p006 の後。
