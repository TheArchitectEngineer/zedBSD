<!-- awesome-plan project=zedbsd record=ws288-p039 -->

# ws288-p039 MC の行き先ごとの AXI の ID と、汎用の AXI の 2 つへの振り分けの module

- 親: [WS288](../ws.md)。関係: [p013](../phase013/phase.md)（PCIe の bridge の GPU → host の AXI の slave）、[p029](../phase029/phase.md)（host の memory だけ）。
- 状態: in-progress（2026-10-25 17:07 Q1、q1580 P2）
- 依存: p034（MC の保護、main 464b3c7）。

## 決定（2026-10-25 17:07 ユーザー）

Q1 の説明（MC に AXI を 2 本付けるより、1 本のまま行き先ごとに ID を分け、board の interconnect で振り分ける方がよい。client ごとの順序は、行き先が変わる時に前の行き先の応答を待って保つ）の後、「MCはIDの使い分けを実装しましょう。KC705ボードでなく、汎用の部品として、AXI ２つの振り分けをするモジュールを作って、KC705で利用しましょう。」

## 範囲（amaterrace-gpu）

1. **MC**: root で要求の行き先（local = DDR3 / host）を判定し、行き先ごとに AXI の ID を分ける。行き先の判定の範囲は parameter か register（GHCI の MEM の capability で driver に知らせる host の窓の範囲と合わせる。どちらにするかは設計の節で決め、GHCI の文を直す）。client ごとの順序（読み・書きは順番どおり）は、client が前と違う行き先へ出す時に前の行き先の応答が揃うまで待たせて保つ。am_axi_master が ID ごとの応答を扱う。保護（atg_mc_prot）・aperture・quiesce・flush・unit_idle・ROOT_KEEP（DC）と両立。
2. **汎用の振り分けの module**（`rtl/common/am_axi_split` など、vendor に依らない）: AXI4 の slave 1 つ → master 2 つ。アドレスの範囲（parameter）で振り分け、応答を ID ごとに戻す。同じ ID の要求が別の master へ行く時は前の応答を待つ（AXI の規則）。範囲の外は DECERR。幅と clock は変えない（幅・clock の変換は使う側で）。TB と spec。算術は am_ の module。
3. **KC705 での利用**: board top で MC → am_axi_split → (0) MIG（DDR3）、(1) PCIe の bridge の GPU → host の AXI の slave（p013 の口、P2 と調整。p013 の v1 の DECERR の端の代わりに本物の口へ）。BAR2 の経路（MIG の前の interconnect）とは別。
4. driver・firmware の置き場所の方針（ring・fence を host に置くなど）は範囲の外（必要なら後の Phase）。

## 受け入れ

- am_axi_split の TB（範囲の振り分け、ID ごとの応答、同じ ID の待ち、DECERR）PASS、MC の TB に 2 つの行き先の場合（client ごとの順序が保たれる）を足して PASS、lint clean、arith-lint で新しい code が 0。
- KC705 の board の TB（atg_kc705_raster・atg_kc705_pcie）で、GPU から host の窓への読み書きが PCIe の模型に届く。
- make sim・Verilator は T に。

## 記録

- 2026-10-25 17:07 Q1: 新設、P1 に（SU の算術の書き換えの等価の確かめ直しを区切ってから）。KC705 の board top への組み込みは p013 の P2 と調整。
- 2026-10-25 17:07 Q1: P1 は SU の書き換えの等価の確かめ直しで手が離せず、P2 が p038・p013 で区切ったので、新しい P2 に（p013 の board top と PCIe の GPU → host の口を使うので p2-p013 60d6e25 を merge して始める）。
