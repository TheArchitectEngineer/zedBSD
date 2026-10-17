<!-- awesome-plan project=zedbsd record=ws245-p023 -->

# ws245-p023: power well の操作（W1、xd-power）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、…」、Q1 経由）。それまで cleared にしない。design では「ユーザーが居る時間に流す」段）
Parent: [WS245](../ws.md)
Queue: q1229-i01（P1、ws245-p022〜p046）
設計: [design.md](../design.md) §6.2 の p023 の行、§2.5、§6.7（p006 の保存した `xd-power.c` を当て直す）

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-power.c/.h`（新、Zlib） | well の操作だけ（domain の map と refcount・DC・DC9 は p024・p025）: `drv_i915_xd_power_find`（control と index で platform の表の行）、`_well_enable`（PW_1 は FLR source の bit を platform の WA の時だけ → PG0 の fuse → TC の AUX は channel の TBT_IO の routing → request → state を待つ（来なければ report に載せて続ける）→ DP の TC AUX は PHY の health の hook → 自分の PG の fuse（遅れは report）→ VGA の hook → pipe の割り込みの hook）、`_well_disable`（pipe の drain の hook、失敗は EBUSY で request を残す → request を消す → BIOS が要求していなければ state の消えを待つ）、`_well_owned`（request と state の両方）、`_well_adopt`（BIOS の request の引き継ぎ: driver を先に立て BIOS を後で消す）。hook は `struct i915_xd_power_hooks`（pipes_up・pipes_down・vga_reset・tc_health）、遅れは `struct i915_xd_well_report`（state_late・fuses_late）。§6.7 の保存した `xd-power.c` の enable/disable の部分を当て直し、Linux と違っていた所（state が来ない時に request を下げて ETIMEDOUT にしていた）は Linux（warn して続ける）に合わせた。手順は block ごとに `Procedure referenced:`、待ちの時間は `Values referenced:` |
| `display/power.c`（殻） | `drv_i915_power_well_enable/_disable/_is_enabled/_sync_hw` の request の well の中身を xd-power に。殻に残る物: always-on と DC_off の枝（p025 まで）、`irq_sync_failed` の後の拒み、hook の実装（`i915_pw_hook_pipes_up/_down`（`irqs_enabled` と数と drain の失敗の log）・`_vga_reset`（`drv_i915_vga_reset_io_mem` と数）・`_tc_health`（旧の Dekel の窓 `drv_i915_dkl_phy_wait_set` と数と log: 表 R の HIP_INDEX、窓の lock は 1 つ）、ack の timeout と fuse の遅れの log と数、`hw_enabled` の写し。旧の well の `ops`・`hsw_idx` から xd の行を引く `i915_pw_xd_well`。削除: `i915_pw_req`・`i915_pw_wait_fuse`・`i915_pw_post_enable`・`i915_pw_tc_aux_port`・`i915_pw_tc_aux_route`・`i915_pw_tc_uc_health_wait`（hook に作り替え）と `SKL_FUSE_STATUS`・`SKL_PG0/1`・`GEN8_CHICKEN_DCPR_1`・`DISABLE_FLR_SRC`・`I915_TC_AUX_CH_CTL_*` の define。R11: 8 → 0（`P3` の log の段の符号 3、`ws052-p009` 3、`BUG-256` 1、comment の段の符号 1） |
| `display/internal.h` | `struct i915_pw_ctx` に `struct i915_xd *xd`（window と platform の記述） |
| `display/display.c` | `display->pwc.xd = &display->xd`（core init で） |
| `platform/amd64/vmunix.mk` | i915 の行に `xd-power.c` |
| `tests/display/host-xd-power-test.c`（新） | 24 の check（下） |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-power-test` を足した |

