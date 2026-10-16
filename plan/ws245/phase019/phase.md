<!-- awesome-plan project=zedbsd record=ws245-p019 -->

# ws245-p019: AUX の transaction（xd-aux）

Status: in-progress（q1225-i02 P1: 実装・host 試験・build は済み（343c8516a）。5330 の素の起動の回帰（基準 p053 と p019 の `i915.debug=display` の 2 回、全部の書き込みの列の比べ）の依頼文を Q1 に送った（2026-10-16 ユーザー「この作業の間、P1のテストはT1にオフロードして、T1でシリアライズして競合しないように実行しましょう。」）。T1 の結果が出るまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1225 / q1225-i01（調べだけで返却）、q1225-i02（実装）
設計: [design.md](../design.md) §6.2 の p019 の行（改訂 6）、§2.9・§3.2 の xd-aux

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-aux.c/.h`（新、Zlib） | 1 つの channel の 1 つの transaction: `drv_i915_xd_aux_init`（channel 0〜8、他は EINVAL）、`drv_i915_xd_aux_control`（制御の語）、`drv_i915_xd_aux_exchange`（前の transaction の busy を 1 ms おきに 3 回見る → 大きさの検査 → 最大 5 回の try → 結果の分類 → reply の unload）。結果は `I915_XD_AUX_SENT/BUSY/TOO_LONG/UNFINISHED/RECEIVE_ERROR/NO_ANSWER/BAD_SIZE` と最後の制御の語と reply の byte 数。try が 10 ms で終わらない時は owner の `stalled` を呼ぶ（旧の「did not complete within 10 ms」の報告をその場で出すため）。数（transactions・tries・no_answers・receive_errors・stalls）を持つ。register には xd-io だけで触る。値と手順は block ごとに `Values referenced:`・`Procedure referenced:`（Linux v6.8.12 intel_dp_aux.c の intel_dp_aux_xfer・intel_dp_aux_wait_done・skl_get_aux_send_ctl・intel_dp_aux_sync_len・intel_dp_aux_fw_sync_len） |
| `display/aux.c`（殻） | pack/unpack・wait_done・clock divider・sync の長さ・send control・_send・_xfer_locked・TGL の CTL/DATA の番地の選びを削除。`_xfer`（TC の lock、AUX の power domain、PPS の lock、VDD、latency の要求）は残し、中で `i915_dp_aux_exchange` が xd-aux を呼んで、結果を旧の errno（-EBUSY・-E2BIG・-EIO・-ETIMEDOUT）と旧と同じ文の log（not started の WARN は status が変わった時だけ、not done・receive error・stall は `i915_dp_aux_report`、timeout と forbidden size は debug）に直す。HDCP の Aksv の flag は int に。`drv_i915_dp_aux_init` が port の `aux_ch` から channel の番号を決め（無い channel は MISSING_CASE で A）xd-aux を用意する。R11: 1 → 0 |
| `display/dp-internal.h` | `struct intel_dp` の 4 つの関数 pointer（clock divider・send control・CTL/DATA の番地）を外し `struct i915_xd_aux xd_aux` を足した。`xd-aux.h` を include。R11: 3 → 0 |
| `display/internal.h` | `struct i915_dp_env` に `aux_io`（AUX の transaction が通る xd-io の窓。hook を結ぶ binding が一緒に結ぶ）、`struct i915_dp_kernel` に `aux_io`（同じ MMIO の窓、AUX の mutex の下で使う）。R11: 0 |
| `display/dp-sink.c` | `drv_i915_dp_kernel_sync_start` で `k->aux_io` を `k->mmio` で開き、`drv_i915_dp_kernel_bind` で `env->aux_io` に結ぶ（eDP と外部 DP の env が同じ `k` を共有するので AUX の mutex も窓も 1 つ）。R11: 0 |
| `intel/display-regs-xd.h` | AUX の制御の語に done の interrupt（bit 29）と HDCP の key の select（bit 15）を足し、`Values referenced:` に名前を足した |
| `tests/display/xd-regs-map.h` | 上の 2 つの対（`DP_AUX_CH_CTL_INTERRUPT`・`DP_AUX_CH_CTL_AUX_AKSV_SELECT`）。R11: 1 → 0（plan の path の言及） |
| `tests/display/xd-fake-regs.c/.h` | AUX の sink の model（`struct i915_xd_fake_aux`、`i915_xd_fake_aux_attach/_script`）: 制御の語の書き込みを channel の振る舞いで受ける（busy 中は無視、done・error の w1c、busy で transaction）。DPCD の native read/write、I2C の EEPROM（1 byte の write で offset）、答えの script（ACK・NACK・DEFER・TIMEOUT・DAMAGED・STUCK・SIZE）。規則の関数が書き込みの前の値を読む `i915_xd_fake_previous` |
| `tests/display/host-xd-aux-test.c`（新） | 43 の check: 制御の語（旧と同じ 0xfe40023f、TBT IO、HDCP、大きさ）、channel A と TC2 の register、native read（15 byte）・write（3 語目が部分）の書き込みの列、I2C の bare address・offset・16 byte の read、NACK・DEFER（native・I2C）は 1 回の try の reply、無応答 5 回（休み無し）、4 回無応答の後の答え、damaged 2 回（各 400 µs）の後の答え、damaged 5 回（2000 µs）、大きさ 0・21、25 ms の stuck（stall 2・try 4 で回復）、ずっと stuck（stall 5、50〜60 ms で UNFINISHED）、busy（3 回 1 ms おき、書き込み 0、読み 4）、21 byte の要求・reply の room、room より大きい reply |
| `tests/display/dp-fake-hw.c/.h` | model に AUX の窓 `aux_io`（model の register と時計への hook）を持たせ、`drv_i915_dp_fake_bind_env` が `env->aux_io` に結ぶ |
| `tests/display/edp-sync-ktest.c` | 手で組む env に model の `aux_io` を結ぶ |
| `tests/display/host-dp-test.c` | stuck 25 ms の check の `wait_timeouts >= 1`（env の wait_reg の数）を外した（AUX の待ちは xd-aux の物になり、stall の数は host-xd-aux-test が見る）。先頭の comment の plan の path（R11: 2 → 0） |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-aux-test` を足した |
| `platform/amd64/vmunix.mk` | i915 の行に `xd-aux.c` |

