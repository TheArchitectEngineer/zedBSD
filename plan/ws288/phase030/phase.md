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

設計の段: design.md に節（review 済み）、実装の段の分け方と LW。実装の段: 各項目の model・単体 TB・scene が一致、`make lint` clean、`make sim` の PASS が減らない、`make verilator` PASS。板・timing は対象外。

## 記録

- 2026-10-25 02:32 Q1: 新設（U46 のユーザーの決定）。設計の段を P1（kernel-and-driver-designer）に。
- 2026-10-25 P1: 設計の草稿を design.md 版 6 の §16 に（Vulkan 1.0 core の limit・feature・必須の format の表、format の土台、ROP・RX・VF・TU・BLT・SU・CP の設計、GHCI の draft（amaterrace-gpu p1-p030 の `docs/ghci.md` §8.5・§12.12・CAPS[30]・FMT・µop 14）、段 S1〜S11 = 68 LW、U47〜U57）。design-reviewer の review はこの後。
