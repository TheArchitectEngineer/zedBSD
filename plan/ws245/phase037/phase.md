<!-- awesome-plan project=zedbsd record=ws245-p037 -->

# ws245-p037: 引き継ぎ（N1）の判断と GOP の出力の止め方（xd-takeover）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。design は「ユーザーが居る時間に流す」段）
Parent: [WS245](../ws.md)
Queue: q1229-i05（P1）
設計: [design.md](../design.md) §6.2 の p037 の行、§2.14、§2.3

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-takeover.c/.h` | `drv_i915_xd_takeover_stop`: lit の 1 pipe を disable の順で止める（plane（on の時だけ、次の vblank を待つ）→ panel の backlight（hook）→ HDMI の sink（hook）→ transcoder の停止 → DDI function の disable → scaler 2 つ → data island（DP は PPS も）→ DP の sink の D3（hook）→ DDI buffer と transport（FEC は DP_TP_CTL を読む）→ TRANS_CLK_SEL を none → panel の電源（hook）→ port の参照（hook）→ DDI clock の gate（combo は clock map の off bit、TC は off bit と DDI_CLK_SEL を none）→ 他の lit の pipe が使っていなければ PLL の disable → link の参照（hook）→ pipe DMC off）。PLL は combo なら clock map の select、TC なら DDI_CLK_SEL（TC PLL か TBT PLL）。`drv_i915_xd_vga_off`: VGA_CONTROL が on なら sequencer の screen off（hook）→ 300 µs → disable と posting read。record に `panel_ddi` |
| `display/takeover.c`（殻） | N0 の判断・読み・log（`drv_i915_native_precheck/decide/log/log_again`・`drv_i915_n0_pipe_powered`・`i915_read_*`・`i915_cpu_hypervisor`・名前と sha の helper、N0 の register の macro）を消した。`i915_crtc_disable_noatomic_begin`（N1 の takeover と sanitize が使う）は registry の plane の記録の後、hardware を `i915_takeover_xd_stop`（xd の stop、hook は registry の encoder の旧の状態を保つ: backlight off、HDMI scrambling、DP の D3 と link_trained、PPS の VDD と off、DDI IO の wakeref、dual mode、post_pll_disable の hook）に。旧の crtc_disable hook（pipe.c の hsw_crtc_disable と ddi.c の encoder の段）は takeover から呼ばれなくなった（commit_disable が p048 まで使う）。`drv_i915_vga_disable` は xd の `drv_i915_xd_vga_off`（SR01 は旧の legacy I/O の殻）。log の `i915: N1 …` は `i915: takeover: …` |
| `display/display.c` | `i915_display_native_check` は xd の check だけ（判断が use でなければ display を使わない、OTHER も使わない。`i915: N0:` の 4 行は残す）。影の比較を消した。`display->n0.takeover` → `display->xd.firmware.take_over` |
| `display/modeset.c` | 同じ置き換え、`i915: N1 read/write/lock` → `i915: takeover: …` |
| `display/internal.h`・`takeover.h` | N0 の型（`i915_native_report`・`_deps`・`_pipe`・`I915_N0_*`）、`display->n0`・`n0_mmio`・`n0_last`・`read_opregion_copy/vcopy`、N0 の宣言を消した |
| `tests/display/host-xd-takeover-test.c` | 56 check（p036 の 39 + stop の panel（順と hook の位置、PLL off、record が dark）、HDMI（sink は前、panel の hook 無し、PPS の island は残す）、2 pipe の共有 PLL、VGA の 3 つ） |
| `tests/display/host-native-decide-test.c`・`README.md` | 中身を空に（削除は Q1）。README の行を外し xd の runner を案内 |

段の phase.md の必須の項目:
- 消す file（Q1）: `src/drivers/gpu/i915/tests/display/host-native-decide-test.c`（空）、`plan/ws084/tests/run-native-decide-host-test.sh`（空の file を compile する runner。master・未完の WS から参照されていない。WS084 の dir なので担当は触っていない）。
- 殻が更新する旧の写し: registry の crtc（active・enabled）と plane の visible、shared DPLL の参照、complete の cdclk/dbuf/bw/power の記録（旧のまま）、encoder の panel・PPS・DP の状態（hook）。
- 表 R: GOP の出力の止め方の書き手は xd-takeover だけ（旧の crtc_disable の段は takeover では呼ばれない）。
- R11: `takeover.c`・`modeset.c`・`display.c` の開発の管理情報 0（`2026-09-29`・`(N1)` を消した）。

## 旧と同じに保った物と違い

- 確かめ方: `sh build/tmp/refcmp.sh a5add86c9 p037` で旧の host 試験の出力が同じ（どちらも takeover を通らない）。takeover の列は host-xd-takeover-test で見る。
- 列は Linux の hsw_crtc_disable（encoder の disable → post_disable → shared DPLL → post_pll_disable → pipe DMC）と同じ順。違い: (1) plane が既に off なら書かない（registry の plane の disable が先に xd plane で off にする）、(2) DP の sink の FEC ready の解除は書かない（GOP は FEC を使わない前提）、(3) PSR・DSC・HDCP・audio・VRR の MSA は無い（GOP は使わない）、(4) PLL は他の lit の pipe の使用を hardware で見る（旧は shared DPLL の active mask）、(5) eDP の transcoder・MST・bigjoiner は扱わない（display 12・13 の GOP は使わない）。
- registry の encoder が無い crtc（GOP が HDMI を灯し registry に HDMI の encoder が無い時）も hardware は止める（旧は encoder の段が空で port が残った）。hook は skip。
- 残り（p050）: readout・sanitize・`setup_outputs`・n1 の helper・VGA の client。§2.14 の「init の well が上がらない時に GOP の絵を残す」は今の core の init の失敗の扱いのまま（p041・p042 で xd-display の段に）。
- `takeover.c` の削除は約 600 行（design の見積もり 2500 は crtc_disable の Linux の段を含むが、sanitize が使うので p050 まで残る）。

## 実機で確かめる事（5330、全部の段の後の 1 回で。ユーザーが居る時間に）

- `i915: takeover: pipe A stopped: DDI 0 mode 2, PLL 0, late 0x0, encoder kept (rc 0)` と `i915: takeover: rc=0, crtcs stopped 1, still active 0x0`。
- GOP の画面 → desktop の切り替えの黒の時間が延びない（目視はユーザーの一覧）。digest の差は takeover の部分（scaler・DIP の書き込みの順が旧と違い得る）。
- 節目（p048）: HDMI を挿したまま蓋を閉じての起動（GOP が外部を灯す道）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p037/xd-host` → 全部 pass（takeover 56）、`I915_XD_STRICT` の検査は通る。
- 旧の host 試験: `sh build/tmp/refcmp.sh a5add86c9 p037` → dp・lcd とも SAME。
- build（2026-10-16）: `sh build/tmp/kbuild.sh p037` で 6 組が rc 0・warning 0。`git diff --check` は空。
- 5330: 未実施（全部の段の後に 1 回）。
