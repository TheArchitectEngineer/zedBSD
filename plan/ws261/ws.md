<!-- awesome-plan project=zedbsd record=ws261 -->

# WS261 include/kern/boot.h から PC-98・Raspberry Pi の固有の構造体を除く

- ID: ws261、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-18 Q1 作成、p001 を P3）
- 起点（2026-10-18 ユーザー）:「HALからカーネルへは、汎用の構造体しか渡せない規約です。固有の情報は、名前文字列で取得するAPIがありますので、それを使ってください。ブートローダからHALへは、固有の構造体を使えますが、それはHAL内に定義を置きます。また、boot.hのPC98やRPiの構造体は、削除します。この削除対応を、WSを作って別なサブエージェントで進めてください。」
- 規約: [Guardrail](../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」。

## 対象（2026-10-18 Q1 の grep）

- `include/kern/boot.h` の `struct kern_pc98_parameter_handoff`、`KERN_PC98_*`・`KERN_HANDOFF_VERSION_PC98`、`struct rpi4_boot_handoff`、`KERN_RPI4_HANDOFF_MAGIC` など（「XXX: To be removed」の印）。
- 使っている所: `src/hal/arm64/`（cmain・bsp・page・smp・space、bsp-rpi4 の boot・fdt・gic・cons）、`src/kern/platform/rpi4.c`、`src/drivers/platform/rpi4/`（bt・audio・ethernet・sdhci）、`src/drivers/platform/pc98/`、PC-98 の HAL（`src/hal/i386/` の pc98 の部分）と boot loader（`bootloader/pc98/`）。
- 範囲外（ユーザーの指示に無い）: 同じ印の `struct kern_sun4u_*`（sparcv9）。扱いはユーザーに聞く。

## 画面の情報（2026-10-18 ユーザー）

- ユーザー:「フィールドごとに文字列の名前があれば、構造体はいらないのでは。amd64ではどうなってますか？」→ Q1 の説明（amd64 の "pcat.framebuffer" も boot loader の struct zbl6_framebuffer を kernel に渡している）→（クリック）「直す、WS261 に含める」。
- 画面の情報は値ごとの名前で渡す（例 `rpi4.framebuffer.phys`・`.size`・`.width`・`.height`・`.pitch`・`.format`）。構造体の型を HAL と kernel で共有しない。
- amd64 の `pcat.framebuffer` も同じ形に直し、読む側（`src/drivers/platform/pcat/graphics/backend.c`、`src/drivers/gpu/amdrdna/readout.c`、`src/drivers/gpu/nvrtx/discover.c`、`src/hal/amd64/page.c` など）を追従させる。

## kernel の置き場所の約束（2026-10-18 ユーザー）

- `bootloader/include/amd64-kernel-image.h`（UEFI の loader と amd64 の HAL と link script の約束）を i915 の試験（`src/drivers/gpu/i915/tests/render/executor.c`・README）が include している。ユーザー（クリック）「i915 の試験を外し、名前は riscv64 の時に揃える」→ この試験の include を WS261 で外す。header の命名と置き場所は WS256 p004 で揃える。

## 完了の条件

1. `include/kern/boot.h` に PC-98・Raspberry Pi の固有の構造体と定数が無い。
2. HAL → kernel は汎用の `kern_boot_handoff` と、固有の情報は `hal_get_arch_handoff(name)` の名前付きの object だけで渡す。新しく足す名前の一覧と中身を記録する（HAL の責務の追加。hal.h の宣言は変えない）。
3. boot loader → HAL の固有の構造体は HAL の中か `bootloader/` の header に定義を置き、`bootloader/` の header を include するのは HAL だけ（2026-10-18 ユーザー「bootloader/のヘッダを見ていいのは、HALだけにします。HALからカーネルへは、名前ごとの値でアクセスします」）。
4. 画面の情報は Pi 4・amd64 とも値ごとの名前で渡り、kernel が boot loader・HAL の構造体の型を include しない。
5. 振る舞いは変えない: build（config-rpi4.mk・config-pc98.mk・config-amd64.mk）warning 0。Pi 4 と PC-98 の起動の確かめは T1（QEMU の raspi4b と PC-98 の QEMU）に依頼する。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 使っている所の洗い出し、名前付きの object の一覧と HAL 内の定義の置き場所の案、置き換え、build、T1 への依頼 | in-progress（q1254、P3） |
