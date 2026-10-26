<!-- awesome-plan project=zedbsd record=ws290-p001 -->

# ws290-p001 設計: GHCI 2.0 の driver の書き直し

- 親: [WS290](../ws.md)。
- 状態: planned（2026-10-26 Q1 新設）
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
