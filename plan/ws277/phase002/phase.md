<!-- awesome-plan project=zedbsd record=ws277-p002 -->

# ws277-p002 DC8200 の go/no-go（attach・引き継ぎの判定・記録）

- 親: [ws277](../ws.md)、Queue: q1348 / q1348-i01（P1、2026-10-20）
- Status: in-progress（実装・build・host 試験まで。実機 U0 待ち）
- 設計: [design.md](../design.md) 版 3.1 §7 の p002、§2.4・§4.1、§2.6（読み戻し）、§11。ユーザーの判断 D1〜D11 は全部 (a)（[ws.md](../ws.md) の「決定」）。

## 範囲

- DC8200 の窓の写像（identity 0x29400000/0x100、display 0x29400800/0x2000）、model・revision・CID の読み、両方の primary plane の register（ADDRESS・STRIDE・CONFIG・CONFIG_EX・SIZE・TOP_LEFT・BOTTOM_RIGHT・BG_COLOR・BLEND_CONFIG）の snapshot、HDMI の panel（panel 0 と推測）の PANEL_CONFIG・PANEL_CONFIG_EX。
- §2.4 の判定: model 0x8200、FB_EN が立ち DISPLAY_ID が panel 0 の最初の plane、ADDRESS == bootfb の物理（4 GiB 未満）、STRIDE == pitch、SIZE == W×H、tile 0・rotation 0・scale 無し・DEC 無し。
- 読み戻しの記録: panel 0 の `CURRENT_LOCATION` を約 40 ms（kernel の counter、無ければ 20 万回の読み）読み続け、最初の値・最小・最大・値が下がった回数（wraps）・読みの回数、その間 plane の COMMIT（CONFIG_EX bit 12）が一度でも立っていたか。
- **書き込みは無い**（plane・panel・IRQ に触らない）。zero copy・IRQ・present は p003。
- R4 の記述の部分: `src/kern/platform/visionfive2.c` に DC8200 の組み込みの記述（`vf2_dc_builtin`）。DT に `starfive,jh7110-dc8200` の有効な node があれば reg 0・1 と interrupts を優先。PD_VOUT（4）が off・不明なら DC を読まない（読むと bus が止まる）。HDMI（0x29590000・IRQ 99）は p002 で使わないので記述しない（p005・p006 で足す）。

## 実装（2026-10-20 P1）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/dc8200-adopt.h`・`.c` | 判定だけの純粋な関数 `powervr_dc_judge()` と理由の名前。値だけを受け、host でも同じ |
| `src/drivers/gpu/powervr/dc8200.c` | `drv_powervr_dc_probe()`: bootfb を読み、窓を写像し、snapshot・判定・読み戻しを static な `board_adoption` に残し（p003 の戻し先）、boot log に 1 行 |
| `src/drivers/gpu/powervr/powervr.h` | `struct drv_powervr_dc_place` と `drv_powervr_dc_probe()` |
| `src/kern/platform/visionfive2.c` | 組み込みの記述、DT の優先、PD_VOUT の確認、GPU の attach の後に呼ぶ |
| `platform/riscv64/vmunix.mk` | VF2 の PowerVR の source に 2 file |
| `plan/ws277/tests/p002/` | host 試験（`run.sh OUTDIR`） |

出典は block ごとに `Values referenced:`（vendor 6.12 `vs_dc_hw.h`・`vs_dc_hw.c`・`vs_dc.c`・`jh7110.dtsi`、mainline `vs_dc_top_regs.h`・`vs_primary_plane_regs.h`・`vs_hwdb.c`、`starfive,jh7110-pmu.h`）。vendor の code・名前は写していない。HAL・toolchain は変えていない。

### 画面の 1 行（kern_logf、quiet でも platform の debug port = hal_putc で bootfb に出る）

```
dc8200: go (takeover possible) model 8200 rev 5721 cid 310 plane 0 addr fe000000 stride 7680 size 21c0780 cfg … ex … panel …/… loc <first> <min>-<max> wraps <n> reads <n> commit <0|1> fb <phys>/<pitch>
```

no-go の時は `dc8200: no-go (<理由>) …`。理由: unknown model / no enabled plane on the panel / plane address is not the framebuffer / plane stride is not the framebuffer pitch / plane size is not the framebuffer size / plane is tiled / rotated / scaled / compressed。DC を読む前に止まった時は `dc8200: no-go (no boot framebuffer, N)`・`(power domain off)`・`(power domain unknown, N)`・`(unreadable device tree node, N)`・`(control map failed, N)`。

## 確認（2026-10-20 P1）

- build（warning 0）: `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws277-p002-vf2 vmunix` → 0、warning 0。`config-riscv64-virt.mk BUILD=build/ws277-p002-rv`・`config-amd64.mk BUILD=build/ws277-p002-amd64` → 0、warning 0（この 2 つは新しい file を build しない。共通の header の変更の確かめ）。
- host 試験: `sh plan/ws277/tests/p002/run.sh <OUT>` → `PASS adopt-host-test (16 passes, 0 failures)`（go・plane 1 の選択・別 panel・model・address・4 GiB 超・stride・幅・高さ・tile・rotation・scale・DEC・COMMIT と format の bit は無視）。
- QEMU: 未実施（QEMU に DC8200 は無い。依頼に従い起動していない）。
- 実機: 未実施（下の U0）。

## 実機の試験（U0、T1 またはユーザーの試験の一覧へ）

- image: VF2 の通常の build（`config/ci/config-visionfive2.mk`、`CONFIG_DRIVER_POWERVR := y`）。配布の U-Boot で HDMI に monitor を繋いで起動する。
- 見る物: 起動の画面（HDMI）の `dc8200:` で始まる 1 行。画面を撮る（serial・console の log では判定しない）。
- 合格（go）: HDMI に GOP の画が出ていて、行が `dc8200: go (takeover possible)`。
- no-go: 行の理由を記録する（D1 の no-go の順、design §7 へ）。
- 記録する値（p003・p004 の入力）: model・rev・cid、plane の番号（HDMI が panel 0 で plane 0 か）、cfg（format・swizzle）、ex、panel の 2 語（PANEL_CONFIG_EX bit 0 の今の値）、`loc` の first・min-max・wraps（40 ms で wraps が 2 前後なら行の counter。上位 16 bit・下位 16 bit のどちらが行かは min-max から読む）、`commit`（1 なら COMMIT は自己 clear しない、0 なら自己 clear するか元から立っていない）、fb の物理・pitch と addr・stride の一致。
- 注意: 判定は画面の行だけで行う。行が出ない・起動が止まる時は DC の窓の読みで bus が止まった疑い（PD_VOUT は on でも voutcrg の clock が切られている場合）。その時は画面で `dc8200` の行の直前で止まったかを見て記録する。

## 残り・未確認

- HDMI が panel 0 である事（推測、U0 で確かめる）。
- voutcrg の DC8200 の clock の gate の状態は確かめずに読む（R1 の provider が無いので）。PD_VOUT の on だけで守っている。
- HDMI の組み込みの記述（0x29590000・IRQ 99）は p005・p006 で足す。
- `board_adoption` の snapshot を display.c が使うのは p003。
