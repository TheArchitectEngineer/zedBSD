<!-- awesome-plan project=zedbsd record=ws286-p001 -->

# ws286-p001 調べと設計（ARMv7-A の HAL・toolchain・ZYBO の BSP・driver）

- 親: [ws286](../ws.md)、Queue: Q1 の割当（P1、2026-10-23、worktree p1-zybo・branch agent/p1-zybo、基点 main 6206d397e）。
- Status: in-progress（版 1 を書いた。2026-10-23 のユーザーの指示で別の担当に引き継ぐ: §12 の決定が未反映）。
- 成果物: [design.md](../design.md)（版 1、§0〜§11 + 未反映の決定 §12）。

## 目的と受け入れの条件

- ARMv7-A の HAL（入口・MMU・例外・GIC・timer・SMP・L2・VFP・atomic・TLS・ABI・signal）、toolchain/sysroot、起動の道と license、BSP と driver（UART・SLCR・SD・GEM+RTL8211E・USB・devcfg・reset）、試験、Phase と LW、risk、判断を design.md に書く。
- 受け入れ: Q1 の手配する review で致命の欠落が無く、U1〜U6 の判断が付く。

## 記録（2026-10-23 P1、版 1）

- 読んだ物: design §0 の表（tree の file と、Linux v6.12・U-Boot v2025.07・embeddedsw 2024.2・QEMU v10.0.11 の source。scratchpad に取り、tree には入れていない）。
- 実行した command: `git worktree add …/p1-zybo -b agent/p1-zybo main`、`grep`/`sed`、`qemu-system-arm -M help`・`-cpu help`、`curl` で source の取得。build・QEMU・実機は無し。
- 書けている節: §1（板と SoC の事実、clock、CPU1、QEMU の machine）、§2（toolchain: LLVM に ARM target が無い → Q1 の変更、sysroot の案）、§3（HAL の設計全部、hal.h は不変、arch.h/armv7.h の 3 点は事前承認の形）、§4（SD と U-Boot の起動 — **JTAG 先行の決定の前の版**）、§5（driver）、§6、§7（試験）、§8（Phase、**順の見直しが要る**）、§9（risk）、§10（判断）、§11。
- 未だの物: §12 の決定の反映（JTAG の起動を先に、OpenOCD の script の設計、SwitchBot 無し、OSS だけ）、`proposed/hal-armv7.md`・`proposed/boot-test-armv7.md`（p002 で書く予定だった）、review。
- HAL の API（hal.h）の変更: 不要。arch.h の節と arch/armv7.h の新規、handoff の名前 2 つ（design §3.13、U1）。
- toolchain の変更（Q1 の許可）: `LLVM_TARGETS_TO_BUILD` に `ARM`、patch level 11（p002 の前）。zedbsd の triple `armv7-unknown-zedbsd-eabihf` と sysroot（p008 の前）。
- 計画に無い依存: U-Boot SPL の build（GPL、tree の外、host の cross gcc）。OpenOCD の手順の確認（§12）。