段の phase.md の必須の項目: 消す関数（上の aux.c の 11 個。どれも aux.c の static で、呼び手は aux.c の中だけ → 0）。殻が更新する旧の写し: `intel_dp->aux_busy_last_status`（not started の WARN の重複を抑える、殻に残した）。表 R・hook: 無し（AUX_CTL/DATA の書き手は段の後も 1 つ: xd-aux）。消す file: 無し。R11: aux.c 1・dp-internal.h 3・host-dp-test.c 2・xd-regs-map.h 1 → 0、他の触った C の file は 0（vmunix.mk は前の段と同じく対象外）。

## 旧と同じに保った物と違い

- 同じ: 書き込みの列（data の語 → 制御の語 → `status | DONE | TIMEOUT_ERROR | RECEIVE_ERROR`）、制御の語の値（BUSY・DONE・INTERRUPT・TIMEOUT_ERROR・TIMEOUT_MAX・RECEIVE_ERROR・SIZE・fast wake 18・sync 32、TBT IO、Aksv）、busy の 3 回 1 ms、try 5 回、timeout は休み無し、receive error は 400 µs、10 ms の待ち、結果の順（busy → 大きさ → not done → receive error → timeout → 受けた大きさ 0/21 超）と errno、log の文と報告の数。外側の再試行（dp-sink.c の native 32 回、I2C の defer）は旧のまま（p031・p048）。
- 確かめ方: 旧の host 試験（`host-dp-test.c` 72・`host-lcd-modeset-test.c` 123 の check、どちらも DP の model を通る）を段の前（fb66c84d0）と後で `-v` で流し、出力（821 行・392 行。AUX の制御の語 0xac00023f・report の数・timeout の 64 行を含む）が pointer の値を除いて一致。
- 違い（実機に効く物）:
  1. done の待ち: 旧は env の `wait_reg`（2 µs の spin の後は tick の sleep）、新は xd-io の poll（1 µs を 20 µs まで、10 µs を 200 µs まで spin、その後 100 µs の sleep、合計 10 ms）。AUX の transaction（数百 µs）は tick を待たずに終わる。busy の 1 ms と receive error の 400 µs の休みも xd-io（`kern_usleep_range`）で、`edp acquire` の行の `wait_timeouts`・`sleeps: tick=` に AUX の分は数えられない（素の起動では元々 0: T1-702 の p053 の dmesg で `wait_timeouts=0 … tick=0`）。
  2. fast wake の quirk（`QUIRK_FW_SYNC_LEN`、MTL の 1 機種）は旧で常に偽なので持たない。display 14 以降の POWER_REQUEST は範囲外。
  3. 無い channel の MISSING_CASE は transfer ごとではなく init の 1 回（VBT の parse が 0〜8 に限るので起きない）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p019/xd-host` → regs 495・io 48・platform 89・vbt 137・survey 57・aux 43、失敗 0。xd の .c（xd-aux.c を含む）の `I915_XD_STRICT` の検査は通る。native-decide 14、失敗 0。旧の `host-dp-test`（72）・`host-lcd-modeset-test`（123）は失敗 0 で、出力が段の前と一致（上）。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws245-dev build/ws245-dev/vmunix` warning 0（`-Werror`）。`I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組も warning 0。`git diff --check` は空。
- 段の kernel: `build/p019/vmunix-p019-343c8516a`（sha256 6cdd543d…）。基準: `build/p019/vmunix-p053-918265e98`（918265e98 から build、sha256 ea5b3427… = T1-702 の p053 の kernel と同じ binary）。
- 5330: 未実施（T1 に依頼。依頼文は Q1 に送った）。外部出力（USB-C DP の AUX の TBT IO）: 未確認（host 試験のみ、節目は p020）。
