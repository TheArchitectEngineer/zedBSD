<!-- awesome-plan project=zedbsd record=ws245-p050d4b -->

# ws245-p050d4b: 走らせ方の口（resident run・show run・kernel の binding）

Status: planned（2026-10-17 Q1 の判断で p050d4 を 4 段に分けた 2 段目。p050d4a の後）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6、§4.2。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- resident run・show run（ktest の道）・kernel の binding（bind_ops・fill_cfg・preflight・locks_init・output params/cfg/link fallback）と diagnostics の retained の 3 つを xd-display へ。fill_cfg が `struct i915_xd_picture_config` を直に作り、p050d4a の変換 `drv_i915_lcd_picture_config` を消す。
- output.c の `drv_i915_lcd_compute_hdmi/_dp_ext` を xd-screen の plan_link に（state.c の compute と `i915_state_from_plan` を消す）。
- ktest（lcd-run・hdmi-output・lcd-flip・lcd-gpu・lcdg・lcd-show）は p050d4a で組を外した物の file の削除を Q1 に（R11 と §6.4 の基準）。

## 受け入れ

- 消した関数の本番の呼び手が 0、build（既定と I915_TESTS=y の vke2・execution）warning 0、`I915_XD_STRICT`、R11。比べは付け替えた旧 host 試験（host-lcd-modeset-test）と xd の試験。5330 は全部の段の後に 1 回。
