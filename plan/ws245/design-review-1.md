# ws245 design.md の敵対的レビュー 1（2026-10-16）

設計の担当（P2、q991）が起動した design-reviewer の報告を Q1 が要約した物（担当は file を書かずに返した）。対象は commit 6bccaef57 の design.md。blocker 6・major 19・minor 14。

Q1 が元の資料で確かめた点: B2（plan/ws031/display-ref/dmesg-display.txt:35 rawclk 19200 kHz）、B6（regs-selected.txt:68 HSW_PWR_WELL_CTL2 0x00000c03）、M1（dmesg-display.txt:17-18 has_cdclk_crawl yes・squash no）、M8（display/tc-kern.c が実在）。他は未確認。

## blocker

- **B1** 記録と design.md が別物（P01〜P23・D1〜D6 の記録）。→ 2026-10-16 に記録の誤りとして取り消した（485ad5be6）。解決済み。
- **B2** PCH の raw clock を 38.4 MHz としている（§1・§2.1・§2.2・§2.11）。5330 は 19.2 MHz（backlight 96000 ＝ 19.2 MHz ÷ VBT の 200 Hz）。SFUSE_STRAP から決め、参照 clock 38.4 MHz と分ける。
- **B3** 「CDCLK を変えない」（§2.6・§7 #9）と PRM の init/uninit・DC9 の resume が矛盾。xd-clock に CDCLK の off → 既知の値への on・off を入れる（p005）。
- **B4** 逃げ道 `i915.display=old` が成り立たない（own の file を xd の口に付け替え、struct i915_display の field を消すと旧 code は呼ばれず compile もできない。head の付け替え先は p009 の成果物）。(a) 口を p002 で決め実装を丸ごと切り替える（旧は薄い adapter）か、(b) 逃げ道は commit の rollback と割り切る。
- **B5** Type-C を TGL の機構で書いている（TCCOLD の pcode 0x80000026 は TGL 専用、ADL-P は AUX_USBC の well で block。live は DE_HPD_ISR・SDEISR、pin は DFLEXPA1）。§2.8 を ADL-P の流れで書き直す、modular FIA を明記。
- **B6** power well の構造の誤り（ADL-P の PW_A は PW_2 と独立、B/C/D が PW_2 の下）と、pipe の well の上げ下げでの DE_PIPE の割り込みの再設定（interrupts.c の post_enable/pre_disable の hook）の欠落。

## major

- M1 CDCLK は squash でなく crawl、表は adlp_cdclk_table（179.2/192/307.2/556.8/652.8）。
- M2 pipe count の mailbox は DG2 専用、ADL-P では送らない。
- M3 DBUF の表の出典の誤り（TGL は 2 slice、ACM は DG2）、MBUS の join・DBUF_CTL の tracker・1→2 画面の resident の灯し直しの欠落、screen_plan に「並ぶ pipe」の入力が要る。
- M4 VBT の DDC pin（1/2/3..6）と GMBUS の pin（1/2/9..12）の取り違え、adlp_ddc_pin_map の表が要る。
- M5 DMC の load の WA（PIPEDMC_GATING_DIS）、event handler の disable、PIPEDMC_CONTROL の enable の欠落。
- M6 lock の規則の不足（DDI_BUF_CTL の bit 6 と bit 31 の別 worker の rmw、AUX の共有）。
- M7 TC の IN_USE → FREE を無条件にしている（links 0 を待つ中間状態が要る）。
- M8 §4.2 の口の表が網羅していない（tc-kern.c の 17 関数、display.c の dc9・lcd_kernel_*・hpd_* ほか、Gen9 が internal.h の型を使う、dc9.c・capture.c の扱い）。§4.1「Gen9 は一切触らない」は誤り。
- M9 kernel の execution 試験（ktest-display・ktest-gt ほか）が試験の扱いから抜けている。
- M10 新しい host 試験の置き場所が WS の中（完了で消える）。src の tests と master.md の Tools へ。
- M11 依存の誤り（独立なのは p003〜p005、p011 ← p009・p010、p010 ← p009）。
- M12 受け入れの oracle の不整合（BUF_CFG 0x0fdb0000 と cursor の DDB 0 の方針、空の i915_cdclk_info.txt、regs-selected.txt に PORT_TX が無い）。
- M13 suspend・resume の quiesce と HPD の re-arm の欠落。
- M14 GOP の引き継ぎで VGA plane の disable・pipe の色の register の初期化が無い。
- M15 PRM の許諾が「その product で動く実装」で、TGL・ACM の PRM から ADL-P の実装に及ぶか自明でない。
- M16 §5.4「Linux の関数は Procedure referenced に書かない」が guardrail と食い違う。
- M17 module の分け方が Linux の file とほぼ 1 対 1（構造を写さない規則での監査の危険）。境界の根拠の列を足す。
- M18 LW の過小評価と換算の矛盾（概算 50〜60 LW、推測）。
- M19 Tiger Lake（5320）を支えないことは今の機能を失う。p008 より前の判断。

## minor（要旨）

m1 ADL の公開 PRM の有無を再確認、m2 DPLL4、m3 CFGCR0 の WA、m4 AUX の well の待ち（600 µs）と timeout の混在、m5 GMBUS の NAK の扱いの食い違い、m6 register の名前（PIPE_ARB_CTL、PIPE_BOTTOM_COLOR）、m7 IPC の enable、m8 resident と head の pipe の衝突、m9 HPD long の再 probe、m10 p002 の試験と p011、m11 p002 から kernel の build に入れる、m12 hotplug の license は MIT・grep の文字列、m13 照合の Linux の tree が P1 の build/ に依存、m14 記述の細部。

## §7 に足す判断（レビューの提案）

B4 の逃げ道の形と p008〜p009 の 2 画面、B3 の CDCLK の範囲、M15 の PRM の許諾、M19 の TGL の時期、PRM と Linux の値が食い違う時の優先、M10 の回帰試験の置き場所、M17 の module の分け方の基準。
