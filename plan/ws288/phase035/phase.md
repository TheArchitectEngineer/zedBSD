<!-- awesome-plan project=zedbsd record=ws288-p035 -->

# ws288-p035 DC を GHCI の GPU の一部に（CP の command で制御、present は queue の操作、割り込みは GHCI の IRQ）

- 親: [WS288](../ws.md)。今の仕様: amaterrace-gpu `docs/ghci.md` §13（DISP の capability、host が register を直に触る）、§4（IRQ_STATUS）。RTL: `rtl/dc/`（atg_dc・dma・i2c・regs・tgen・tmds・ycc）。
- 状態: in-progress（2026-10-25 13:01 P3、q1576-i01: 実装の段 D1・D2a を commit した。続きは D3・D5。D2b（MC）と D4（CP）は P1 の p034 の merge の後）。
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
- 2026-10-25 13:01 P3（q1576-i01、amaterrace-gpu の worktree p3-p035、main f9f817e を merge した a0fec17 の上）: 実装の段。
  - **D1**（3fc9e79）: GHCI §13 を 13.1 DISP と 13.2 DISP2（draft）に分け、§3.1（IRQ [12:9]）・§3.2・§4（id 11）・§7.3（µop 15 DISP_SET・16 FLIP と表の後の段落。P1 が直した WAIT_IDLE の行は触らない）・§10・§15 を直した。§15 の下にずれていた §12.12 を §13 の前へ戻した。`ghci_regs.h`・`.vh`・`ghci_packets.h` に追加だけの定義。新しい spec: `atg_cp_disp`（34 port）・`atg_cp_seq_dec`（10）・`atg_dc_cursor`（23）。
  - **番号の照合**: §15〜§17 と重ならない（FMT は id 10、PROT は id 9、µop は 14 QUERY まで、IRQ は [8] まで。P1 の p034 は µop・IRQ・HCR の offset を足さない）。DISP2 の窓は HCR の 0x200〜0x2FF（空き）。XCAP_MASK は D4 で 10 → 11 bit にする。
  - **確認 C3**: `atg_cp_seq` は今 40 port（clk・rst_n を含む）。kick 5 本を kick と 3 bit の code に束ねて −3、P1 の +2 で 39。
  - **§18.4 からの変更**（Q1 承認 2026-10-25）: P1 の p034 が `busy_in[0]` を B_MEM（unit の memory の未応答、B_ALL = 0xFF）に使った。そのため DC の待ちは `busy_in` を 9 bit に広げた bit 8 にする。bit 8 は B_ALL にも WAIT_IDLE の 8 bit の mask にも入らない。DISP_SET の flags[0] の待ちは擬似の index 0x3BF をやめ、kick の code で `atg_cp_disp` に知らせる。
  - **自分で決めた事**: (1) record は HALTING の間も送る。送らないと、HALTING で slot を待つ FLIP が詰まる。record を待たせるのは HALTED・FAULTED・RESETTING だけ。(2) record の write が誤りの応答でも IRQ.FLIP を立てる（STATUS[4] と一緒に）。(3) ENABLE = 0 の時の FLIP(1) はすぐに cancelled で取る。(4) `am_amb_mux` は応答の valid を requester ごとに分けるので、DMA の tag を見る変更は要らない。(5) DISP_ERROR は ENABLE = 1 の間 1 frame に 1 回まで。
  - **D2a**: f82b687 は移動だけ（`rtl/dc`・`spec/dc` → `rtl/gpu/dc`・`spec/gpu/dc`、README の行）。8cc0aab は次のとおり: `atg_top` が `HAS_DISP` で `atg_dc` を子に持つ（`C_DC` は C_EXT の場所、+0x10000 の decode はまだ今のまま）。新しい port は pclk・prst_n・pclk_sel・tmds・px_*・I2C。board の top から DC を外した。lint の KC705 の形は EXT_CLIENT=1 の代わりに HAS_DISP=1。atg_top の 4 つの TB は新しい入力をつないだ。`STANDALONE` は D3 で消す（DC の TB の書き直しと一緒）。
  - **check**: `tools/lint.sh` は全部 clean。`sim/run.sh` で atg_kc705_raster・atg_top・atg_dc・atg_dc_regs・atg_dc_dma が PASS（5 件）。長い atg_top_* の 7 つは iverilog の build が warning 0 で、実行していない。
