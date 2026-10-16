<!-- awesome-plan project=zedbsd record=ws245-p050d1 -->

# ws245-p050d1: 内蔵 panel の sink の取得と kernel の panel の hook を xd に

Status: in-progress（2026-10-17 P1、q1229-i08: 実装・host 試験・build 済み（3604ddfa1）。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§2.9（AUX）、§2.11（panel power）、§3.4、§4.2。着手の手がかりは [phase050d](../phase050d/phase.md)「着手の手がかり」の 1〜3

## 範囲

- 新しい xd の部品（内蔵 panel の sink）: panel の電源の順序の状態（PPS の選び、遅延の program と待ち、VDD を誰が要るか、最後の電源の on/off と backlight off の時刻、遅れての VDD off）、sink の取得（PPS の用意 → VDD → DPCD の能力 → eDP の DPCD の版 → EDID を AUX の I2C で）、panel の操作（on・off・VDD on・VDD off・backlight の bit）、DPCD の read/write（AUX の power と VDD を保ったまま xd-aux の native の転送）。
- kernel の道の付け替え: `dp-sink.c` の `drv_i915_edp_device_init_connector/_fini` の eDP の部分、modeset の kernel の backend の hook（`drv_i915_edp_emit_dpcd_read/_write/_read_dpcd_caps/_panel`）を xd の部品に。遅れての VDD off の timer と AUX の power の参照は殻（kernel の環境）が xd に渡す。
- 残す物: 旧の Linux の eDP の world（`drv_i915_edp_begin/_init_late/_end/_panel_op`、`panel.c`・`aux.c` の Linux の転送、`edid-read.c`）は host の旧試験（host-dp-test・host-lcd-modeset-test）と ktest が使う間は残す（kernel からは呼ばれない）。消すのは p050d4（oracle の check を xd の試験へ移してから）。DP-ext（p052）が使う eDP device の lock・thread（`drv_i915_dp_kernel_*`）は残す。

## 受け入れ

- 新しい xd の host 試験（run-xd-host-tests.sh）: host-dp-test の eDP の oracle（PPS の選び、DPCD の能力と eDP の版、VDD を転送の間保つ、最初の転送の前の power-up の待ち、遅れての VDD off の時刻（5 × power cycle）、power cycle の待ち（T12）、AUX の retry の有限、native の DEFER・NACK、短い返事、I2C の DEFER・部分の read、短い EDID と全部 0 の EDID の拒み、port A 以外と Type-C の AUX の拒み）を xd の形で。
- build（既定と I915_TESTS=y の 5 組）warning 0、旧 host 試験（dp 72・lcd 120）と harness out50c が前の段と同じ（kernel の道だけを変えるので）。R11 の数。
- 5330 の回帰は全部の段の後に 1 回（ユーザーの決定 2026-10-16）。

## 変えた物（2026-10-17 P1、3604ddfa1）

| file | 中身 |
| --- | --- |
| `display/xd-edp.c/.h`（新） | 内蔵 panel の sink: `struct i915_xd_edp`（PPS の選び、firmware・VBT・規格の遅延と待ち、初期の遅延の間は VDD を保つ印、VDD を誰が要るかと VDD が持つ AUX の power、最後の電源の off・on と backlight off の時刻、能力・eDP の能力・EDID）、環境 `struct i915_xd_edp_env`（時計、power の get/put/put_later（用途 CORE・AUX）、lock（PPS・AUX）、遅れての VDD off の timer の arm/cancel、notes）。`drv_i915_xd_edp_start`（PPS の選び → 遅延の program → firmware の VDD の引き取り → 能力（拡張の能力を含む）→ eDP の能力 → EDID、失敗は VDD をすぐ off）、`_late`（VBT の sequencer、最終の遅延、遅れての VDD off）、`_stop`、`_panel`（on・off・VDD on・VDD off・backlight on/off と待ち）、`_vdd_work`、`_dpcd_read/_write`・`_read_caps`（AUX の lock → AUX の power → PPS の lock → VDD → xd-aux → 逆順、AUX の power は later） |
| `display/xd-aux.c/.h` | I2C over AUX: `drv_i915_xd_aux_i2c`（message ごとに address だけ → 16 byte までの chunk（sink が少なく動かしたら以後小さく）→ 最後に MOT 無しの address、native の DEFER・I2C の DEFER（最大 7 回の追加）・短い write の status 要求、tries は I2C の速さから最少 7） |
| `display/xd-edid.c/.h` | EDID の読み: `struct i915_xd_edid_ddc`（block を読む hook）、`drv_i915_xd_edid_read`（block ごとに 4 回まで、転送ごとに 5 回まで、最初の読みが全部 0 は ENXIO、checksum と header、拡張は読めた所まで） |
| `intel/display-dpcd-xd.h`、`tests/display/xd-regs-map.h` | I2C の request・reply の code、拡張の能力の bit と番地、eDP の能力の番地と大きさ（Values referenced、Linux との対 12） |
| `display/dp-sink.c`（殻） | `drv_i915_edp_device_init_connector` は xd-edp の start・late（`i915_edp_device_take`: 環境を device の mutex・timer・power domains に、sequencer の数と muxed は xd の PCH の種類から、VBT の早い遅延）、結果を `dev->res` に写す（`i915_edp_device_result`: modeset.c・LCD-A が読む）、`_fini` は `drv_i915_xd_edp_stop`、遅れての VDD off の body は `drv_i915_xd_edp_vdd_work`。ログを xd の値で短く |
| `display/aux.c`・`panel.c`（殻） | modeset の kernel の hook `drv_i915_edp_emit_dpcd_read/_write/_read_dpcd_caps/_panel` は xd-edp を呼ぶ（error は Linux の errno に） |
| `display/internal.h` | `struct i915_edp_device` に `xe` |
| `tests/display/host-xd-edp-test.c`（新）、`run-xd-host-tests.sh`、`platform/amd64/vmunix.mk` | 41 check（下）。runner と vmunix に足した |

