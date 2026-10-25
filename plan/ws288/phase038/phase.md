<!-- awesome-plan project=zedbsd record=ws288-p038 -->

# ws288-p038 TU の filter の pipeline（p025 の前倒し）

- 親: [WS288](../ws.md)。p025（timing の 1 段目）の範囲のうち TU の filter だけを先に行う。
- 状態: in-progress（2026-10-25 14:05 Q1、q1578 P2）
- 依存: p036 の d94076c（T1-791 で試験中）の上。

## 決定（2026-10-25 14:05 ユーザー）

Q1 の説明（ffilt は 1 cycle に選択・ETC の展開・unpack・重み・FMA を全部行う状態機械で、配線後の最悪の 39 ns・約 110 段。累算 A = fma(w, x, A) の依存は 16 個の累算器と順番の入れ替えで解ける）の後、「ではTUのfilterはパイプラインにしてください。」

## 範囲（amaterrace-gpu）

1. `atg_tu_ffilt`: 前半（slot の選択・ETC/EAC の展開・atg_fmt_unpack・重み）を段に分け、FMA を複数段の pipeline にする。累算の依存は、同じ累算器への足し算が FMA の latency 以上離れる順番（例 texel → lane → 成分）で避け、足りない時（mask の lane が少ない、成分が少ない）だけ止める。結果は今と bit で同じ（tools/tumodel.py の FFilter）。
2. `atg_tu_filter`（8 bit の形式）も同じ観点で見て、TU の最悪の経路にかかる所を段に分ける。
3. 新しい算術は p037 の規則（rtl/common の am_ の module、am_mul の LAT）で書く。
4. 目標: TU の中の最悪の経路を 5 ns（200 MHz）に近づける。OOC で TU の単体の WNS と段数、LUT・FF・DSP・BRAM の前後を測る。処理量（1 quad の cycle 数）の前後も記録。

## 受け入れ

- TB atg_tu_ffilt・atg_tu_filter・atg_tu・atg_top_tex が PASS（bit 一致）、lint clean。make sim 全部と Verilator は T に。
- TU の単体の OOC の WNS・段数・面積の前後の表。met しない経路は p025 への申し送りの表に。

## 記録

