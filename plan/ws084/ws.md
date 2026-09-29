<!-- awesome-plan project=zedbsd record=ws084 -->

# WS084: i915 の firmware の画面の引き継ぎ（素の実機の UEFI の起動でデスクトップを出す）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: —
Objectives: O1
Parent: [Master](../master.md)
Queue: なし（main が実装、2026-09-29 ユーザーの指示）
Resume point: p002 の 2 回目の修正（combo PHY の PLL の readout、HDMI の live status の log）、image `build/demo-hdmi5/hdd-image.img`。実機の再試験はユーザー待ち
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

素の 5330 を UEFI で起動すると、GOP が内蔵の LCD（pipe A）を点けたまま kernel に渡す。i915 の N0 の判定は active な pipe を
「takeover（N1）が未移植」として止め、GPU の node は display を持たず、compositor は `ENOTSUP` で終わっていた（ユーザーの実機の写真、
`i915: N0 decision: STOP ... a pipe is active (firmware display)`）。これまでの実機の試験は QEMU の passthrough で、firmware の画面が無いので
この経路を通らなかった。ユーザー:「ではWSを立ち上げて実装してください。display takeoverは以前に実験して動いた実績があり、難しくないと思います。
メインエージェントで実装してください。」

完了の条件: 素の 5330 を USB の image から UEFI で起動し、firmware の画面を引き継いで greeter（または自動の login の desktop）が内蔵の LCD と
HDMI の LCD に出る。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws084-p002 | bare metal の log（ユーザーが ssh で dmesg、下）で見つかった組み込みの不足を直す | incomplete（実装済み、実機は未実施） | p001 |
| ws084-p001 | N0 が active な pipe で止まらず `takeover` の印を付け、resident の display の開始が最初の書き込みの前に N1（readout + sanitize、`intel_crtc_disable_noatomic`、release）を走らせる。以前の parity の N1 の実機の手順（`4ab09939` の `parity_lcd_kernel.c`: 画面の object を仮の framebuffer で prepare → PLL の pool を空に → readout → takeover → release）に合わせる | incomplete（実装済み、実機は未実施） | — |

## p001 の記録（2026-09-29 main）

- `takeover.c` `drv_i915_native_decide`: active な pipe は止めない。他の条件（pipe の読み取りの誤り、GGTT の重なり、VT-d）は今までどおり止める。
  proceed で active な pipe があるとき `report.takeover = 1`。`internal.h` の `struct i915_native_report` に `takeover`。
- `modeset.c` `i915_resident_takeover`: resident の run の開始（preflight の前）で `display->n0.takeover` のとき、panel の cfg を
  `drv_i915_lcd_kernel_fill_cfg` で作り、仮の 640x480 の framebuffer で `drv_i915_lcd_modeset_prepare` → `drv_i915_lcd_dplls_reset` →
  `drv_i915_n1_readout` → `drv_i915_n1_takeover` → `drv_i915_n1_release` → PLL の pool と DBUF の状態を空に。readout と takeover の結果を log に出す。
- host 試験 `host-native-decide-test.c` の GOP の場面の期待値を「PROCEED、takeover」に変えた。**host 試験は未実施**: その build の道具
  （`plan/ws031/tests/display-host-lib.sh`、WS031 の片付けで削除済み）を git の履歴から出して流すと、`present.c` の `drv_i915_perf_*` が
  解決できず link できない（この変更と無関係の古さ）。
- kernel の build（main の config）: warning 0。USB の image `build/demo-takeover/hdd-image.img`。
- 実機: 未実施（ユーザーが試す）。QEMU の passthrough は firmware の画面が無いので、この経路の確認にならない。

## 危険と残り

- preflight は「pipe A の power well が切れている、vblank・underrun の割り込みが mask」を見る。takeover の後にそれが満たされないと、
  run は `LCD-B preflight` の log で止まる（そのときは takeover の後の power の扱いを直す）。
