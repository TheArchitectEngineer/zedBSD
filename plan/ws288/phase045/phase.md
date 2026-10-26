<!-- awesome-plan project=zedbsd record=ws288-p045 -->

# ws288-p045 DC の clock の領域を分けて 200 MHz に

- 親: [WS288](../ws.md)。関係: [p044](../phase044/phase.md)（範囲 3 で scanout が underflow、720p で 1 frame の約 35% しか読めない）、design §18（DC）・§19.4（clock の領域、U102 は DC の memory の側を gclk に置いていた）。
- 状態: in-progress（2026-10-26 15:57 Q1 新設）
- 由来（2026-10-26 15:57 ユーザー）: 「すみません、gclkが20MHzなんですか？それだとDCは成立しないです。このまま小手先の対応をしてもだめです。クロックドメインを分割して、DCを本来の200MHzにしてください」 → 640x480 などの小手先の案（A・C）は取らない。U102 の「DC の memory の側は gclk」を、DC は自分の 200 MHz の領域に、と改める（ユーザーの判断）。

## 範囲（amaterrace-gpu、設計を先に）

1. 設計: DC の scanout の DMA と FIFO の書きの側を 200 MHz の新しい clock（dclk）に。dclk の出所（規則 docs/style.md の Board clocking: logic で分周しない、MMCM/PLL の出力は可。候補は MIG の ui_clk 200 MHz か SYSCLK からの MMCM の 200 MHz の出力）、DC の memory の道（今は AMB の client C_DC → MC の木 → gclk → MIG。案: DC が自分の AXI の master を dclk で持ち、板の memory の xbar（atg_kc705_mem_xbar）に 3 つ目の master として MIG の ui_clk の側でつなぐ、など）、register の側（gclk の CP・atg_cp_disp との境は CDC）、pclk の側は今のまま、session の保護（PROT、§17）の検査の道、atg_top の他の板（ZYBO・sim）での形（parameter で今の形にも戻せる）。design-reviewer で review。決めた形を Q1 に送り、Q1 がユーザーに示す。
2. 実装: RTL（CDC は am_afifo・am_sync・am_axi_cdc、演算は rtl/common の am_、400 行の規則、spec）、XDC の clock と CDC の制約、TB（atg_dc・atg_top_disp・板の top の TB）、lint。dclk 200 MHz で DC の領域の timing を met（OOC と板の配線）。
3. 板: bitstream を作り、JTAG で書き（MEM_READY を確かめる）、VF2 から 720p の scanout が underflow しない事（SCAN_WORDS = 921,600、underflow 0）、ユーザーの目視。

## 受け入れ

720p60 の scanout が underflow 0 で 1 frame を全部読む（実機）。sim の TB が PASS（T）。board の build の warning の一覧をユーザーに見せる（2026-10-26 のユーザーの指示）。

## 記録

- 2026-10-26 15:57 Q1: 新設。担当は P1（新しい起動）。
- 2026-10-26 15:59 ユーザー「いったん停止してください」→ Q1 が P1（p045）に停止を伝えた。P2・P3・P4・T は動いていない。
