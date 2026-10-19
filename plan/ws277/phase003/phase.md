<!-- awesome-plan project=zedbsd record=ws277-p003 -->

# ws277-p003 zero copy の flip（DC8200）

- 親: [ws277](../ws.md)
- Status: in-progress（2026-10-20 04:00 P1 q1351-i01: 本体を実装・build・host 試験 PASS。実機 U1〜U5・U11 待ち。p002 の実機 U0 も未）
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

### 残り（q1350 の時点）

- p003 の本体は下の q1351 で実装した。
- p005: VOUT の SYS 側の gate は共有（止めない）にした。自前の mode で止める必要が出たら表の印を見直す。

## 本体（q1351 / q1351-i01、P1、2026-10-20）

### 実装

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/dc8200.c`・`powervr.h` | vblank の IRQ: `drv_powervr_dc_vblank_start()`（display の attach から。保留を 1 回読んで捨て、`kern_irq_register`、`AQ_INTR_ENBL` に HDMI の panel の bit を足す。以後常に有効）。handler は `AQ_INTR_ACKNOWLEDGE` を読み（handler だけが読む）、HDMI の panel の bit の時だけ sequence を進め、tick・`CURRENT_LOCATION` の生の値（64 個の環と最小・最大、U11）を記録して待ち手を起こす。他の panel の割り込みは数えるだけ。保持の数には触らない。commit: `drv_powervr_dc_commit(frame or NULL)` は `dc_commit_lock` を `spin_lock_irqsave` で取り、区間の中で fatal の印を見て立っていれば書かずに ENXIO、手順 (1) PANEL_CONFIG_EX bit 0 を立て CONFIG_EX の COMMIT を clear (2) plane の register (3) COMMIT を set (4) bit 0 を戻す、の後に同じ区間で s0 を読む。frame は ADDRESS と CONFIG の format（X8R8G8B8 = 5）・swizzle（BGRA の byte は ARGB 0、RGBA は ABGR 2）だけを変え、STRIDE・SIZE・角・BLEND は snapshot のまま。fatal の戻しも同じ `write_plane()` を使う。`drv_powervr_dc_wait(target)`: target に達するまで眠り、vblank が 100 ms（`KERN_MS_TO_TICKS(100)`）来なければ ETIMEDOUT。`drv_powervr_dc_scanout_ready()`（go・IRQ 開始・最初の vblank を観測・fatal でない）、`drv_powervr_dc_frame_fits()`（snapshot の SIZE・STRIDE と一致）、`drv_powervr_dc_vblank_report()` |
| `src/drivers/gpu/powervr/dc8200-flip.c`・`.h`（新） | 値だけの規則（host で試せる）: 保持の数 `holds`（front・pending・unsettled、最大 3）と `destroyed`、`powervr_flip_due(s0, seq)`（seq ≥ s0 + 2）、`taken`（pending を front にし、その後で前の front と unsettled の `holds--`、0 かつ destroyed の物を返す）、`lose`（pending を unsettled に残す。書き戻し中の二度目の喪失は front の余分の保持だけを外す）、`destroy`。全部の出来事を 64 個の flip の記録の環（出来事・buffer・holds・sequence・s0）に記録し、順序に反する出来事（latch 前の taken、二重の commit、0 未満の drop）を `mismatches` に数える |
| `src/drivers/gpu/powervr/display.c` | resource に `hold`・`direct`・`frame`。import で zero copy の可否を判定（offset 0、stride が 32 の倍数かつ ≤ 65535、backing の page が連続、末尾 ≤ 4 GiB、範囲が backing の中）。外れても EINVAL にせず copy で出す。present: mutex の中で「flip 中」が終わるのを `mutex_wait` で待ち、lease を確かめ、出力喪失なら書き戻しの s0' + 2 を確かめるまで ENXIO。zero copy（`dc8200.zerocopy=test`・scanout_ready・import の検査・frame_fits）なら commit → mutex を外して s0 + 2 を待つ → taken → 返る。それ以外は bootfb へ copy してから、front が buffer なら plane を bootfb へ戻して latch を待つ（Minor 6）。待ちが 100 ms を超えたら lose → front を書き戻して ENXIO。`resource_destroy` は保持中なら destroyed を立てるだけ。解放（`powervr_buffer_release`・`kern_free`）は mutex の外の thread の文脈で行い、`owner` を見ない。release・close: 「flip 中」の完了 → 書き戻しの待ち → plane を bootfb へ戻し latch を待つ → 解放 → flip と vblank の要約を 1 行ずつ → console の resume。戻らなければ保持を残す（leak を選ぶ） |
| `include/kern/boot.h`・`src/kern/boot.c` | boot parameter `dc8200.zerocopy=`（`off`・`test`、無い時は off）。display は claim の時に読む（attach の時は parameter が未だ無い場合がある） |
| `platform/riscv64/vmunix.mk` | `dc8200-flip.c` を追加 |

- 設計との差（記録）: (1) BLEND_CONFIG は「blend 無効」の値（mainline `VSDC_FB_BLEND_CONFIG_BLEND_DISABLE`）が手元に無いので snapshot（U-Boot の値）を保つ。X 形式なので alpha は使われないと推測（U2 で確かめる）。(2) flip の記録の環の照合は kernel の中で `mismatches` として数え、release の時に `powervr: display flips N, mismatches M, lost L` と `dc8200: vblank S other O loc lo-hi newest n` を 1 行ずつ出す。試験の program は dmesg（kernel の記録）からこの 2 行を読む。環そのものは gdb・monitor で読める。(3) refresh の実測（query）は p004 の「refresh の実測」に残し、query は 60 Hz のまま。
- HAL の API・toolchain は変えていない。R3 は未着手なので zero copy は `dc8200.zerocopy=test` の時だけ（§3.2 の 3）。

### 確かめ（2026-10-20）

- build（warning 0）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws277-p003-vf2 vmunix` → 0。`config-riscv64-virt.mk BUILD=build/ws277-p003-rv` → 0。`config-amd64.mk BUILD=build/ws277-p003-amd64` → 0。3 つとも `warning:` は 0 件。
- host: `plan/ws277/tests/p003/run.sh build/ws277-p003-host4` → `PASS fatal-host-test (36 passes, 0 failures)`（IRQ は HDMI の bit だけ数える、最初の vblank まで ready でない、frame_fits、commit の 12 の書き込みの順・address・format/swizzle・hold、4 GiB 超の拒否、s0 は書いた後に読む、wait は s0 + 2 で返る・止まった panel で ETIMEDOUT、fatal の後の commit は書かずに ENXIO と既存の fatal の項目）、`flip-host-test 24 passes 0 failures`（s0 + 2 の規則、保持の数、二度目の present、destroy の遅延と最後の drop での解放、喪失・書き戻し・二度目の喪失・回復、mismatches）、`boot-zerocopy-host-test: 7 checks, 0 failures`。`plan/ws277/tests/p002/run.sh` → 16 passes 0 failures。`plan/ws229/tests/run-host-boot-gen9.sh` → 10 checks 0 failures（boot.c の既存の parameter）。
- 未実施: display.c の present・release の道（mutex を外した待ち、lose の後の書き戻し、解放の文脈）の host 試験（display.c は gpu core・render・console に依存し stub が大きいので、規則は dc8200-flip.c の試験で代え、道は読んで確かめた）。QEMU は起動していない（QEMU に DC8200 は無い）。実機は全部未実施。