- display の core の初期化（CDCLK 等）は N0 の後、takeover の前に走る。parity の N1 も同じ順で実機で動いた。
- HDMI の主出力（display=auto で HDMI が見つかる）では、firmware の pipe A（eDP）を止めてから pipe B の HDMI を点ける。

## p002 の記録（2026-09-29 main）

ユーザーの実機（bare metal、`build/demo-hdmi3`）の dmesg:
- `N0 decision: PROCEED`、takeover の readout は `active pipes 0x1 ... DPLL-1 ... 0 kHz`、stop で `pipe_off wait timed out`・`Timeout waiting for DDI BUF to get idle`、
  preflight で `TRANSCONF 0x40000000`（enable は落ちたが state が active）→ `resident display: not started`。
- その前の P5 で `BIOS left unused DDI_IO_A power well enabled, disabling it`。parity の N1 の実機の記録（`4ab09939` の
  `plan/ws031/handover/notes/n1-implementation-state.md` の停止要因 1・4）と同じ: nogem の readout は動いている pipe の電源ドメインに参照を取らないので、
  firmware の画面の DDI IO・AUX の well が未使用に見えて落ち、pipe が止まれなくなる。P7 の `intel_power_domains_enable` の INIT の返却も同じ well を落とし得る。
- `display output: eDP panel (display=hdmi, but no HDMI sink is connected at boot: rc=13)`: HDMI の probe（EDID）が起動時の 1 回で未接続。

修正:
- `takeover.c` `drv_i915_modeset_sanitize_hw_state`: active な pipe があるとき well の sanitize をしない（takeover の readout が参照を取る）。
- `display.c` `i915_driver_register`: `n0.takeover` のとき `power_domains_enable` を遅らせる（`dprobe.power_domains_enable_deferred`、以前から宣言だけあった）。
- `modeset.c` resident の開始: takeover の成功の後に遅らせた `power_domains_enable` を行い、`n0.takeover` を消す（2 回目の lease で takeover をやり直さない）。
- `output.c`: `display=hdmi` のとき、未接続（EAGAIN）なら 250 ms ごとに最大 6 秒 probe をやり直す（USB 給電の LCD の controller が EDID に答えるまで）。`display=auto` は待たない。
- 検証: kernel と image の build（warning 0）、`plan/ws075/tests/hdmi/host-output-test.sh` 80 checks 0 failures（host の `<time.h>` を先に読む flag を足した）、
  QEMU の boot test PASS（`build/ws084-boot-test/login.png`、sshd 起動）。実機: 未実施。

### p002 の 2 回目（2026-09-29、demo-hdmi4 の実機の dmesg）

- well は残った（P7 で `DDI_IO_A hw_enabled=1`）が、takeover の stop はまだ `pipe_off wait timed out`、readout は `DPLL-1 ... 0 kHz`。
  原因: takeover の readout が encoder に `intel_ddi_get_config` を結び、combo PHY の `icl_ddi_combo_get_config`（PLL を読む）を結んでいなかった
  （`ddi.c` の「XXX: never bound」）。crtc の state に PLL が無いので sanitize が firmware の DPLL1 を止め、clock を失った pipe A が止まれない。
  parity の実機の run は preflight が RUNNING を通していたので表に出なかったと見る（推測）。
- 修正: `ddi.c` `drv_i915_lcd_ms_bind_readout` が combo PHY の port に `i915_icl_ddi_combo_get_config` を結ぶ（Linux の intel_ddi_init と同じ）。
- HDMI: 6 秒の再試験でも未接続。hotplug の割込みは DDI A だけで DDI B は無い。`hotplug.c` `drv_i915_hpd_probe_connector` が毎回 SDEISR と pin の bit を log に出す
  （live status が立たないのか、EDID が読めないのかを分ける）。
- 検証: build（warning 0）、QEMU の boot test PASS（`build/ws084-boot-test5/login.png`）。kernel 内の hotplug の試験（`tests/display/hpd-ktest.c` 等）は build の道具が無く未実施。実機: 未実施。