段の phase.md の必須の項目:
- 消す file: 無し（`power.c` は殻。domain の map と refcount は p024、DC は p025、init の列は p035 で xd に）。`dkl-phy.c` は p040 まで残す。
- 消す関数と残る呼び手: 上の power.c の static 6 個（呼び手は power.c の中だけ → 0）。`i915_pw_driver_reg`・`i915_pw_state` は `drv_i915_power_domain_hw_state_on`（domain の読み、p024）が使うので残る。
- 殻が更新する旧の写し: `struct i915_power_well` の `hw_enabled`、`pwc` の `ack_timeouts`・`vga_reset_calls`・`irq_post_enable_calls`・`irq_pre_disable_calls`・`irq_sync_failed`・`disable_refusals`・`tc_uc_health_timeouts`。下の scratch の harness で旧と同じ数を見た。
- 表 R: PWR_WELL_CTL_AUX（p023〜p024）は ops だけ xd、refcount は旧が持ち ops を呼ぶ（設計どおり）。HIP_INDEX は uc_health を殻の hook で旧の窓（`display->dkl.lock`）。
- hook と lock: 殻が power domain の lock の下で xd を呼ぶ（呼ぶ側は旧のまま）。xd-power は lock を取らない。tc_health の hook は旧の Dekel の窓の lock を取る（`drv_i915_dkl_phy_wait_set`）。
- `plan/ws051/tests/host-tc.sh`（power.c の domain の関数を sed で取る）: 取る 3 つの関数（`drv_i915_aux_*_power_domain`）は変えていないので壊れない（未実行: host の Type-C の試験は別の物で、今回の差に触れない）。
- R11: power.c 8 → 0、xd-power.c/.h 0、host-xd-power-test.c 0、internal.h 0、display.c 0。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）・`host-lcd-modeset-test`（123）の出力は p022 の後と同じ（well の操作を通らない）。
- 確かめ方 2（scratch の harness、git に入れない: `zz-wells.c`、`-Wl,--wrap` で MMIO と wait を register の model に）: `drv_i915_power_domains_init` の map の全部の well（ADL-P 28・TGL 23、always-on と DC_off を除く）に enable → disable → BIOS の request の引き継ぎ（sync_hw）→ disable を流し、書き込みの列と `hw_enabled` と数を段の前（92ee105c0 を `git archive`）と後で比べた。**ADL-P は全部一致**（360 行）。**TGL は 1 行だけ違う**: PW_1 の enable の前の `CHICKEN_DCPR_1` の FLR source の bit（0x46430 ← 0x8000）を書かなくなった。
- **違い（意図した物、Linux を優先: 決定 4）**: Wa_16013190616 は Linux v6.8.12 では `IS_ALDERLAKE_P` の時だけ（`hsw_power_well_enable`）。旧は display 12 にも書いていた。xd は platform の表の `I915_XD_WA_PW1_FLR_SOURCE`（ADL-P 系の行だけ）で決める。
- 違い: state の待ちは表の `enable_timeout_ms`（Linux の値: 既定 1 ms、AUX_USBC 500 ms）。旧は既定 2 ms（AUX_USBC は旧の表でも 500 ms）。fuse の待ち 1 ms は同じ。disable の state の待ちは 1 ms（旧 2 ms）。どちらも遅れは log だけで続ける。
- 違い: 待ちは xd-io の poll（1 µs の spin から 100 µs の sleep まで）で、旧の `drv_i915_wait_reg` ではない。PG0 の fuse の遅れの log は request の後にまとめて出る（旧は request の前）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- survey の `PWR_WELL_CTL2` = 0x00000c03（PW_1 + PW_A）、well の timeout の log（`enable ACK timeout`・`fuse PG… wait timeout`）が無い。
- 5320（TGL）は節目でユーザーの一覧: PW_1 の FLR source の bit を書かなくなった事で起動が変わらない事。
- TC の AUX（USB-C DP）は p024 の節目でユーザーの一覧。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p023/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 24、失敗 0。`I915_XD_STRICT` の検査（xd-power.c を含む）は通る。
- power の check: ADL-P の表の 29 の well が 5330 の debugfs の一覧と同じ順、PW_1（index 0・PG1）、PW_2（index 1・PG2・VGA）、PW_A〜D（index 5〜8・PG6〜9・pipe の割り込み A〜D）、AUX_USBC1 の 500 ms、AUX_TBT2（index 10）、find の 2 つ、PW_1 の書き込み（FLR → request）と owned、PG0/PG1 の fuse の遅れの report、PW_2 の VGA、PW_A が PW_2 無しで上がり pipe A の割り込みを戻す、state が来ない時は report して request は残す、DC_off は EINVAL、AUX_A は request だけ、AUX_USBC2 の routing（TBT_IO を落とす）→ request → TC2 の health、AUX_TBT3 の routing（TBT_IO を立てる）と health 無しと state の遅れ、PW_B の drain → off、drain の失敗で PW_C を落とさない、BIOS が要求中の well は driver の request だけ消える、BIOS の request の引き継ぎ（driver → BIOS の順）、BIOS の要求が無ければ書かない。
- build: 下。
- 5330: 未実施。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021`（`build/p021/k-*` の使い回し）で既定の vmunix（sha256 de0d4cfd2075b749…）と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
