<!-- awesome-plan project=zedbsd record=ws288-p030 -->

# ws288-p030 Vulkan 1.0 core の残り（ROP・format・primitive・query）

- 親: [WS288](../ws.md)。出所: [design.md](../design.md) §15.18（p027 の設計で見つけた食い違い 11 項目、U46）。
- 状態: in-progress（2026-10-25 02:32 q1559 P1: 設計の段）
- 依存: 設計の段は無し。実装の段は p009 part F（TU）・p027（ROP・RX の MSAA）と同じ module に触るので、段ごとに Q1 が順を決める。
- 順: p027 の後・p018 の前（U46 の推し、firmware と driver がこの形に依る）。
- 承認: 2026-10-25 02:32 ユーザー「U項目は推しでOKです。ただ、U44はちゃんとあるべき論で実装するのがいいと思います。U46は、N=2でもう１つサブエージェントを立てて、実装しましょう。もし検証が多いなら、N=3でもいいです。」

## 範囲（design §15.18）

1. ROP の stencil の test・op・front/back（facing）。
2. D16_UNORM（ROP・TU）。
3. stencil の aspect の sample（TU、`sampledImageStencilSampleCounts`）。
4. color attachment 4 つ（MRT、`maxColorAttachments` の最小 4、SU の exp と ROP の RT）。
5. Vulkan の必須の color attachment・sampled の format（表を文面で確かめる）。
6. point と line の描画（strictLines = FALSE の MS の line を含む）。
7. occlusion の query（MS では sample を数える）。
8. SPIR-V の front end の FragDepth・SampleMask（p010 の範囲、ここでは GHCI・ISA の側の口と記録だけ）。
9. D24S8 の depth だけ・stencil だけの clear・copy（BLT）。
10. input attachment（`subpassInputMS` を含む）。
11. TU の RGB565 の filter（texl/texb）。
- 最初に、Vulkan 1.0 の文面で必須か任意かを確かめ、ほかに抜けている core の必須（例: blend の op・factor の全部、depth bias、depth clamp/bounds の要否、primitive restart、viewport・scissor の数、logic op の要否）も表にして加える。

## 受け入れ

設計の段: design.md に節（review 済み）、実装の段の分け方と LW。実装の段: 各項目の model・単体 TB・scene が一致、`make lint` clean、`make sim` の PASS が減らない、`make verilator` PASS。板・timing は対象外。**item 8（FragDepth・SampleMask）と item 10（input attachment）は RTL を足さないので、この Phase の受け入れは「GHCI の口と記録」まで**（design §16.13・§16.14。実装と試験は p010・p018 の範囲、U50・U51。2026-10-25 P1、review 21）。

## 記録

