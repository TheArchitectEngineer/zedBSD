<!-- awesome-plan project=zedbsd record=ws244-p002 -->

# ws244-p002 intel/ の Gen9 の display の header と bits.h

Status: cleared（2026-10-18 Q1 判定: 先頭の表示・SHA-256 の一覧を外し block ごとの Values referenced、名前は既に I915_SKL_*、#define の行は不変（差分は comment だけと Q1 が確かめた）、b7cb226be merge f6b4952b0、build 2 組 warning 0 は P1 の報告）（旧: in-progress（2026-10-18 q1251-i01、P1。cleared は Q1））

- 親: [WS244](../ws.md)。範囲: `src/drivers/gpu/i915/intel/display-regs-gen9.h`・`display-regs-gen9-modeset.h`・`bits.h`。
- 受け入れ: 先頭の外部の著作権表示・許諾・SHA-256 の出典の一覧を外す。値は block ごとに `Values referenced:`（版・path・macro・関数の名前）。名前は zedBSD の名前。動作は変えない。

## 実行の記録（2026-10-18）

- 名前は既に `I915_SKL_*`・`I915_CNP_*`・`I915_DPCD_*`・`I915_DDC_*` で、Linux の名前の macro は残っていない（使う 24 file の追従は不要だった）。
- 2 つの header の先頭の Tungsten Graphics・Intel の Copyright と MIT の許諾、「Modified for zedBSD: excerpted … Sources (SHA-256 …)」を外し、zedBSD の表示（Zlib）と file の役割の説明だけにした。
- 各 block の comment に `Values referenced: Linux v6.8.12 <path> (<名前>)` を付けた。名前は Linux 6.8.12 の tree（drivers/gpu/drm/i915・include/drm）で `#define`・関数・表の定義を引いて path を決め（scratchpad の補助 script）、引けなかった block は手で調べた（intel_display_device.c の PIPE_A_OFFSET ほか・dbuf.size、skl_watermark.c の skl_setup_wm_latency・skl_wm_max_lines、intel_dpll_mgr.c の skl_ddi_lcpll_get_freq、intel_fb.c の intel_linear_alignment、drm_edid.h の DDC_ADDR・EDID_LENGTH、drm_dp.h の DP_*）。port の番号・WM の段の数の説明の 2 block は hardware の事実なので参照を付けていない。
- bits.h: 「Linux の helper を置き換える」の説明を外し、自前の helper の説明にした。
- `#define` の行は 1 行も変えていない（`git diff -U0` で comment と空行だけを確かめた）。GT ではないので mmio-record の比較は不要。

## 確認

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws244 build/ws244/vmunix`: rc=0、warning 0、`amd64 vmunix check: PASS`。
- 同 `BUILD=build/ws244-i915t I915_TESTS=y`: rc=0、warning 0、PASS。
- Pi 4 は i915 を含まないので行わない。host 試験: この header を使う host 試験の runner が tree に無く、変更は comment だけなので build で代えた（未実施）。QEMU・実機は動作を変えないので行わない。
- 消してよい展開物（Q1 へ）: build/tmp/ws244-p002-1792321504/。
