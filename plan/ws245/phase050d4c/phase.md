<!-- awesome-plan project=zedbsd record=ws245-p050d4c -->

# ws245-p050d4c: 始まりと終わり（eDP の device・nogem・setup_outputs・VGA・world）

Status: planned（2026-10-17 Q1 の判断で p050d4 を 4 段に分けた 3 段目。p050d4b の後）
Parent: [WS245](../ws.md)
Queue: q1229-i11（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6。分け方は [phase050d4](../phase050d4/phase.md)

## 範囲

- eDP の device（dp-sink.c の `edp_device_prepare/_init_connector/_fini`、`i915_edp_device_lcd_a` の compute）を xd-edp に。
- `drv_i915_display_nogem_front/_fini`・`drv_i915_setup_outputs`・VGA の client の登録・takeover の world を xd-display/xd-takeover に。display.c の `drv_i915_lcd_world_create/_destroy`・`_set_display_ver/_set_xd_platform` を消す。
- dp-ext-kern.c が呼ぶ dp-sink.c の DPCD・binding は殻 file（p052 で消える）に移す。
- edp-ktest・edp-sync-ktest・aux（ktest）・host-dp-test の oracle を xd-edp・xd-aux の試験へ。

## 受け入れ

- 消した関数の本番の呼び手が 0、build warning 0、`I915_XD_STRICT`、R11。5330 は全部の段の後に 1 回。
