<!-- awesome-plan project=zedbsd record=ws237-p003 -->
# ws237-p003: 段 (2) panel の backlight（PCH の PWM）

Status: in-progress — 実装と host の確かめは済み（2026-10-14 P2）。実機は WS の最後に SV7 の素の起動でまとめて（2026-10-14 ユーザー「SV7の確認はせずに進めて、最後に確認してデバッグで調整しましょう。」）
Disposition: normal
Parent: [WS237](../ws.md)
Queue: Q1 の指示（2026-10-14、P2: p003 → p004 → p005 を順に、既定は今の動きのまま、boot の語で有効、1 か所の定数で既定を切り替え）
依存: [p002](../phase002/phase.md)（readout と段の語）

## 範囲

panel の明るさを PCH の PWM で変える。Settings の明るさの既存の道（libkeiland-backend-zedbsd の `backlight-zedbsd.c` → `/dev/backlight/backlight0`、
kernel の backlight class `include/kern/backlight.h`、Gen12 と同じ "i915" provider）に繋ぐ。mode・panel の電源の順・backlight の enable の順は p004。

## 段の語（p002 から変えた、c7d371583）

`i915.gen9.display=copy|flip|backlight|modeset|full`。右の語ほど左の段を全部含む（切り分けは 1 段ずつ左へ戻す）。既定は
`src/drivers/gpu/i915/display/gen9-display.h` の `I915_GEN9_DISPLAY_DEFAULT`（今は `I915_GEN9_DISPLAY_COPY`）の 1 か所。p002 の
`I915_GEN9_DISPLAY_FLIP_DEFAULT` はこれに置き換えた。boot の parser（`src/kern/boot.c`）は 5 つの語を受ける。

## 実装

| file | 内容 |
| --- | --- |
| `display/gen9-display.[ch]`（新、c7d371583） | 段の語の解読（`drv_i915_gen9_display_word()`、host で試す）と現在の段（`drv_i915_gen9_display_level()`）、log の名前 |
| `display/gen9-backlight.[ch]`（新） | PCH の判定（ISA bridge から `drv_i915_detect_pch()`、無ければ platform から: SKL・KBL → SPT、CFL・CML → CNP。Linux の intel_virt_detect_pch と同じ割り当て）、PWM の register の読み（SPT・KBP: CTL1 0xc8250・CTL2 0xc8254 の period 31:16・duty 15:0。CNP・CMP: CTL1 0xc8250・FREQ1 0xc8254・DUTY1 0xc8258）、解読 `drv_i915_skl_pwm_decode()`、明るさ ↔ duty（0 % = period の 4/255、100 % = period、直線、四捨五入、往復で同じ値）、backlight device の登録（eDP の transcoder が灯っていて PWM が enable で period が 0 でない時だけ）。get・set は PWM が止まっていれば EBUSY。firmware の period・enable・polarity は変えない |
| `display/bootfb.c` | bind で `drv_i915_gen9_backlight_start()`、fini で `_stop()` |
| `intel/display-regs-gen9.h` | PWM の register（Linux 6.8.12 `display/intel_backlight_regs.h` 65・81〜93 行、MIT、sha256 fe411d1e…98b7） |
| 試験 | `src/drivers/gpu/i915/tests/display/host-gen9-backlight-test.c`・`plan/ws237/tests/run-gen9-backlight-host-test.sh`、`plan/ws237/tests/host-boot-gen9-display.c` に新しい語 |

出典（事実だけ、logic は Zlib で書いた）: Linux 6.8.12 `display/intel_backlight.c`（MIT）の lpt_get/set_backlight、lpt_setup_backlight、cnp_setup_backlight、
bxt_get/set_backlight、intel_backlight_init_funcs の PCH ごとの割り当て、get_backlight_min_vbt の 0〜255 の scale。

## 確かめ（host と build だけ）

| コマンド | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws237-p002 build/ws237-p002/vmunix` | rc 0、`warning:`・`error:` 0 行、`amd64 vmunix check: PASS` |
| `sh plan/ws237/tests/run-gen9-backlight-host-test.sh` | `gen9_backlight_host_test: 21 checks, 0 failures`（SPT・CNP の解読、拒否、0・50・100 %、往復 101 値、短い period） |
| `sh plan/ws237/tests/run-host-boot-gen9-display.sh` | `host-boot-gen9-display: 15 checks, 0 failures` |
| `sh plan/ws237/tests/run-gen9-flip-host-test.sh` | 48 checks、0 failures |

未実施: 実機（SV7 の素の起動で WS の最後にまとめて。手順は [ws.md](../ws.md) の「最後の確かめ」）。

## 制限

- VBT が backlight を eDP の DPCD（AUX）や utility pin に置く panel は扱わない（log に「the PWM does not drive the panel」）。VBT の最小の明るさ・PWM の周波数は読まない（firmware の period をそのまま使い、最小は 4/255）。
- panel を消す・点ける時の backlight の enable・disable の順は p004（panel の power sequence）で。