- 2026-10-25 14:05 Q1: 新設、P2 に（新しい起動、p2-p036 の d94076c から branch p2-p038）。
- 2026-10-25 15:17 P2 → Q1（申告、途中）: cda8e8c（p2-p038、親 a1fe096 = d94076c に main 73d8dc2 を merge）。新しい atg_tu_fma（fp32 の FMA の pipeline 10 段、atg_su_ffma と bit 一致、TB 175k PASS）、atg_fmt_cvt（atg_fmt_comp の pipeline 5 段）。atg_tu_ffilt: 順番 texel → lane → 成分、前半 8 段、同じ累算器の 6 cycle 以内だけ待つ（5 では FAIL、6 で正確）。1 quad の cycle: nearest 14.6 → 23.6、bilinear 32.2 → 43.1、多 pass 80.3 → 74.3、gather 24.0 → 29.1。atg_tu_filter: 18 段で止まらず quad を重ねる（出力の FIFO 4 と credit）、3420 quad で 39,436 → 33,306 cycle。atg_tu: もう片方の経路が idle の時に pass を渡す。TB atg_tu_filter・ffilt・tu・tu_fma・fmt_cvt・atg_top_tex PASS、lint clean、arith-lint ffilt 105 → 26（atg_tu_etc に残る）・filter 55 → 0。基準の TU の OOC（d94076c、配線後）: WNS −35.142 ns・117 段・LUT 28,796・FF 8,310・BRAM 7.5・DSP 45。cda8e8c の OOC を実行中。
- 2026-10-25 15:29 P2 → Q1（申告、途中）: cda8e8c の TU の配線後の OOC: WNS −35.142 → −21.882 ns。最悪は atg_tu_addr（r_plane、89 段、p038 の範囲外 → 申し送り）。block ごと（配線後の slack ns / 段）: addr −21.9/89、addr/desc −13.4/25、addr/cubeu −11.9/33、cache −5.06/10、ffilt −7.69/22（組み合わせの atg_tu_etc）、filter −2.87/3（np の選択の fanout 321）、cvt −1.32/8、fma −0.77/7。合成 LUT 26,464・FF 13,913・BRAM 8・DSP 60。fe19cec（親 cda8e8c）: atg_tu_etc を 2 cycle の pipeline に（am_ の形）、cvt 6 段、FMA は加算の項を cycle 3 で register に（待ちの窓 7）、filter の np の選択を 1 段前で register に。TB atg_tu_ffilt・filter・etc・fma・fmt_cvt・tu・fmt・atg_top_tex PASS、lint clean、arith-lint atg_tu 321 → 166（filter・etc・ffilt は 0）。fe19cec の配線後の OOC を実行中。
- 2026-10-25 16:04 P2 → Q1（申告、Q1 は report を未確認）: p2-p038 cda8e8c → fe19cec → fe7612b → a0295a7（測定済み）→ a4e323b（tc_ready と IDLE の load の enable を register に、max_fanout 64、TB PASS、配線は未測定）。基点 a1fe096 = d94076c + main 73d8dc2。
  - TU の OOC（atg_tu、LINE_BYTES 64、flatten none、5 ns）: 配線後の WNS −35.142 ns（117 段、ffilt）→ **−22.185 ns**（89 段、最悪は atg_tu_addr の crd → r_plane）。合成の WNS −36.004 → −24.381。合成の LUT / FF / BRAM / DSP: 28,631 / 8,281 / 7.5 / 45 → 26,275 / 14,899 / 8 / 60。
  - a0295a7 の配線後の block ごと（slack ns / 段）: ffilt/fma −0.105/7、ffilt/cvt −0.052/4、ffilt/etc −1.396/10、ffilt −4.083/1・filter −2.017/1（pass の register の enable の fanout 約 800〜900 FF、a4e323b で直した、未測定）、cache −6.689/9、addr/cubeu −11.48/33、addr/desc −16.58/28、addr −22.185/89。合成の見積もりでは filter +1.26・ffilt +0.58・etc +1.20・cvt +1.42・fma +1.40（5 ns を満たす）。
  - 処理量（1 quad が unit を占める cycle、前 → 後）: nearest 14.6 → 28.4、bilinear 32.2 → 48.5、多 pass 80.3 → 80.3、gather 24.0 → 34.0、border 14.5 → 28.2（quad ごとの drain 約 13/23 cycle と FIN 4。定常は 1 cycle に 1 step）。atg_tu_filter（3420 quad）39,436 → 33,306 cycle。
  - 変更: atg_tu_fma（pipeline の fp32 FMA、10 cycle）、atg_fmt_cvt（7 cycle）、atg_tu_etc（4 cycle）、atg_tu_ffilt（texel → lane → 成分、待ちの窓 7）、atg_tu_filter（18 段）、atg_tu（もう片方が idle の時に pass を渡す）、vivado-ooc.tcl の OOC_BLOCKS。これらの算術は全部 rtl/common（arith-lint 0、atg_tu 321 → 166）。
  - 確認: TB atg_tu_ffilt・filter・tu・etc・fma（atg_su_ffma と 175k）・fmt_cvt（400k）・fmt・atg_top_tex（370 s）PASS、mutation は期待どおり FAIL、lint clean。make sim・Verilator は未実施（T）。
  - 残り（P2 が今）: 400 行の規則（am_ を除いて filter 707・ffilt 593・etc 429 行）→ 子に分ける（filter: depth の比較・channel ごとの和・rnd81、ffilt: 前半の datapath、etc: EAC の decoder）→ commit → T。
  - **p025 への申し送り**（a0295a7、配線後）:

    | 経路 | WNS ns | 段 | 試した事・理由 |
    | --- | --- | --- | --- |
    | atg_tu_addr crd/k → r_plane | −22.185 | 89 | p038 の外（plane・LOD の算術を 1 cycle、29 の演算子が inline） |
    | atg_tu_desc cs → d_off の LUTRAM | −16.580 | 28 | p038 の外（descriptor の level の offset を 1 cycle） |
    | atg_tu_cube job → ns/nt_num | −11.480 | 33 | p038 の外（cube の面の選択と除算の準備） |
    | atg_tu_cache t → mline の CE、addr の FSM → cache tc_ctl | −6.689 / −4.750 | 9 / 13 | p038 の外（enable の fanout、addr → cache の handshake） |
    | ffilt/etc w → delta1 | −1.396 | 10 | 4 段にした。RGB の mode・色・modifier の経路が E1 に |
    | ffilt/fma lz8 → st9、cvt pos0 → BRAM の address | −0.105 / −0.052 | 7 / 4 | 配線だけ、合成は +1.4 |
    | ffilt st → tx の CE、filter nq → tx の CE | −4.083 / −2.017 | 1 / 1 | a4e323b で直した、未測定 |
