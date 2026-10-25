<!-- awesome-plan project=zedbsd record=ws288-p034-design-review -->

# ws288-p034 の設計（design.md §17 session の保護）の review

親: [phase034](phase034/phase.md)、設計 [design.md §17](design.md)。

## 1 回目（design-reviewer、2026-10-25、33 件: Critical 2・High 5・Medium 16・Low 10）

対象は design.md §17 の初版（zedBSD 4a2181e63）。照らした先: amaterrace-gpu p1-p030b dc471f9、firmware p2-p018 2ce0fe6、driver p1-p030、P3 の vk（p3-p006）。reviewer は読むだけ。対応は全部 §17 の本文（版 7 の改訂）に入れた。

| # | 重大度 | 指摘（要約） | 対応（§17 の改訂） |
| --- | --- | --- | --- |
| 1 | Critical | driver の FENCE は frame の後に session の tag で ring に入る（`cp.c` 201〜203・242 行）。ring の µop を tag で検査すると毎回 fault。GHCI の「fence and result writes of its packets」とも食い違う | ring の class 0/1 の CP 側の µop（driver の FENCE・RESULT）は検査しない（§17.4 の表、§17.10 で GHCI の文を直す、U70）。driver は今のまま（§17.9） |
| 2 | Critical | firmware の内蔵の領域（identity buffer・内蔵の GSI1 program・wait word、`builtin.c`）を VF・I$・fence unit が使う。private（byte 0）だと全部拒まれる | 表の byte [7] public（全 protected の session が読む、U69）を identity buffer と内蔵の program に。firmware 自身の待ち（`fw_wait_units`）は UOP_SELF で検査しない（§17.3・§17.4・§17.6・§17.8） |
| 3 | High | heap の可視の chunk を 0 にせず再利用（scratch・bank の image の残り） | slab を取る時に DMA で 0 にしてから owner を書く（§17.7・§17.8）、TB と fwtest に case（§17.15） |
| 4 | High | aperture の base（line_bytes）が 4 KiB に揃わず、相対の granule が page を跨ぐ。firmware が APERTURE_BASE を知らない。PROT_INFO の >> 8 は 64 B 揃えの表で最大 192 byte ずれる | 絶対の granule の番号（address >> 12）、表は [G0, G1)、PROT_TABLE は 4 KiB 揃え、window に PROT_TBASE・G0・G1、driver は PROT の時 aperture の base を 4 KiB に（§17.3・§17.6・§17.9） |
| 5 | High | MEMORY の flags[1] は P3 が IMPORTED に使っている。IMPORTED の MEMORY は exporter の byte で失敗する。program は MEMORY にならない | READ_ONLY は flags[2]、IMPORTED は表を触らず ring の GRANT で access、program の allocation は driver の合成の identity の MEMORY READ_ONLY（§17.8・§17.9・§17.10）、R1 で P3 と合意 |
| 6 | High | RAM の中身は CTRL.RESET で消えないのに dirty が 0 に戻り、reset の後の最初の session が scrub されない | `resid` の reset の値を MIXED に（§17.6・§17.12・§17.13-3）、TB に case |
| 7 | High | slot の再利用が byte の片付けの正確さだけに頼る。object の作成の失敗で byte が残る | object を作ってから byte を書く、SESSION_CLOSE で表の全体から slot を掃く、slot は round-robin（§17.3・§17.8） |
| 8 | Medium | packet の外で書いた µop は次の packet（別の session）で流れる | FIFO の entry ごとに slot（書いた時の class 2 の slot、packet の外は 0 = firmware 自身）と SELF を持つ。class 2 の slot は PKT_INFO で取った時に latch（§17.6） |
| 9 | Medium | scrub の律速は lda の primitive table（2 × 6144 word）で graphics の session も毎回。性能の数字が誤り | 表を直し 6.2k cycle・≈0.9%（200 MHz）に、resid で A → driver → A は scrub しない（§17.7・§17.11・U63） |
| 10 | Medium | DRAIN の待ちを新しい µop の watchdog が数え、帰属を誤る | DRAIN の間は前の slot の仕事として数える（§17.6・§17.13-14） |
| 11 | Medium | PLB の miss の間 DC が止まる（最悪 2 回分） | miss の要求を park に退かし、chk = 0 と別の client の hit を通す（§17.5・§17.11）、TB で DC の遅れを測る |
| 12 | Medium | hit の判定が 1 cycle に収まる根拠が無い | 判定は 1 段の pipeline（chk = 1 に +1 cycle）、R2 に timing の実測（§17.5・§17.17） |
| 13 | Medium | PROT_INV の全部の flush で miss が増え、walk の捨て直しで livelock し得る | 範囲の PROT_INV、捨て直しは 1 度まで（§17.5）、大きな allocation の RMW の時間を §17.11 に |
| 14 | Medium | DRAIN の「unit の client の要求が応答済み」の信号が無く、`axi_idle` は DC で deadlock。leaf で待つ要求の header は新しい slot で作り直される | DRAIN に `atg_top` の unit の client の `c_req_valid` の OR を足す、確認 C1 は「応答まで」（覆わなければ outstanding の counter）（§17.6） |
| 15 | Medium | GRANT の drain は安全に効かず費用だけ | U65 の推しを「GRANT は drain しない」に（§17.8・§17.19） |
| 16 | Medium | 同じ範囲の 2 つの grant の REVOKE が両方消す | REVOKE は 1 entry だけ（§17.3・§17.8・§17.10）、TB に case |
| 17 | Medium | 可視の区分を granule ごとにすると 16 MiB の共有の heap が尽きる | session ごとの slab を line 単位で貸す、上限は heap limit（§17.8） |
| 18 | Medium | robustBufferAccess の compiler の漏れが device 全体の fault に。§17.13-4 の「自分の仕事だけ」は矛盾 | §17.1・§17.13-1 に書き、p010 の robust を R5 の試験に（U72）、-4 の文を直した |
| 19 | Medium | module の行数・port の計画が成り立たない（`atg_mc` 366 行、`atg_cp_seq` 374 行・40 port、`atg_cpu` 40 port、window の階層の配線） | root の grant を `atg_mc_prot` へ移す、seq の pre は `atg_cp_seq_wait` に・slot の計算は `atg_cp_prot` に（seq の port を増やさない）、window は sub-bus の先の `atg_cp_prot`、CAM の gw_* の配線を表に、port は group で数える（§17.16） |
| 20 | Medium | 試験の計画の穴（RESET の後の手順、FENCE の tag、public・slab・scrub・reset・aperture・重複の case、MUT の不足） | §17.15 に全部足した（MUT 13 種） |
| 21 | Medium | GHCI の draft の直しが足りない | §17.10 に足した（fence と result、MEMORY の失敗、表無し、IMPORTED、tag 0、SLOT_MAP[0]） |
| 22 | Medium | chk = 0 の firmware の DMA が client の address に向くと表まで書き換え得る | DMA の呼び出しの chk の表を R6 の受け入れに、表への write は tbl（DMA_GO[18]）の時だけ（§17.3・§17.8） |
| 23 | Medium | aperture を変えると表の大きさが合わない | PROT_TABLE を書いた時に G0・G1 を latch、aperture を書くと PROT_TABLE = 0、範囲の外は拒む（§17.3） |
| 24 | Low | PROT_CAPS[7:0] = 63 と firmware の FW_SESSIONS 32 の食い違い | PROT_SLOTS = 31（U62） |
| 25 | Low | PLB の tag は 14 bit、LUTRAM の量が多い | tag 14 bit、§17.14 を 1.2k LUT に |
| 26 | Low | STATE を消さない理由「firmware の STATE の cache」が確かめられない | firmware は session の変わり目で STATE の cache を捨てる、記録の自己完結を確認 C2（§17.7・U66） |
| 27 | Low | occlusion の counter は 1 つ | 切り替えで clear（§17.6・§17.7） |
| 28 | Low | `am_amb_mux` は VF・SU にも有る | ROP・VF・SU の mux は 0 を繋ぐ（§17.4） |
| 29 | Low | host の KICK は `atg_cp_prot` を通らない | KICK は slot 0 の µop として同じ道（§17.6） |
| 30 | Low | chk = 0 の fault の tag を SLOT_TAG[prot_unit] にすると unprotected で 0 になる | prot_unit = 0 なら今の tag のまま（§17.6） |
| 31 | Low | KC705 は aperture = local memory。表の 0 埋めの手順、walk の read の axi_idle | RUN の後に BLT（tag 0）で 0、その FENCE の後に PROTECTED を開く。walk は axi_idle と quiesce に数える（§17.3・§17.5・§17.9） |
| 32 | Low | 段の R1 と危険の R1 の名前の重複、R6 の LW が過小、MEMORY の flag の合意 | 危険は「確認 C1・C2」に、R6 13 LW（計 51）、R1 に P3 との合意（§17.17） |
| 33 | Low | U に足す判断 | U69（public）・U70（ring の CP 側）・U71（CAM の数）・U72（robust）を足し、slab は §17.8 で決めた |
