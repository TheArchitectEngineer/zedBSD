<!-- awesome-plan project=zedbsd record=ws288-p035 -->

# ws288-p035 DC を GHCI の GPU の一部に（CP の command で制御、present は queue の操作、割り込みは GHCI の IRQ）

- 親: [WS288](../ws.md)。今の仕様: amaterrace-gpu `docs/ghci.md` §13（DISP の capability、host が register を直に触る）、§4（IRQ_STATUS）。RTL: `rtl/dc/`（atg_dc・dma・i2c・regs・tgen・tmds・ycc）。
- 状態: in-progress（2026-10-25 12:34 P3: 設計の段を終えた、判定は Q1。実装の段 D1〜D8 は未着手）。
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
- 2026-10-25 12:34 P3（q1574-i01 の 2、zedBSD の worktree p3-p006）: 設計の段。[design.md](../design.md) §18（版 8）を書いた。design-reviewer の review を 2 回受け（1 回目 28 件: Critical 2・High 7・Medium 13・Low 6、2 回目 21 件: High 5・Medium 10・Low 6）、全部を本文に反映した（対応の表は §18.15・§18.16）。要点:
  - DC を `rtl/gpu/dc/` へ移し atg_top の子にする（board の reset の domain、MC の quiesce・flush・axi_idle から外す）。新しい capability DISP2（id 11）。
  - CP が RUNNING の間、scanout の register は ring の µop（新しい 15 DISP_SET・16 FLIP）だけが書く。host が書けるのは IDLE・HALTED・FAULTED の時だけで、どちらか一方だけが書く排他にした。RUNNING の間の host の write は捨てて BAD_WRITE を立てる。
  - present = FLIP。plane ごとに待ちの slot が 1 つ。取り込むと `atg_cp_disp` の record の書き手が record を書き、その後に IRQ_STATUS.FLIP。
  - DC は「全部の unit」の待ちに入れない。scanout の read の誤りは device の fault にしない。DC の event は IRQ_STATUS の [12:9]（MSI は 1 vector のまま）。
  - mode の表と cursor の plane（gclk の中で合成）を足した。image の条件を決め、route は SHARED と COPY の 2 つ（SHARED の可否は import の時に判定）。
  - ADV7511 と HPD は host の driver のまま。driver は UNGATED・queue・scan の hold・recover の時の数え直しを持つ。
  - GHCI §13.2 の書き直しの案、試験、段 D1〜D8（51 LW）。
- U 項目: U90〜U99（推し付き、§18.10）。RTL・firmware・driver・GHCI の文はまだ変えていない。
- v1 で達しない事（§18.1）: zedBSD の display の core は producer の fence を CPU で待ってから backend を呼ぶ。そのため「前の draw の完了」を FLIP の COMPLETE に任せるには core の変更が要る（U98、後の Phase）。また ring が 1 本なので、FLIP は長い class 2 の packet の後ろで待つ（p028 で測る）。
- 2026-10-25 12:41 Q1: P3 の設計（zedBSD agent/p3-p006 4b92046f3: design §18 版 8、review 2 回 28・21 件）を main に merge。U90〜U99 はユーザーの「推しで進めてよい」に従い推しで決める（起きている時に見せる）。実装の段 D1〜D8（51 LW）は次に空いた担当に。番号（DISP2 の id 11、µop 15・16、IRQ [12:9]）は p034 の draft と照合してから。
