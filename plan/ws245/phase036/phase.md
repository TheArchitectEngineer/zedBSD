<!-- awesome-plan project=zedbsd record=ws245-p036 -->

# ws245-p036: GOP の読み出しと判断（N0、xd-takeover）

Status: cleared（2026-10-18 Q1 判定: q1229-i20・i21（5330 の実機）でこの段の「実機で確かめる事」の 5330 の項目を満たした（「判定の材料」の節、P1 q1241）。外部出力・suspend などの残りは beta2.md の U19〜U23 で追う）（旧: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない））
Parent: [WS245](../ws.md)
Queue: q1229-i05（P1）
設計: [design.md](../design.md) §6.2 の p036 の行、§2.14

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-takeover.c/.h`（Zlib、新） | readout（読むだけ: PWR_WELL_CTL の state、pipe ごとに well が on なら TRANS_CONF・TRANS_DDI_FUNC_CTL・PIPE_SRCSZ・PLANE_CTL/STRIDE/SIZE/SURF、pipe A が読めれば combo PLL 2 つの enable・DDI_BUF_CTL A・PP_STATUS/CONTROL・SBLC_PWM_CTL/DUTY・DPCLKA_CFGCR0、VGA_CONTROL、MCHBAR の GFXVTBAR）、framebuffer の GGTT の範囲、VT-d（環境の reader）、判断（壁を全部記録し、最初の壁で止める: 記述が無い → pipe が読めない → GGTT の重なり → VT-d が読めない → 変換・保護。lit は止めずに take over）、出力の分類（platform の記述の port: panel の DDI の DP SST → panel、combo の HDMI/DVI → HDMI、Dekel の DP SST → Type-C DP、他 → other）、log 2 行（`i915: xd-takeover:`） |
| `display/xd-power.c/.h` | `drv_i915_xd_power_well_powered`（state bit だけ。request の無い well（DC off・mailbox）は on） |
| `display/xd-display.c`・`xd.h` | `drv_i915_xd_check_firmware`（環境: CPUID の hypervisor、boot handoff の framebuffer、VT-d の read-only map）、panel の DDI は VBT の eDP の child（無ければ A）。`struct i915_xd` に `firmware` |
| `display/display.c`（殻） | `i915_display_native_check` が旧 N0 の後に xd の check を呼び、`i915_display_firmware_compare` で 1 行: `i915: xd-takeover: agrees with N0` / `differs from N0: <field>`（decision・takeover・lit pipes・GGTT overlap・lit pipe の DDI function・出力の種類） |
| `display/output.c/.h`（own） | `drv_i915_gop_output_read` は xd の record（`struct i915_xd_firmware`）を読む（規則は同じ: port A の DP SST = eDP、DDI B の HDMI/DVI、TC1〜4 の DP SST。platform の記述への置き換えは p051）。R11: 開発の管理情報 15 → 0 |
| `tests/display/host-xd-takeover-test.c`（新） | 39 check（旧 `host-native-decide-test.c` の 14 の状況 + 記述無し、readout（書き込み 0）、分類、GGTT の範囲） |

段の phase.md の必須の項目:
- 消す file: 無し（影の段。旧 N0 の判断は p037 で消す）。
- 殻が更新する旧の写し: `display->gop`（xd の record から own の規則で）。旧の `display->n0` は旧のまま（p037 で消す）。
- 表 R: 書き込みは無い（読むだけ）。
- R11: `output.c` 15 → 0、`display.c`・`xd-display.c`・`xd-power.c`・`xd.h` 0。

## 旧と同じに保った物と違い

- 確かめ方: `build/tmp/refcmp.sh`（前の段の commit の tree を export して旧の host 試験を流し、norm の出力を比べる）で `host-dp-test`（72）・`host-lcd-modeset-test`（120）の出力が同じ。旧の N0 の試験（`plan/ws084/tests/run-native-decide-host-test.sh`）は p047 から link が通らない（`drv_i915_mmio_record_mark` が無い: その runner は mmio-record.c を足していない）。14 の状況は host-xd-takeover-test に移した。
- 判断は旧と同じ順・同じ条件。違い: pipe の電源は xd の well の表（pipe と transcoder を灯す well の state）で読む（旧は旧の power domain の表）。xd の記述が無い device は止める（旧は止めない。§2.14 の「記述が無い → GOP の絵を残す」。p036 では旧の判断がまだ効く）。
- 分類の違い（5330 の配線では同じ）: combo の DDI A の HDMI は xd では HDMI（旧は DDI B だけ）、panel の DDI は VBT の eDP の child（旧は A 固定）。own の output.c の規則は p051 まで旧のまま。
- VT-d の register の出典: GFXVTBAR（MCHBAR の 0x5400）は Intel の 12th Gen の datasheet vol. 2、VER・GSTS・PMEN は VT-d の仕様（`Values referenced:`）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `i915: xd-takeover: agrees with N0` が出る（differs ならその field）。`i915: xd-takeover: lit pipes 0x1 …, output panel (pipe A, DDI 0, mode 2)` と `use, take over 1`。
- `i915: N0: the firmware's output: DP SST on DDI A, pipe A` が今と同じ。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p036/xd-host` → 全部 pass（takeover 39）、`I915_XD_STRICT` の検査は通る。
- 旧の host 試験: `sh build/tmp/refcmp.sh bde6f680e p036` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p036` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `xd-takeover: lit pipes 0x1, … output panel (pipe A, DDI 0, mode 2)` と `use, take over 1`、`firmware: the firmware's output: DP SST on DDI A, pipe A`

満たしていない・未確認の基準と残る物:

- `agrees with N0` の行は無い（p042・p050a で N0 の影の比べと `N0:` の文言を消した後なので、基準の文が古い）

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
