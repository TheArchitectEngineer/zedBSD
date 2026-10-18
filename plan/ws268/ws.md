<!-- awesome-plan project=zedbsd record=ws268 -->

# WS268 x68k（X68000）を再び build できるようにする

- ID: ws268、Master: [master.md](../master.md)、Primary Milestone: MG008
- Status: incomplete（2026-10-19 P2 q1258。p001 の build・検査は済み、MAME の起動は未実施）
- 起点（2026-10-18 ユーザー）:「sun4u, x68k, それぞれ、再びビルド可能にするWSを作ってください。sun4uはqemuで動けばOKです。x68kはエミュレータのセットアップ、もうわからないですよね？わからなければ、ビルドだけでいいです。virt 68kが動けばいいです。」
- 今の壊れ（2026-10-18 P2 の報告・Q1 の確かめ）: `bootloader/x68k/stage2.c` と `src/hal/m68k/bsp-x68k/boot.c` が使う `struct x68k_boot_handoff` の定義が、2026-10-03 のリポジトリの作り直しの時から tree に無い。toolchain は GCC（`m68k-linux-gnu-gcc`、platform/x68k）。
- emulator（Q1 の調べ、2026-10-18）: 手順の記録は無い。host に MAME（`/usr/games/mame`）、`~/x68030.zip`（MAME の X68030 の ROM の組と思われる）、`~/x68k-emulator/`（SHARP の公開の BIOS `iplrom30.dat` と許諾の文書）がある。ユーザーの指示「わからなければ、ビルドだけでいいです。virt 68kが動けばいいです。」→ 起動は任意（試して分からなければ build だけで完了）。m68k の動く確かめは WS266（virt）で行う。

## 完了の条件

1. config の x68k の vmunix と boot loader・image が warning 0 で build できる（handoff の構造体は boot loader → HAL の物として HAL か bootloader/ に定義、Guardrail の受け渡しの規約に従う）。
2. 任意: MAME の X68030 で起動を試し、手順を記録する（成立しなければ「未実施」と書く）。
3. 規約の全文。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 壊れの洗い出しと直し、build。任意で MAME の起動の試し | in-progress（build 済み、2026-10-19 P2） |
| p002 | 規約の全文の見直し | planned |
