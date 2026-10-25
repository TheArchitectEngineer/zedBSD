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

## 設計（P2 2026-10-25 17:16、Q1 が転記。P2 の worktree p1-p039 に 60d6e25 を merge = 0ed7436）

- D1 行き先の判定は parameter: MC の root が grant の後の line（保護の walk を含む）を [LOCAL_BASE, LOCAL_BASE+LOCAL_BYTES) なら local、他は host。LOCAL_* は atg_top の parameter = GHCI の MEM の LOCAL_BASE/LOCAL_SIZE（read-only）。register にすると board の固定の振り分け（am_axi_split の parameter）と食い違い得るため。
- D2 GHCI の追加: MEM の capability に +0xC HOST_LIMIT（R）「LOCAL の外で HOST_LIMIT より下の GPU address が host に届く。0 = 2^32 まで」。どちらでもない address は bus error（fault kind 5）。driver は host の buffer を local の外かつ HOST_LIMIT の下に置く。KC705 = 0x8000_0000。atg_cp_regs の 0x1CC。**zedBSD の driver（p006 の範囲）がこの word を読む必要がある。**
- D3 AXI ID 1 bit: 0 = local、1 = host。LOCAL_BYTES = 0（unified）は全部 ID 0。atg_top に m_awid/m_arid/m_bid/m_rid。
- D4 client ごとの順序は am_axi_master で: 要求は順序の key（client 番号、walk は 0xFF）を持ち、ID・方向ごとの FIFO（新 am_fifo_key）が outstanding の key を持つ。同じ方向・同じ key の burst が別の ID に outstanding の間は受けない。読みと書きは方向ごと（AMB の規則のまま）。応答は ID ごとの FIFO、BEATS > 1 は ID ごとの組み立ての buffer。BLT の host → local の copy は読み・書きの方向が別なので待たない。
- D5 保護・aperture は不変（分類はその後）。D6 quiesce・flush・axi_idle は両方の ID を見る。unit_idle は不変。
- D7 DC の root の自分の entry は最優先のまま、DC が待つのは自分の別の行き先の outstanding の時だけ。
- D8 am_axi_split（rtl/common）: slave 1 → master 2、parameter の範囲（range 0 優先、burst の開始 address で判定、4 KiB 境界）、ID・方向ごとの count と行き先、別の行き先に outstanding のある ID の要求は待つ、W は AW の順（deadlock を避ける）、範囲外は DECERR、B は round-robin・R は burst 単位で合流、幅と clock は不変。
- D9 KC705: LOCAL_BASE 0x8000_0000・LOCAL_BYTES 1 GiB・HOST_LIMIT 0x8000_0000。range 0 = local → MIG、range 1 = [0, 2 GiB) → PCIe の GPU → host（512 bit・gclk・ID 4、中で 512 → 128 の幅の変換と clock の変換）。MIG の口に ID は無い（構成上 ID 0 だけ）。sim の model に host memory、board TB で CP の debug port から host の窓に読み書き。
- 注意: MC の既存の算術（atg_mc_cnt・atg_mc_prot）は p037 の範囲で未変換。P1 が MC を書き換える時に atg_mc.v・am_axi_master.v と衝突し得る → MC の p037 は p039 の後。

## 受け入れ

- am_axi_split の TB（範囲の振り分け、ID ごとの応答、同じ ID の待ち、DECERR）PASS、MC の TB に 2 つの行き先の場合（client ごとの順序が保たれる）を足して PASS、lint clean、arith-lint で新しい code が 0。
- KC705 の board の TB（atg_kc705_raster・atg_kc705_pcie）で、GPU から host の窓への読み書きが PCIe の模型に届く。
- make sim・Verilator は T に。

## 記録

- 2026-10-25 17:07 Q1: 新設、P1 に（SU の算術の書き換えの等価の確かめ直しを区切ってから）。KC705 の board top への組み込みは p013 の P2 と調整。
- 2026-10-25 17:07 Q1: P1 は SU の書き換えの等価の確かめ直しで手が離せず、P2 が p038・p013 で区切ったので、新しい P2 に（p013 の board top と PCIe の GPU → host の口を使うので p2-p013 60d6e25 を merge して始める）。
- 2026-10-25 17:56 P2 → Q1（申告）: 575dcf6（p1-p039、親 0ed7436 = main 464b3c7 + p2-p013 60d6e25）。MC: LOCAL_BASE/LOCAL_BYTES、grant の後 local は ID 0・host は ID 1、m_awid/arid/bid/rid、atg_mc.v 382 行。am_axi_master: NID/ID_W/KEY_W、ID ごとの user の FIFO と読みの buffer、順序の key（client、walk は 0xFF）、新 am_fifo_key。atg_top・atg_cp・atg_cp_regs: HOST_LIMIT（MEM +0xC、0x1CC）、ghci_regs.h/.vh。GHCI §11.2 と MEM の表: HOST_LIMIT、どちらでもない access は fault kind 5、local と host の間の完了の順は任意（約束の順序は両方にまたがって保つ）。新 rtl/common: am_axi_split・am_axi_idtrack・am_arb3（と am_fifo_key）、spec。KC705: LOCAL_BASE 0x8000_0000・1 GiB・HOST_LIMIT 0x8000_0000、split の m0 → MIG、m1 → atg_kc705_pcie の GPU → host（512 bit・gclk、新 core atg_kc705_host_dn 512 → 128 と clock 変換）、ip.tcl に追加（Vivado で生成、lint の stub）。sim の PCIe の model に host memory。TB: am_axi_split_tb（新 sim/models/axi_id_slave.v）、atg_mc_tb の case D（2 つの行き先、client の順序）、atg_kc705_pcie_tb の host の group、atg_kc705_raster_tb（HOST_LIMIT、fill を 0x8004_0000 に）。PASS: am_axi_split・am_axi_master・atg_mc・mc_prot・cp_regs・kc705_pcie・kc705_raster・top・top_prot・top_fw。mutation 4 つは FAIL か deadlock。lint rc 0。arith-lint は新 code 0。未確認・残り: axi_pcie の AXI → PCIe の書きの BRESP が posted で host memory に届く前に返る可能性（GHCI §11.5 の fence との順序、p024 か PG055 で）、2 つ目の clock 変換の reset、行き先を交互に変える client の往復の待ち、MC の p037 の書き換え（P1）と atg_mc.v・am_axi_master.v が衝突し得る。T は T1-801。
- 2026-10-25 18:50 Q1: main（cfc49dd、p035 と p013 の 1 vector の MSI を含む）と p039 の 575dcf6 は 15 file で衝突（P2 の p040 の試みで判明）。p039 は前の P2 の作 → 今の P2 が新しい branch p2-p039m（575dcf6 から）で main を merge して解く（MSI は main の 1 vector、DC は atg_top の中、MC の ID・am_axi_split・HOST_LIMIT は p039）。その版を T に回し main へ。T1-801（575dcf6）は参考の結果として扱う。
