<!-- awesome-plan project=zedbsd record=ws290-p001 -->

# ws290-p001 設計: GHCI 1.0（ghci.md 89529de）の driver の書き直し

- 親: [WS290](../ws.md)。
- 状態: in-progress（2026-10-28 仕様の疑問は全部解決、正本は ghci.md 89529de。次は design 版 4 の仕上げ（手順は [ws.md](../ws.md) の「再開点（2026-10-28 ハンドオーバー）」）。前: 2026-10-27 q1621-i01 P3 で版 3 を書きかけて中断、2026-10-26 q1594-i01 P1 で版 2）
- 由来: WS290 の出典と決定（ws.md）。

## 範囲（code は書かない）

1. 読む物:
   - amaterrace-gpu の `docs/ghci.md` 全文（main 4d12da1、読み取り専用）。2.0 の部分（§1.2 の Key Architecture Tenets、§3.1・§3.2〜3.5、§6、§7.1〜7.3、§8、§9、§10、§11、§12、§13.2・§13.3、§14、§15、Appendix A）を driver の要件として読む。§3.1.1・§7.5 などの 1.1 の部分は対象外（捨てる物の確認にだけ使う）。
   - amaterrace-gpu の `include/`（ghci_regs.h など）と `plan/`（am-ws001-p004 の MMU、am-ws001-p005 の複数の ring、am-ws001-p007 の 2.0 化）。include が 1.1 のままなら、2.0 の値は ghci.md から driver の側の header に起こす。
   - zedBSD の今の driver `src/drivers/gpu/ghci/`（33k 行: ghci.c・pci-attach.c・cp.c・memory.c・firmware.c・display*.c・jobs.c・vk/・compiler/ など）、それを使う userland（`userland/base/ghcictl`、`userland/tests/ghci-*`）、試験（`plan/ws288/tests/ghci-*`）、platform の結線（`src/kern/platform/pcat.c`、`src/drivers/platform/visionfive2/vf2-pcie.c`、platform の vmunix.mk）。
   - 参考: WS288 の [design.md](../../ws288/design.md)（driver との境界、§8 の `vk/`）、ws288-p043・p044 の記録。
2. 書く物 `plan/ws290/design.md`:
   - ghci.md 2.0 の要件の一覧（節ごと）と、今の driver の各 file の行き先（残す・直す・捨てる・新しく書く）。GSI1 の compiler（§12）、表示（DISP2）、Vulkan の経路（`vk/` の中継を、PSO・descriptor・`CMD_*` を driver か userland で組み立てる形へ）をどう扱うか。
   - 新しい構成（file と責務）、attach から ring・fence・割り込み・fault/reset・VM（ring の VM の操作、2 段の page table、TLB の無効化の順序）・discrete と unified（§11.3）・PCIe の binding（§15.2）までの流れ。
   - host 試験の設計: GHCI 2.0 の device の model（register・ring の消費・fence の書き込み・割り込み・fault の注入）を zedBSD の tree の中に持ち、driver の code を host で動かす。amaterrace-gpu の sim・firmware は使わない（ユーザーの決定）。
   - 他の platform（VF2 の riscv64 の M.2）と userland（ghcictl、ghci-* の試験）の扱い。
   - 実装の Phase の分割（p002〜、依存、各 Phase の受け入れと host 試験、LW の見積もり）と、最後の規約の全文の見直しの Phase。
   - **ghci.md の疑問の一覧**: 2.0 の部分の誤り・食い違い・未定義（例: §3.1 の 19 register と §3.2 以降の説明の整合、ring の pointer の単位、版の表記「Version 1.0」と GHCI_VERSION 2.0、PSO の 48 byte の配置、VM の操作の command の形）。推測で埋めず、それぞれに driver の側の推しの案を添えて、ユーザーの判断を待つ項目として書く。
3. design-reviewer で敵対的なレビューを受け、指摘を反映する。

## 受け入れ

- design.md があり、上の項目を覆う。design-reviewer の指摘が反映済み。疑問の一覧がユーザーに示されている。
- source は変えていない。

## 記録

