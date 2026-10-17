<!-- awesome-plan project=zedbsd record=ws245-p030 -->

# ws245-p030: DDI の buffer（xd-ddi: BUF_CTL・DP_TP・idle）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i03（P1、ws245-p028 から残りの段を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p030 の行、§2.3・§2.4、表 R の DDI_BUF_CTL の行

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-ddi.c/.h`（Zlib） | DDI の buffer: `struct i915_xd_ddi_link`（DDI、transport の transcoder、Type-C か、lane・rate、port が保つ bit、enhanced framing・MST・FEC）。`drv_i915_xd_ddi_dp_word`（保つ bit（所有 bit は除く）、port width（3 lane は 4）、display 13 の Type-C は PHY link rate の code）・`_dp_start`（走っている link は buffer → transport → idle の待ちで止める → transport を pattern 1（SST は enhanced、MST）→ 呼ぶ側の手順（TBT の WA）→ buffer（Type-C は今の所有 bit を保つ）→ posting read → active の待ち）・`_pattern`・`_idle_patterns`（idle の pattern と DP_TP_STATUS の待ち 1 ms）・`_hdmi_start`（保つ bit と enable、display 13 の Type-C は所有 bit）・`_stop`（buffer を消す → DP の transport → FEC と posting read → idle の待ち）。待ち: idle 8 µs、active 1 ms（Type-C 3 ms）。`Procedure referenced:`（intel_ddi.c の intel_ddi_init_dp_buf_reg・intel_ddi_prepare_link_retrain・intel_ddi_set_link_train・intel_ddi_set_idle_link_train・intel_enable_ddi_hdmi・disable_ddi_buf・intel_ddi_disable_fec・intel_wait_ddi_buf_idle・intel_wait_ddi_buf_active）、rate の code と待ちの時間は `Values referenced:` |
| `display/ddi.c`（殻） | `i915_ddi_init_dp_buf_reg`・`_prepare_link_retrain`・`_set_link_train`・`_set_idle_link_train`・`i915_disable_ddi_buf_ctl`・HDMI の buffer の enable を xd に。殻に残る物: crtc state と port から `struct i915_xd_ddi_link` を作る `i915_ddi_xd_link`、ADL-P の Type-C の DP-alt/legacy の時の `i915_adlp_tbt_to_dp_alt_switch_wa`（旧の Dekel の窓、表 R）を xd の手順の hook に渡す `i915_ddi_tbt_to_dp_alt_hook`、待ちの遅れを旧の文の error にする `i915_ddi_report_late`、`intel_dp->DP` の写し（ADL-P の Type-C で TBT でない時は所有 bit を足して旧と同じ値）。削除: `i915_ddi_buf_phy_link_rate`・`i915_dp_tp_status_reg`・`i915_wait_ddi_buf_idle`・`i915_wait_ddi_buf_active`・`i915_ddi_disable_fec` |
| `intel/display-regs-xd.h`・`tests/display/xd-regs-map.h` | DP_TP_CTL の FEC と MST、DDI_BUF_CTL の translation select と reversal（`Values referenced:`）と 4 つの対 |
| `tests/display/host-xd-ddi-test.c` | 13 の check を足した（下）、計 39 |
| `tests/display/host-lcd-modeset-test.c` | E（time base の fault）を、xd に移って backend の wait_reg を通らなくなった DP_TP_STATUS の待ちから、stop の pipe-off の待ち（0x70008、旧の pipe.c）に移した: fault の時に -EIO と最初の異常、stop が確かめられず retained、model を捨てると戻る（124 check）。R11: comment の `plan/ws031/tests/…` の参照を消した |

段の phase.md の必須の項目:
- 消す file: 無し。
- 消した関数（本番の呼び手 0）: ddi.c の static 5（上）。`i915_dp_tp_ctl_reg` は readout（get_config の DP_TP_CTL の読み）に残る（p036 で）。
- 殻が更新する旧の写し: `intel_dp->DP`（enable を含む。lcd 試験の `ddi_buf_ctl_value == 0x80000002` が見る）。
- 表 R（DDI_BUF_CTL の bit 6 と reversal）: xd-ddi は Type-C の port で今の bit 6 を読み、その値を保って書く（所有は tc.c が rmw で持つ）。reversal は殻の `saved_port_bits` から語に入る。HDMI の display 13 Type-C（legacy）は Linux のとおり bit 6 を立てる。
- hook と lock: modeset の lock の下で `drv_i915_lcd_xd_io` の window。TBT の WA の hook は旧の Dekel の窓（`display->dkl.lock`）を使う（xd-ddi は窓を持たない）。
- R11: ddi.c 0、host-lcd-modeset-test.c 1 → 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験 `host-dp-test`（72）は段の前（05775ce1f の `git archive`）と出力が同じ、`host-lcd-modeset-test` は rmw を書いた値に直した書き込みの列が**全部一致**（DP_TP_CTL・DDI_BUF_CTL の enable と disable を含む）。違いは E の書き直しだけ（上）。
- 違い: Type-C の DP の buffer の bit 6 は、語ではなく今の register の値に従う（Linux は計算した語で上書きする。TBT-alt でない時は tc.c が既に所有を取っているので同じ値になる見込み。TBT-alt の時は Linux は bit 6 を消して書き、xd は残す: 実機で確かめる事）。
- 違い: 知らない training pattern の値は pattern 1（旧は field を消すだけで、0 = pattern 1 と同じ値）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- eDP の訓練が通り（`link training` の行、CR/EQ）、`Timeout waiting for DDI BUF` と `Timed out waiting for DP idle patterns` の行が無い。survey の DDI_BUF_CTL(A) と TRANS_CONF が基準と同じ。
- 節目（ユーザーの一覧）: 5320 の起動。外部出力（HDMI = DDI B、USB-C DP = DDI 4 の所有 bit）: 未確認（p031 の節目で）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p030/xd-host` → 全部 pass（ddi 39）、`I915_XD_STRICT` の検査は通る。
- ddi の buffer の check: 5330 の eDP の語 0x00000002（Linux の 0x80000002 から enable を除いた物）、start の列（DP_TP_CTL 0x80040000 → 手順 → DDI_BUF_CTL 0x80000002、active を待つ）、走っている link の止めと再開（buffer off → transport off → idle → 再開）、pattern 2・normal・idle（と送られない時の遅れ）、FEC つきの stop、display 13 の Type-C の語（5.4 GHz の code 2、4 lane、reversal、所有 bit を除く）、start で tc.c が立てた bit 6 を保つ・立っていなければ立てない、HDMI の Type-C の buffer、idle のままの buffer は 3 ms で遅れ。
- `sh plan/ws245/tests/check-xd-regs-linux.sh build/ws229-p001/linux/linux-6.8.12 build/p030/linuxcheck` → 1027 対が一致、差 0。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p030` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
