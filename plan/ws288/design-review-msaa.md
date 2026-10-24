<!-- awesome-plan project=zedbsd record=ws288-p027-design-review -->

# WS288 design §15 MSAA の review の指摘と対応

- 対象: [design.md](design.md) §15（版 5）と amaterrace-gpu の `docs/ghci.md` §3.2 CAPS[29]・§12.11（draft）。Phase: [p027](phase027/phase.md)、q1558-i01。
- review 1: 2026-10-25 design-reviewer（P2 が起動、読むだけ）。対象の版: zedBSD c58700b76、amaterrace-gpu 00a9a9c。18 件（Critical 0、High 2、Medium 8、Low 8）。reviewer は RTL・spec・ghci.md・ir.h/spirv.c と docs.vulkan.org（fragops・capabilities）を読んだ。確かめて問題無しとされた点: edge の単位と sample の offset、tie の規則（共有の edge で E1 + E2 = 0、bias は片方）、bbox の十分さ、MS_CTRL・SAMPLE_KILL の番号と SET_REG の待ち、lda の 0xFFx の捨て方、mask operand、行数、帯域の算術、LW の合計、a2c・alphaToOne・FragDepth・操作の順・sampleCounts の規則。

| # | 重さ | 指摘 | 対応（§） |
| --- | --- | --- | --- |
| 1 | High | RGB565 の 4x の attachment を TU が読めず resolve・sample できない | S5 で `texf` に 16 bit の RGB565 の texel を足す。`texl`/`texb` の 565 は §15.18 の 11。scene の (8) に 16 bpp の 4x（§15.1・§15.8・§15.9・§15.15・§15.17） |
| 2 | High | resolve の draw の時間を TU で見積もったが RX の frag（≈200 cycle/quad）が律速、≈12 倍小さい | 1080p ≈1.0 s・720p ≈0.46 s @ 100 MHz に直し、BLT の案（≈0.5 s）と同じ基準で U44 に並べた（§15.8・§15.20）。推しは A のまま（新しい RTL 無し、RX の pipeline 化で一緒に速くなる） |
| 3 | Medium | 帯域が ROP の line の粒度（depth の cache 無し、行ごとに 64 byte の読み書き）を見ていない、720p60 の目標に根拠が無い | 表を「理想」と「v1 の line の粒度」の 2 列に（depth は 1x も 4x も 64 byte/画素）、律速は RX の ≈2 M 画素/s と明記、U45 を「v1 は正しさだけ、数字は p015 の後」に（§15.12・§15.20） |
| 4 | Medium | `exp 0x105` の AND の重ね掛けは Vulkan の出力の変数の意味と違う | 最後の書き込みが勝つ。drain で r6 & 値（§15.2・§15.5・ghci §12.11） |
| 5 | Medium | smask の書き手が 2 つ（fl の snoop と ex_op）で 1 本の書き口と衝突 | `mr6`（fl だけ）と `m5`（ex_op だけ）と書いた bit の FF に分けた（§15.5・§15.12） |
| 6 | Medium | color の出力の無い（depth だけの）shader が ROP に届かない、r3 を DEPTH に出すと sample の z を失う | exported に 0x105 を含め、compiler は `exp ~0 → 0x105` を出す、r3 を 0x104 に出すのは禁止（§15.5・§15.11・§15.14・ghci §12.11）、scene の (12) |
| 7 | Medium | firmware の「予約の TU の slot」は 16 slot の limit と矛盾 | 予約しない。slot 0 などを STATE の copy B から読んで保存・復元（§15.8） |
| 8 | Medium | resolve の draw の欠け: tile の dst、region の offset・layer、BLT と ROP の順序、§2.4 の表の 122 が X | 一時の linear の image + BLT TILE、constant で offset、layer ごとの draw と `.3` 形、BLT ↔ ROP の `WAIT_IDLE` を p018 の規則に、§2.4.4 の 122 を R に（§15.8・§15.11・§2.4.4） |
| 9 | Medium | `texsz.s`（imm 2）は今の decode で fault、変える module が表に無い | `atg_su_dec_tabc`・`atg_su_tex`・`atg_su_tex_ret`・asm/dis/model・GHCI §12.6 を S5 に、CAPS[29] = 0 は fault（§15.5・§15.16・§15.17・ghci §12.11） |
| 10 | Medium | 試験が LINE_BYTES 32、16 bpp、奇数の幅、MS_CTRL の変更、SAMPLE_KILL × CENTROID、depth だけ、`texsz.s` を見ない。S1 の受け入れが検証できない | scene に (8)〜(13)、KC705 と ZYBO の 2 つの形、単体 TB は LINE_BYTES 32・64、S1 は spec §8 と §15.15 の一致（§15.6・§15.15・§15.17） |
| 11 | Low | `atg_su_exp` の port は fl_pw_* が要るので 35 → 39、`atg_rop_order` は 28 | 直した（§15.5・§15.6・§15.16） |
| 12 | Low | ord の FIFO を退けた理由（atg_rx の 40 port）は wiring の例外で誤り | 理由を「ROP の順序の口と FIFO の深さの論・back-pressure が要る」に（§15.19） |
| 13 | Low | d_s が「1 warp = 1 三角形」に縛られる、warp ごとに計算し直している | 三角形ごとに 1 回の計算、制約と将来の primitive ごとの形を明記（§15.4・§15.13） |
| 14 | Low | 他の節との矛盾（§2.2 の CAPS [26]〜[27]、§3.4.3 の r4/r5、§3.7 の texture の行、§8 の合計と依存の図、review の file が無い） | §2.2 に版 5 の注、§3.4.3 を x + 0.5 と r6〜r9 に、§3.7 に注、§8 の依存の図に p027 → {p010, p018, p019}、合計の注、この file を作った |
| 15 | Low | ghci §12.11 の「image word」の参照先が無い、CAPS[29] = 0 の r6 が未定義 | 「part F の TEX_IMAGE、merge 待ち」と明記、CAPS[29] = 0 は r6 未定義（compiler は 1）（ghci §12.11・§15.10） |
| 16 | Low | sample の評価の加算器の量、細い三角形で scan が 5 倍、ZYBO の bitstream 3 への影響 | `es = ec + O` の 3 本の加算器の形に、空の quad の短絡（保守的）、R33 に ZYBO の影響（§15.3・§15.12） |
| 17 | Low | `atg_rx_bary` の FSM に DZ が無い、`fq_scov` が SAMPLE_KILL の前か後か | DZ を足した、fq_scov は SAMPLE_KILL の後（§15.3・§15.4・§15.13） |
| 18 | Low | §15.18 に input attachment、D24S8 の depth だけの copy、16 bit の座標（w·S ≤ 65535）を足す | 足した（§15.1・§15.7・§15.18） |

reviewer が確かめていない点（残る）: `VkImageCreateInfo` の MS の VUID の文（design は記憶と明記）、P3 の part F の実物（texf の ConstOffset の符号化と rs2 の衝突。ConstOffset は word1 の imm_hi なので rs2 とは別の field の見込み、part F の merge の時に確かめる）。