- 2026-10-26 Q1: 新設。
- 2026-10-26 P1（q1594-i01、worktree `/home/awe/zedBSD-worktrees/p1-ws290`、branch agent/p1-ws290）:
  - 読んだ物: amaterrace-gpu `docs/ghci.md`（**4d12da1**、`git show` だけ。読んだ時点の main be3cf15 と `docs/`・`include/` に差なし）、同 `include/ghci2_regs.h`・`ghci_cmd.h`・`ghci_layout.h`・`gsi1_isa.h`、同 `plan/ws001/ws.md`・`phase004`・`phase005`・`phase007`、zedBSD の `src/drivers/gpu/ghci/`（49 file、33136 行）、`include/uapi/gpu-op.h`・`include/drivers/gpu/gpu.h`・`include/drivers/generic/dma.h`、platform の vmunix.mk、`pcat.c`・`vf2-pcie.c`、userland の ghcictl・ghci-blit・ghci-display、`plan/ws288/tests/`、amdrdna の `vk/`、WS288 design §6。amaterrace-gpu には書き込み・build・git の状態の変更をしていない。
  - 書いた物: [design.md](../design.md)（版 1 2721c49fc → 版 2）。構成（§4）、流れ・状態・寿命・lock・割り込み・DMA・失敗（§5〜§10）、executor（§11）、compiler（§12）、表示（§13）、host 試験（§15）、Phase p002〜p013（196 LW、§18）、ghci.md の疑問 G01〜G39（§19）、ghci.md 以外の判断 U1〜U7（§20）。
  - design-reviewer: 28 件（Critical 1・High 6・Medium 17・Low 4）、本文 [design-review.md](../design-review.md)。全部を版 2 に反映（design.md §23）。
  - 確かめ: `git diff --check -- plan/ws290/` で出力なし。source は変えていない（`git status` で plan/ws290/ の外に差なし）。build・host 試験・QEMU・実機は対象外（設計の Phase）で未実施。
  - 残り: Q1 が §19 の G01〜G39 と §20 の U1〜U7 をユーザーに示す。Q1 が p002 以降の phase.md を design §18 から立てる。WS288 の 1.1 の試験の削除は p003・p004 で path を Q1 に送る。
- 2026-10-27 P3（q1621-i01、worktree `/home/awe/zedBSD-worktrees/p3-ws290`、branch agent/p3-ws290、main 706f1ee5e を merge）:
  - 2026-10-27 13:55 ユーザー「P3で、~/amaterrace-gpu/docs/ghci.md の更新を読んで、GHCIドライバを更新してください。」（q1621）。補足（Q1 経由）「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」
  - 読んだ物: amaterrace-gpu の `git diff 3af62ca ce9725a -- docs/ include/`（19 file、+4109/−977。ghci.md・ghci_regs.h・ghci2_regs.h・ghci_cmd.h・ghci_frame.h を読んだ）、読んだ時点の main **ce9725a**（2026-10-27 13:56）、docs/・include/ の最後の差 **f718918**（13:15。f718918..ce9725a は config/minimal.mk・plan/ws003/minimal_fit_report.md・tools/vivado-synth-l2.tcl）、`plan/ws001`〜`ws004` の ws.md の差、`plan/ws003/phase006`、`config.mk.template`、`rtl/gpu/cp/atg_cp_regs.v`。`git show`・`git diff`・`git log` だけで、amaterrace-gpu には書いていない。
  - 見つけた主な食い違い（design 版 3 §0.1・§19.2）: TO-BE の VERSION が 0x00010000 に（legacy 1.1 は 0x00010001）、CAPS・CAPS2 が 0x020・0x024 と書かれ 19 register の RING_BASE_LO/HI と衝突（G41）、EXT_CAP_FMT0・FEATURE が include と RTL だけ（G42）、RTL の slim の EXTCAP_PTR = 0（G43）、CMD_DRAW_MESH_TASKS が include だけ（G44）、重なる flag（G45）、flag の無い build option（G48）、DISP_SET・FLIP の opcode が無い（G49）。design-reviewer は RTL の DEVICE_CAPS が CAPS の layout を返す事（V07）も見つけた。
  - 書いた物: [design.md](../design.md) 版 3（書きかけ: §0.1、§1.1、§2・§3・§4・§5.1・§11.6・§12・§13・§15・§18 の版 3 の部分、§19.1・§19.2、U8、§24 capability と Vulkan への対応、§25）、[ghci-questions.md](../ghci-questions.md)（各 G の状態、G40〜G49、行の番号の直し）、[design-review-v3.md](../design-review-v3.md)（25 件、V20 以外未反映）。source は変えていない。
  - ユーザーの G01 の回答（原文は [ws.md](../ws.md)）で「単一の GHCI 1.0、版で分岐しない」と決まり、「そのほかは仕様を調整中です。いったん作業を終了していいです。」で中断。hardware の担当の一次回答は main の `plan/ws290/ghci-answers-hw-20261027.md`（ecd4407e8）に Q1 が置いた（未反映、ユーザー「回答は修正されるかもしれません」）。
  - 確かめ: `git diff --check -- plan/ws290/` 出力なし。build・host 試験・QEMU・実機は対象外で未実施。
- **再開**: 条件はユーザーが ghci.md（と資料全般）の修正の終わりを伝えた時。手順は [ws.md](../ws.md) の「再開の手順」（修正後の ghci.md と ghci-answers-hw を読む → 版 3 を G01 の回答で仕上げ、design-review-v3 を確かめて反映 → design-reviewer → p002 以降の phase.md → 実装）。

- 2026-10-28 Q1: GHCI の担当との疑問のやり取りを終了（レビュー 第 9 版で N01〜N36 が全部解決、ghci.md 89529de）。受け入れの「疑問の一覧がユーザーに示されている」は満たした。残りは design 版 4 の仕上げ・design-reviewer・Phase の確定。