### 実機の試験（今は頼まない。VF2 の板の接続と判定の方法は WS256 #10・#15 で未決）

共通: VF2、配布の U-Boot で HDMI に GOP の画が出ている事、boot の行に `dc8200.zerocopy=test`。判定は画面の撮影（目視）と kernel の記録（dmesg）の要約の行。serial・console の log では判定しない。

| ID | 手順 | 合格 |
| --- | --- | --- |
| U0 | （p002）起動して `dc8200: go ...` の行を見る | go。no-go なら p003 の道は動かない（copy だけ） |
| U1 | 試験の program が mappable な blob（作った側も SLC 迂回）に色の帯を書いて present。その後 present せずに CPU が blob を書き換える | 書き換えが present 無しで画面に出る（zero copy の証拠）。claim の行が `zero copy 1` |
| U2 | 赤・緑・青の帯を BGRA と RGBA で present | 色が合う（format・swizzle、BLEND を保った事の確かめ） |
| U3 | 2 つの buffer を交互に数百回 present し、返った直後に前の buffer へ別の模様を書く | release の行 `powervr: display flips N, mismatches 0, lost 0`。目視で崩れが無い（補助、1 frame の崩れは見落とし得る） |
| U4 | release で console に戻る。小さい frame（1280×720）・stride の違う frame を present。同じ image を何度も present し、途中で destroy してから別の image を present | console に戻る。小さい frame は copy で出て、古い bootfb の画が見えない。mismatches 0。destroy の後も表示中の buffer の画が崩れない |
| U5 | zero copy 中に panic を起こす（試験用の手段） | panic の文字が画面に出る。その後 present が commit しない |
| U11 | U3 の後の `dc8200: vblank S other O loc lo-hi newest n` を読み、`CURRENT_LOCATION` の bit の並び（p002 の U0 の記録）で IRQ の行の位置を見る | IRQ の点が latch の点（帰線の中）と同じか後で、遅れが帰線に収まる。外れたら s0 + 3 にして記録（p004 の入力） |

### 残り

- 実機 U0（p002）と U1〜U5・U11。結果が出るまで cleared にしない。
- p004: UNGATED・adoption_wait、60 fps に戻す、refresh の実測、underflow。
- R3 が main に入ったら `dc8200.zerocopy=test` の印を外す（p007 の前）。
