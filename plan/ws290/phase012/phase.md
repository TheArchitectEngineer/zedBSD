<!-- awesome-plan project=zedbsd record=ws290-p012 -->

# ws290-p012 表示

- 親: [WS290](../ws.md)。設計: [design.md](../design.md) §18 の p012（版 4.5、ghci.md cb937e6）。
- 状態: planned
- 依存: p007・p009・p011

## 範囲

表示: DISP_SET・FLIP、DISP_IRQ、copied・cursor の写し、reset の数え直し、panic、boot framebuffer、adv7511。+ `DISPLAY | DISPLAY_EVENTS`

詳細は design の該当の節（§4〜§16）と §14 の p012 の場面。

## 受け入れ

- p012 の場面 PASS
- 試験は host 試験と amd64・riscv64 の build（warning 0）。実機は p014 だけ。
- 見積り: 22 LW。

## 仕様の仮の扱い（ghci-review の回答で変われば直す）

design §19 の表のうち、この Phase の code に効く物を着手の時にここへ写す。

## 記録

- 2026-10-28 P3（q1638-i01）: design §18 から立てた。
- 2026-10-28 P3（q1644-i01）: p011 から渡す物: 最初の自分の flip が取られた後に `ghci_memory_boot_release(device)` を呼ぶ（discrete の boot framebuffer の範囲を local の allocator に返す）、detach で表示を boot framebuffer に戻すか ENABLE = 0。window（BAR2）は CAP_BLT 無しの discrete の表示の写しに使える。multisample の行の pitch は N79 の答えまで §12.11 の P（layout.h の `ghci_layout_row`）。
