<!-- awesome-plan project=zedbsd record=ws277-p003 -->

# ws277-p003 zero copy の flip（DC8200）

- 親: [ws277](../ws.md)
- Status: planned（本体は未着手。下の「先行: R1・R2・R4」だけを q1350 / q1350-i01 で先に実装）
- 設計: [design.md](../design.md) 版 3.1 §7 の p003、§2.6、§4.6、§9

## 先行: R1・R2・R4（q1350 / q1350-i01、P1、2026-10-20）

WS256 の source に入る依頼（design §9 の R1・R2・R4 の text の部分）を、Q1 の許可で WS277 の p003 の前に実装した。WS256 の記録は Q1 が付ける。

### 実装

| 依頼 | file | 中身 |
| --- | --- | --- |
| R1 | `src/drivers/platform/visionfive2/vf2-crg.h`・`.c` | 生成器 `DRV_VF2_CRG_VOUT`（3、`starfive,jh7110-voutcrg`、reset は assert 0x48・status 0x4c・12 本）。VOUT の表: APB の divider（≤8、共有）、DC8200_PIX の divider（≤63、SETTABLE）、DC8200_AXI・CORE・AHB の gate、PIX0・PIX1 の gate+mux（DC8200_PIX / 入力 `hdmitx0_pixelclk`）、DOM_VOUT_TOP_LCD の gate+mux（PIX0 / PIX1）、HDMI_TX_MCLK・BCLK・SYS の gate。SYS の表に AHB1（10）と VOUT の上流（58 VOUT_SRC・59 VOUT_AXI（≤7）・60 NOC_BUS_DISP_AXI・61 VOUT_TOP_AHB・62 VOUT_TOP_AXI は共有＝止めない、63 HDMITX0_MCLK）。読むだけの `drv_vf2_clock_running()` を追加。DSI・MIPI の clock は入れていない |
| R2 | `src/drivers/platform/visionfive2/vf2-pmu.h`・`.c` | `DRV_VF2_PD_VOUT`（4）を受ける。手順は PD_GPU と同じ（software の mode と encourage）。VOUT には hardware event が無い（TRM Table 3-12）ので GPU の event の mask は PD_GPU の時だけ |
| R4 text | `include/drivers/generic/serial-console.h`、`src/drivers/generic/serial-console.c` | 追加だけ: `drv_serial_console_ops()`（登録せずに表を返す）。既存の関数・表・riscv-virt の動作は不変 |
| R4 text | `src/kern/platform/visionfive2.c` | serial の表を複写した wrapper の `vf2_text_ops` に `reveal_fatal` を足して登録。登録口は static な `vf2_fatal_display`（probe が 0 を返した時に `drv_powervr_dc_restore_fatal` を入れる）。p002 の probe の前に display の clock の gate を bus の順（SYS 60・61・62 → VOUT 4・5・6・7）で読み、閉じていれば `dc8200: no-go (clock <名前> stopped)`。DT に voutcrg が無い時は VOUT の gate を飛ばす（前の動作）。`VF2_PD_VOUT` の局所の定義は `DRV_VF2_PD_VOUT` に置換 |
| R4 text | `src/drivers/gpu/powervr/dc8200.c`・`powervr.h` | `drv_powervr_dc_restore_fatal()`: 最初に fatal の印（release の store）を立て、probe が go の時だけ、`dc_commit_lock` を最大 4096 回 trylock（この CPU が持っていれば試さない。spin_trylock は再帰で trap するため）、取れなくても §2.6 の順で書く（PANEL_CONFIG_EX bit 0 を立てる → CONFIG_EX の COMMIT を clear → ADDRESS・STRIDE・CONFIG・SIZE・TOP_LEFT・BOTTOM_RIGHT・BG_COLOR・BLEND_CONFIG に snapshot → COMMIT を set → bit 0 を clear）。latch は待たない。`drv_powervr_dc_fatal()` は p003 の commit が lock の中で見る印。p003 の commit はまだ無く、`dc_commit_lock` は今は fatal の戻しだけが取る |

- 「p002 の probe（dc8200.c）で voutcrg の gate を見る」は、crg を引けるのが platform なので `visionfive2.c` の `probe_display_controller()`（p002 の probe の入口、PD の確認の直後）に置いた。dc8200.c は汎用の driver のまま。
- 出典（Values referenced）: Linux v6.12 `starfive,jh7110-crg.h`（VOUTCLK 0〜17、SYSCLK 58〜63）・`starfive,jh7110-pmu.h`、StarFive Linux 6.12.y 4cecf169f38e の `clk-starfive-jh7110-vout.c`・`clk-starfive-jh7110-sys.c`・`reset-starfive-jh7110.c`（jh7110_vout_info）・`vs_dc_hw.c`（dc_hw_enable_shadow_register）、TRM Table 3-12・3-13・5-12・5-24。TRM Table 5-23 は VOUT の reset assert を 0x38 と書くが、0x38 は clock 14 の register なので 0x48 は StarFive Linux から取った（code の注記に記録）。
- HAL の API・toolchain は変えていない。

### 確かめ（2026-10-20）

- build（warning 0）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws277-p003-vf2 vmunix` → 0、warning 0。`config-riscv64-virt.mk BUILD=build/ws277-p003-rv`・`config-amd64.mk BUILD=build/ws277-p003-amd64` → 0、warning 0。
- host: `plan/ws256/tests/vf2-soc/run.sh build/ws277-p003-host` → `PASS: 0 failure(s)`（VOUT の 19 項目・PMU の 6 項目を追加。既存の「表の外の clock」の項目は 58 が表に入ったので 57 に変更）。`plan/ws256/tests/serial/run.sh build/ws277-p003-serial` → all PASS（`drv_serial_console_ops()` の 2 項目を追加）。`plan/ws277/tests/p002/run.sh` → 16 passes 0 failures。新しい `plan/ws277/tests/p003/run.sh build/ws277-p003-fatal` → 10 passes 0 failures（probe は書かない、fatal の印、12 の書き込みの順と値、空いた lock は 1 回、塞がった lock は 4096 回試して書く）。
- 未実施: 「この CPU が lock を持つ時は trylock しない」の host 試験（lock の owner が dc8200.c の static で偽れない）。実機（U5 は p003 の本体の後）。QEMU は起動していない。
- T1 への依頼: 不要と判断。serial-console は追加だけで riscv-virt の経路（`drv_serial_console_init()`）は不変。VF2 の起動の確かめは実機（WS256 #10・#15 の方法が決まった後）。

### 残り

- p003 の本体（commit の手順・vblank の IRQ・zero copy）は未着手。commit の側は `dc_commit_lock` を IRQ 禁止で取り、区間の中で `drv_powervr_dc_fatal()` を見て ENXIO（§2.6）。
- p005: VOUT の SYS 側の gate は共有（止めない）にした。自前の mode で止める必要が出たら表の印を見直す。