段の phase.md の必須の項目:
- 消す file: 無し。消す関数: dp-sink.c の `i915_edp_log_pps`（呼び手 0）。旧の Linux の eDP の world（`drv_i915_edp_begin/_init_late/_end/_panel_op/_dpcd_read/_dpcd_write/_read_dpcd_caps/_work_run`、`panel.c` の `drv_i915_pps_*`、`aux.c` の Linux の転送、`edid-read.c`）は kernel から呼ばれなくなったが、host-dp-test・host-lcd-modeset-test・dp-fake-hw・lcd-fake-hw と ktest（edp-ktest・edp-sync-ktest・lcd-modeset-ktest・lcd-show-ktest）が使うので p050d4 まで残す（vmunix に入ったまま）。
- 殻が更新する旧の写し: `dev->res`（dpcd・edp_dpcd・edid・edid_blocks/extensions/ok・pps_idx・遅延）。読む側: modeset.c の panel の check（`res.edid`・`res.dpcd`・`res.edp_dpcd`）、LCD-A（state.c の compute）。p050d2・d4 まで。
- 表 R: 内蔵 panel の PPS（PP_CONTROL・遅延）と AUX A の書き手は kernel では xd-edp（xd-panel・xd-aux を直に）。窓は `dev->k.aux_io`（記録しない窓、PPS の mutex の下）。
- 殻が xd に渡す hook と取る lock: power は `drv_i915_display_power_get/_put/_put_async`（power domains の mutex）、lock は device の PPS・AUX の mutex（AUX → PPS の順）、遅れての VDD off は device の timer queue（body は worker thread で PPS の lock を取る）。
- R11: 触った file の開発の管理情報は全部 0（vmunix.mk・runner は元からの行）。
- build の組: 既定と `I915_TESTS=y` の 5 組。

## 旧と同じに保った物と違い

- kernel の道だけを変えたので、旧の host 試験と harness は前の段（b0c289c9d）と同じ: `sh build/tmp/stagecmp.sh b0c289c9d d1cmp` → dp SAME、lcd の trnorm の差 0、out50c の差 0。
- 違い（kernel）: (1) Linux は AUX の message ごとに power・PPS の lock・VDD を取ったが、xd-edp は 1 回の DPCD の読み書き（retry を含む）・EDID の 1 block の I2C の transaction ごとに取る。(2) 旧の診断の読み（DPCD 0x100 の link の設定、Linux は読まない）をやめた。(3) VDD の AUX の power は cookie 1 で数え、put は cookie を見ない kernel の put。(4) PPS の DPLS の WA は display 13 以上（旧は dp world の固定の 13）。(5) ログの行は短くした（`i915: edp acquire:` の項目が変わった。読む道具は無い: grep で確かめた）。
- 未試験: I2C の部分の read（sink が少ない byte を返した時に続きから読む）は fake が data の語を詰め直さないので host で試していない。segment pointer（2 block 目以降）は fake が持たない。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- 起動の `i915: edp acquire: error=0`、`edp DPCD 000:` の値と `edp EDID sha256` が基準と同じ。内蔵 LCD が点く（survey の PP_STATUS on）。`i915: edp late:` の `off in 3000 ms`。
- suspend → resume（節目、ユーザーの一覧）で panel が戻る。

## 確かめた事

- host（2026-10-17）: `sh plan/ws245/tests/run-xd-host-tests.sh build/d1/xd-host` → 全部 pass（edp 41、regs 635 ほか）、`I915_XD_STRICT` と kind の grep も通る。
- 旧の host 試験と harness: 上のとおり同じ。
- build: `sh build/tmp/kbuild.sh d1` で 6 組 rc 0・warning 0（style の直しの後に既定を作り直して rc 0・warning 0）。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
