<!-- awesome-plan project=zedbsd record=ws289-p001 -->

# ws289-p001 調べと設計（SDHCI と GEM の共通化、SD の配置と BOOT.BIN、boot mode と電源、QEMU zynq の SD・GEM）

- 親: [ws289](../ws.md)。成果: [design.md](../design.md)。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-ws289-p001`、branch `agent/p1-ws289-p001`、基点 main a2339786d）。Fable は使わない（ユーザー）。
- Status: in-progress（設計 版 2 = design-reviewer の全指摘に対応済み。Q1 の判定待ち）

## 範囲と到達点

- 範囲: 設計だけ。source は変えない。書くのは `plan/ws289/` の中だけ。
- 到達点: design.md に (1) SDHCI（共通化の範囲、Zynq の癖、DMA、root を SD に）、(2) GEM（共通化の範囲、32/64 bit の descriptor、SLCR の clock、RTL8211E、割り込み 54）、(3) SD の起動（BOOT.BIN、SPL の Falcon、image の配置と make、jumper、SSH の更新と reboot）、(4) QEMU zynq の SD・GEM と T1 の流し方、(5) Phase と LW、WS286 p006/p007/p010 への依存、risk、U 項目。

## 途中で分かった事（設計に反映）

- 2026-10-23 Q1 経由のユーザーの決定「rpi4を参照するよりも、共通部分は分離しましょう。」→ 汎用の SDHCI の core と汎用の Cadence GEM の core を独立の Phase（p002・p003）で分離し、Pi 4 の SD（QEMU raspi4b）と Pi 5 の Ethernet（実機）の回帰の確認を入れた（design §2 D1・D2、§9、§13 D-c）。
- ws.md の目的の節の「PHY の reset は gpio0 46」は誤り（USB の PHY の reset。WS286 design §1.1 は正しい）。Linux と U-Boot の両方の `zynq-zybo.dts` で確かめた（design §1.2）。この WS に Zynq の GPIO の driver は要らない。
- U-Boot v2025.07 の `xilinx_zynq_virt_defconfig` の SPL は `SPL_OS_BOOT=y`（Falcon）だが、SPL の BSS・stack・malloc（`0x100000`〜`0x2200000`）が kernel（`0x00100000`）と DTB（`0x01F00000`）に重なる → config の fragment で RAM の上端に移す（design §5.2）。legacy の uImage は header を load の 64 byte 下に置く（review C1）ので kernel の image は FIT（design §5.1）。
- tree の `drv_dma_map` は coherent の割り当ての中しか map しない（dma.c）→ SD の DMA は bounce になる。この WS は PIO（design §3.5、U3）。
- QEMU zynq の GEM の PHY は address 7（ZYBO は 0）→ core に走査を入れた（design §4.2）。

## 確認

- 実行した command: 読みだけ（`grep`・`sed`・`cat`、`command -v` で host の道具の有無、`curl -sI` で参照の取得の可否、U-Boot v2025.07 の 11 file を scratchpad `refs-ws289/` に取得し SHA-256 を記録）。build・QEMU・実機は無し（設計の Phase）。
- design-reviewer の pass（Opus の design-reviewer、2026-10-23）: Critical 1（legacy の uImage の header が `0x000FFFC0` に書かれる）・High 1（SD 有りで常に 0x80）・Medium 7・Low 15。全部に対応して版 2（design §15 の表）。review の C1 の裏付けに U-Boot の `include/spl_load.h`・`common/spl/spl_fit.c` を追加で取得し読んだ。

## 残り・引き継ぎ

- Q1 の判定、U1〜U7 の判断（design §11）、Q1 への依頼（armv7 の package、`mac0=`、Tools、ユーザーの試験の一覧、Future Work）。
- p002・p003 は WS286 と独立で今すぐ始められる（arm64 の build だけ）。
