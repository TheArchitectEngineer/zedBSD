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
- 2026-10-26 16:00 P1 が停止（q1593-i01、設計の文・RTL・Vivado は未、commit なし。worktree: amaterrace p1-p045 = c7af33f、zedBSD agent/p1-p045-dc = cbafb5376、どちらも clean）。読んだ範囲での見立て（板では未確認）: (1) 35% の理由は memory の latency より pixel の道が gclk で 1 pixel/cycle（atg_dc_dma の unpack と cursor の段）: 20 MHz で 20 Mpx/s、720p60 の平均 55.3 Mpx/s の約 36% で、実測 35.4% と合う。200 MHz なら 200 Mpx/s で 720p（74.25 MHz）も 1080p（148.5 MHz）も足りる。(2) 読みの要求は最大 8 line × 64 B（LFIFO_LOG2 3 = 512 B）、2 cycle に 1 回 → 512 B / 往復の latency、MIG への直の 200 MHz の道なら 720p はほぼ足り、1080p（line の間は約 594 MB/s）には LFIFO と burst を上げる。(3) MC の root で DC の要求（entry D）は session の保護の検査を受けないが aperture・VRAM の窓の外は拒否される → DC が板の crossbar に自分の AXI の master を持つなら、その検査を dclk の領域で（aperture の register は gclk、quasi-static の CDC）、誤りは DC の読みの誤り（STATUS[3]）として返し、ROOT_KEEP・QUIESCE_EXEMPT の C_DC の扱いを直す。再開: 板の file と docs/style.md を読んで design の節、design-reviewer、Q1 に要約。古い branch agent/p1-p045（19b52e2f4、main に入り済み）は消してよい。
- 2026-10-26 16:01 ユーザー「今から勝手に進めず、私と相談しながら進めましょう。200MHzはこのボードのシステムクロックです。PLLはいりません。それはわかりますか？」→ WS288 の板の作業は、Q1 も担当も一段ごとにユーザーと相談してから進める（担当の起動・板の操作・build も含めて、先に案を示して承認を得る）。DC の 200 MHz は KC705 の SYSCLK（200 MHz の差動の発振器、AD12/AD11）をそのまま使い、PLL/MMCM は要らない。
