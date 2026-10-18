<!-- awesome-plan project=zedbsd record=ws267 -->

# WS267 sun4u（sparcv9）を再び build できるようにし、QEMU で動かす

- ID: ws267、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: planning（2026-10-18 Q1 作成。担当は未定。WS261 p002（sun4u の boot.h の構造体）の後）
- 起点（2026-10-18 ユーザー）:「sun4u, x68k, それぞれ、再びビルド可能にするWSを作ってください。sun4uはqemuで動けばOKです。x68kはエミュレータのセットアップ、もうわからないですよね？わからなければ、ビルドだけでいいです。virt 68kが動けばいいです。」
- 今の壊れ（2026-10-18 P3 の調べ、WS261 p002）: sun4u の vmunix が build できない。`src/hal/pmem-constraints.c` が無い（No rule）、HAL の page.c に `struct hal_pmem` の未定義、kernel の .c（entry・vfs・platform/sun4u）が `KERN_UAPI_NATIVE` 無しで #error。boot loader の stage1・stage2 と HAL の一部の object は作れる。
- 注意: sparcv9 はユーザーも main の checkout で作業している（memory）。担当が触る前にユーザーに確かめる。

- WS261 p002（2026-10-18）で sun4u の受け渡しを規約に合わせた。build が直った後の QEMU の起動の確かめの文面（image の作り方、`qemu-system-sparc64 -machine sun4u -m 256 -drive file=<image>,format=raw,if=ide -boot c`、gdbstub で見る所）は git の 987eb3e70 の plan/ws261/phase002/phase.md の「T1 への依頼の文面」。判定は画面にする（serial の log は使わない、AGENTS.md）。

## 完了の条件

1. config の sun4u の vmunix と boot loader・image が warning 0 で build できる。
2. QEMU（`qemu-system-sparc64 -M sun4u`）で起動し、login まで（判定は画面、serial の log は使わない。画面の手段は p001 で決める）。
3. 規約の全文、HAL・受け渡しの規約（Guardrail）に合う。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| p001 | 壊れの洗い出し（いつから・何が）、直す順、QEMU の起動と画面の判定の方法 | planning |
