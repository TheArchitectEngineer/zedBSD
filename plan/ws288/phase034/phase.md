<!-- awesome-plan project=zedbsd record=ws288-p034 -->

# ws288-p034 session の保護（GHCI §11.6、PROT capability）

- 親: [WS288](../ws.md)。仕様: amaterrace-gpu `docs/ghci.md` §11.6（PROT、`BUFFER_GRANT` 0x10106・`BUFFER_REVOKE` 0x10107、`PROT_CAPS`）。
- 状態: in-progress（2026-10-25 10:34 Q1: 設計の段は review 済み（design §17 版 7、review 2 回 33・21 件）。U61〜U72 は推しで決めた。実装の段 R1〜R8 = 54 LW は次に空いた担当に）
- 依存: p018（firmware の session・heap・object の表）、p006（driver、cleared）、p019（vk/）。RTL の MC の変更は p030 の後の main の上で。
- 承認: 2026-10-25 08:57 ユーザー「了解です。せっかくなので実装してしまってください。」（Q1 が buffer の context ID（GHCI §11.6 の session の保護、PROT）は未実装と答えた後）

## 範囲

1. **設計の段**（design.md に節、design-reviewer で review）: memory の要求に session の番号を付ける道（CP・VF・SU（LSU・TU・LDS の外）・RX・ROP・BLT・DC から MC まで、AXI の user の bit か MC の client ごとの session の register）、session ごとの範囲の表（owned と granted、granule 4 KiB、PROT_CAPS の数）、MC の検査（範囲の外は書かない・0 を読む・fault の kind と address を報告）、TU の cache・ROP の cache の扱い（session が変わる時の invalidate）、firmware の `SESSION_OPEN` の保護の flag・`MEMORY`・`BUFFER_GRANT`・`BUFFER_REVOKE` と表の更新の順（ring の順で後の work に効く）、driver の保護した session（dma-buf の import・share を grant に）、性能（範囲の表の検索の cycle）。
2. **実装**: RTL（MC の検査・表、各 block の session の番号の配線）、firmware、driver、GHCI §11.6 を draft から確定、model と TB（違反で fault、grant の後に読める、revoke の後に読めない、別の session の memory を読めない）。

## 受け入れ

TB と host の試験が PASS、`make lint` clean、`make sim` の PASS が減らない（T に回す）、driver の vmunix の warning 0。実機は p028 の後。

## 記録

- 2026-10-25 08:57 Q1: 新設（ユーザーの指示）。
- 2026-10-25 10:33 P1（q1570-i01）設計の段: zedBSD agent/p1-p030 に design.md §17（版 7、session の保護）と [design-review-p034.md](../design-review-p034.md)。決めた事: granule（4 KiB、絶対の番号）ごとに 1 byte の所有の表（driver が貸す PROT_TABLE、[5:0] owner の slot、[6] read-only、[7] public）、MC の root の PLB（16 line、判定は 1 段の pipeline、miss は park と walk）と grant の CAM（64）、AMB の header に chk・tbl・slot、unit は 1 度に 1 つの session（CP の `atg_cp_prot` が drain → invalidate → 他の session の残りの時だけ SU の scrub（RF・LDS・lda・PT・cbank・export buffer））、firmware の µop と DMA は class 2 の session で検査（firmware 自身の待ちは UOP_SELF）、heap は private・session の slab（0 埋め）・public、DC は root の別の入口、ring の class 0/1 の CP 側の µop（driver の FENCE）は検査しない。U61〜U72（推しつき）、段 R1〜R8・54 LW。design-reviewer 2 回（33 件: Critical 2・High 5・Medium 16・Low 10、21 件: Critical 1・High 3・Medium 6・Low 11）の全部を本文に反映した（件数は reviewer の報告の値）。RTL・firmware・driver は書いていない。未確認（R1 で）: 各 unit の busy が MC の応答まで覆うか（確認 C1）、記録が STATE を自己完結に書くか（確認 C2）、DC の FIFO の余裕、P3 の `share.c` の import の寿命。
- 2026-10-25 10:34 Q1: P1 の設計（zedBSD agent/p1-p030 92b5c3e66: design §17・design-review-p034.md）を main に merge。U61〜U72 はユーザーの「ゴールまで推しで進んでよいです。」に従い推しで決めた（記録は q1-decisions）。設計の段は cleared の相当、Phase は実装の段 R1〜R8 の後に clear。
- 2026-10-25 12:04 P1 → Q1: R1（amaterrace p1-p034 c630c3c: GHCI §11.6・§3.4[9]・§7.2 MEMORY の flags・§5 の reset、PROT_TABLE などの define、tools/protmodel.py --selftest PASS）。未確認の確かめ: C1 = TU の busy は自分の MC の要求・応答を覆わない（CPU の DMA の書き込みも）→ MC に client ごとの outstanding の counter（unit_idle）、C2 = 記録は自己完結（U66 のまま）、DC は KC705 で 100 MHz（design の 200 MHz は誤り、scrub 約 62 µs）、pixel の FIFO 2048 で約 2,760 cycle、share.c は GRANT・PROTECTED を使っていない。Q1 の判断: unit_idle は保護の DRAIN だけでなく CP の WAIT_IDLE の条件にも入れる（PROT に依らない正しさの直し）。
- 2026-10-25 12:31 P1 → Q1: R1・R2（amaterrace p1-p034 0c9982b、zedBSD 5ec5c954e）。R2 = MC の保護（atg_mc_prot・atg_mc_plb・atg_mc_grant を新設、atg_mc を atg_mc_asm・atg_mc_cnt に分けて 395 行、leaf の header に chk・tbl・slot、HAS_PROT の時 DC は root の別の入口、拒んだ read は 0）。unit_idle は既定の形でも CP の FENCE・WAIT_IDLE・RESULT・QUERY の待ちに（atg_cp_seq_wait の bit 0）、TB atg_cp_seq.mem（MUT_SEQ_NOMEM で FAIL）。P1 の申告: lint 132、atg_mc・atg_mc_prot（9 group）・atg_rt・atg_cp_seq・atg_top・atg_top_fw PASS。T1-787（T2）。次は R3（atg_cp_prot を atg_cpu の子に）。
