<!-- awesome-plan project=zedbsd record=ws288-p035 -->

# ws288-p035 DC を GHCI の GPU の一部に（CP の command で制御、present は queue の操作、割り込みは GHCI の IRQ）

- 親: [WS288](../ws.md)。今の仕様: amaterrace-gpu `docs/ghci.md` §13（DISP の capability、host が register を直に触る）、§4（IRQ_STATUS）。RTL: `rtl/dc/`（atg_dc・dma・i2c・regs・tgen・tmds・ycc）。
- 状態: in-progress（2026-10-25 11:32 q1574 P3: firmware の submit の直しの後に設計の段）。
- 依存: p018（firmware）、p006（driver の display.c）、p019（vk/ の swapchain と present）。p019 の「copy しない flip」の段はこの Phase に置き換える。
- 承認: 2026-10-25 09:02 ユーザー「DCがGPUとは別になってしまっていますが、確かに伝統的にはそういう構成だったように思いますが、Vulkan Displayのcapabilityというような抽象化と考えれば、gpu/に移して、CPへのコマンドで制御可能にしてもいいかもしれません。そうすれば、コピーせずにscanoutするのが自然になるでしょう。GHCIの仕様にDCも入れてしまって、割り込みもGHCIの仕様にしませんか？」

## 範囲

1. **設計**（design.md に節、design-reviewer で review、GHCI の §13 の書き直しの案）:
   - DC を `rtl/gpu/dc/` に移し、CP の CPU から DC の register を触る内部の道（host は直に書かない。bring-up の debug と早期の console の扱いを決める）。
   - Vulkan の display の抽象（VK_KHR_display・VK_KHR_swapchain の考え方）に合わせた GHCI の command: display・mode の列挙、mode の設定、plane（主の plane と cursor）、**present を queue の操作に**（前の draw の完了を待って、次の vsync で FB を切り替える。fence・semaphore で順序、copy しない）、vblank・flip の完了・hotplug（ADV7511 の HPD）の event。
   - 割り込みは GHCI の IRQ_STATUS に統合（display の bit、または RESULT の record で知らせる）。PCIe の MSI は GHCI の割り込みの 1 本で足りる（§15.2）。
   - DC の条件（format・1 sample・base と行の揃え・大きさ）と、条件に合わない image の扱い（BLT の copy を firmware の中で、または swapchain の image を条件に合わせて作る）。
   - session の保護（p034）との関係（DC の読みの session）。
2. **実装**: RTL の移動と CP の内部の道・割り込みの統合、firmware の display の command、driver（p006 の display.c を command の形に、vk/ の present）、GHCI §13 の書き直し、TB（present の順序、vsync の flip、event）。

## 受け入れ

design の review、TB と host の試験が PASS、`make sim` の PASS が減らない（T）、driver の vmunix の warning 0。画面の確認は p024・p028 の実機。

## 記録

- 2026-10-25 09:02 Q1: 新設（ユーザーの提案に Q1 が賛成）。