- 2026-10-25 02:32 Q1: 新設（U46 のユーザーの決定）。設計の段を P1（kernel-and-driver-designer）に。
- 2026-10-25 P1: 設計の草稿を design.md 版 6 の §16 に（Vulkan 1.0 core の limit・feature・必須の format の表、format の土台、ROP・RX・VF・TU・BLT・SU・CP の設計、GHCI の draft（amaterrace-gpu p1-p030 の `docs/ghci.md` §8.5・§12.12・CAPS[30]・FMT・µop 14）、段 S1〜S11 = 68 LW、U47〜U57）。design-reviewer の review はこの後。
- 2026-10-25 P1: design-reviewer の review 1（35 件: Critical 2・High 4・Medium 16・Low 13、[design-review-p030.md](../design-review-p030.md)）を全部確かめて反映（design §16.26）。Critical: 圧縮の format の 1 組は 1.0 の必須 → ETC2 + EAC（§16.11a、S12、U58）、RX・ROP の範囲の SET_REG の待ちの hazard（87457fa に既に有る）→ §16.8a。Q1 の依頼（BLT の RESOLVE の平均 3〜7・8 byte の画素）を §16.2・§16.12・S10 に。段 S1〜S12 = 88 LW、U47〜U59。受け入れの item 8・10 の範囲を上に直した。
- 2026-10-25 P1: review 2（23 件: High 4・Medium 5・Low 14）を反映: far も geometric に clip（frag の sample ごとの clip を止めた）、TU・program・LOAD_CONST の待ちの hazard を記録して p027 の修正へ（Q1 に依頼）、BLT の分割を S10 に、stride を 2·S·ps の倍数に、U60（TU の slot が stage で共有）。Q1 の決定で CP の待ちの修正は p027 へ、ユーザーの決定（「圧縮テクスチャはPCIe GPUの実機bringupの後に回します。」）で ETC2 は範囲の外へ。段 S1〜S11 = 79 LW、U47〜U60。設計の段の review 済み（2 回）、U47〜U60 の回答待ち。
- 2026-10-25 04:06 Q1: 設計の段の merge（zedBSD の agent/p1-p030 11e12742f）。design 版 6 §16、review 2 回（35 件・23 件）全部反映、段 S1〜S11 = 79 LW（ETC2 の S12 はユーザーの決定で外し Future Work fw-amaterrace-ctex）。amaterrace-gpu の `docs/ghci.md` の案（p1-p030 c5e17d5）は p027 の ghci.md と合わせて後で merge。U47〜U60 はユーザーに提示。cp_seq の待ちの直しの範囲（TU・program・LOAD_CONST・ATTR_LIMIT）を p027 の P2 に足した。
- 2026-10-25 04:11 ユーザー「PCIEの試験環境は今夜整う予定ですので、少し時間が空きました。テクスチャ圧縮も含めて、Vulkan 1.0の必須項目を実装してしまいます。お願いします。」→ 範囲を Vulkan 1.0 core の必須の全部に戻す: S12（ETC2+EAC の圧縮の texture、design §16.11a）を範囲に戻し、U59（varying 64 component）と U60（TU の slot を stage ごとに 16）も必須なので範囲に入れる（段と LW は P1 が足す）。
- 2026-10-25 05:11 P1 → Q1: S1（format の土台・spec・GHCI の draft の直し）を p1-p031 2ebc9e1・3e24ad1 に。P1 の申告: fmtmodel selftest（D24 の往復 2^24−2 の全部）・srgb-tables check・atg_fmt 5 行・atg_fmt_srgbenc 2 行 PASS、MUT_SRGB_ROUND で FAIL、lint clean 103。Q1 の判断: depth の 4 format の FILTER_LINEAR を立て、S9 で compare 無しの depth の linear も入れる。Q1 が main に merge（a87523c）。S2 以降は新しい起動で。
- 2026-10-25 05:26 P1 → Q1: S2（VF）p1-p030b 48e35a8（親 5f3b747）。VF の VkFormat（ATTR_DST[31:24]）、ATTR_LIMIT（範囲外は読まず (0,0,0,1)）、R32G32B32 の 12 byte、prim_vtx[25:24] の kind、atg_top・atg_rx・atg_rx_clip の幅 24→26。P1 の申告: lint 111 clean、atg_vf_conv 5・atg_vf_fetch 6・atg_vf_asm 6 行 PASS、MUT_ATTR_LIMIT_GE で FAIL。sim-quick は S3 とまとめて T に。
- 2026-10-25 05:42 Q1: T1-762（main 5f3b747、p031・p030 S1・p027 S1〜S5・S7 の merge の後）PASS: make sim 85 PASS・0 FAIL（Q1 が log を数えた）、sim-quick 83、verilator 5、lint 111。**p030 の回帰の基準 = 85 本**。
- 2026-10-25 05:42 ユーザー「p027 の S6 が終わったら、P2 にこの分担で p030 の TU と BLT の系統を渡してよいです。」→ P1 = S3〜S8（RX・ROP・CP）、P2 = S9・S10・S12・S14（TU・BLT）、S13・S11 は後で Q1 が割り当てる。
- 2026-10-25 05:47 P1 → Q1: S3（RX の clip）af9f8bf（親 48e35a8）。atg_rx_cpoly（新、7 平面の Sutherland–Hodgman）、atg_rx_clip（kind、w ≥ 2^-16、near・far、guard band、line・point、DISCARD）、model と vector（旧 rx_clip.txt は不変）。trivial accept を |x|≤4w に（spec に記録）。P1 の申告: lint 112 clean、atg_rx_clip 13 行・atg_rx_cpoly 4 行・atg_rx_setup PASS、MUT_FARCLIP・MUT_WCLIP で FAIL、旧 clip と新 clip が乱数 6000 と scene の全三角形で一致。T1-763 に。
- 2026-10-25 06:00 Q1: T1-763（af9f8bf、S2・S3）PASS: sim-quick 84（83 + atg_rx_cpoly）、verilator 5、lint 112。
- 2026-10-25 06:03 P1 → Q1: S4（RX の setup）b01e9f1（親 af9f8bf）。atg_rx_edge（新、整数の段を移した、line の parallelogram・point の正方形、bbox、viewport の矩形）、atg_rx_setup（micro-op の追加、depth bias、PointCoord の plane、line の plane）、DEPTH_BIAS_*（0x060〜0x062）。P1 の申告: lint 113、atg_rx_setup 9 行 PASS（旧 160 三角形は不変）、MUT_LINE_MAJOR・MUT_BIAS_R で FAIL。T1-765 に。
