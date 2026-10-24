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

## review 2（2026-10-25 design-reviewer、対象 zedBSD 8b6cfd2b1・amaterrace-gpu e82c8a0、P3 の part F の実物 p3-p009f 3ec9aed も読んだ）

review 1 の対応の確認: 直った 8（2・3・4・5・11・12・16・17）、18 は幅の上限の 1 の差、不十分・一部 9（1・6・7・8・9・10・13・14・15）→ 下の新しい指摘で直した。新しい指摘 18 件（Critical 0、High 2、Medium 6、Low 10）。

| # | 重さ | 指摘 | 対応（§） |
| --- | --- | --- | --- |
| 1 | High | `texsz.s` = imm 2 は part F の `texsz.d`（3ec9aed の `atg_su_dec_tabc`・ghci）と衝突 | **imm 3** に。`atg_tu_addr` の `.l` の判定（`opr[6]`）と分ける、S は `ta_value`、`atg_su_tex_ret` は変えない（P2 が p3-p009f の ghci.md 997・1119 行で imm 2 = `texsz.d` を確かめた）（§15.2・§15.5・§15.9・§15.16・ghci §12.11） |
| 2 | High | resolve の STATE の保存の一覧が足りない、constant bank は CPU から読めず戻せない | 保存・復元をやめ、firmware の写しの state を dirty にして次の draw で出し直す。resolve は VF・RX・ROP・program・slot 0・constant を全部書く（§15.8・§15.11） |
| 3 | Medium | scene の ZYBO の形に TU が無い、KC705 だけとの矛盾 | ZYBO の形は `atg_top_tex_tb` と同じ HAS_TU(1)（§15.15） |
| 4 | Medium | RGB565 の `texf` の module（cache・filter）と GHCI が足りない | `atg_tu_cache`（half の選択・border）、`atg_tu_filter`（c/31・c/63 の RNE）、format 4、§15.1 の sampled の行、ghci §12.11、S5 を 5 LW に（§15.1・§15.9・§15.16・§15.17） |
| 5 | Medium | fragment shader の無い pipeline と、exp を通らない lane が ROP に届かない | compiler は全 program の終わりに入口の EXEC で `movi rT, -1; exp rT, 0x105`、fragment の stage の無い pipeline は firmware の内蔵の 2 命令（§15.5・§15.11・ghci §12.11） |
| 6 | Low | `exp ~0` を作る operand が無い（`mone` は −1.0f） | `movi rT, -1` を使い、`mone` の禁止を注記（同上） |
| 7 | Medium | d_s の計算の回数が本文で食い違う、1x で前の warp の d_s が残る | `dz_done`（`t_load` で消す）で三角形ごとに 1 回、4 word は warp ごとに送る、exp は slot の解放と reset で 0（§15.4・§15.5・§15.13） |
| 8 | Medium | 1x の vector を作り直すと r0〜r5 の誤りが期待値に入る | `vec-diff.py` で 87457fa の vector と比べ差が r6 と新しい列だけ（§15.15・§15.17） |
| 9 | Medium | 受け入れを機械的に確かめられない（PASS の名前、port の数、基準の commit、故意の誤りの手順、打ち切り、selftest） | PASS の行の表、spec の port の行数と「N ports」の一致、S1 の開始の commit の PASS の数、`ifdef MUT_*`、`timeout 600`、`msmodel.py --selftest`（§15.15・§15.17） |
| 10 | Low | 幅の上限 16383 は roundup で 65536 になる | w ≤ 16382（§15.7・§15.11） |
| 11 | Low | 画面を覆う三角形 1 つは guard band を超え得る | region の矩形を 2 つの三角形で（§15.8） |
| 12 | Low | tile の dst の一時の linear の image が heap（8 MiB）に入らない | 32 行の帯（240 KiB）ずつ resolve → TILE（§15.8） |
| 13 | Low | 依存の図の辺と ws.md、「p010 の MS の部分」の範囲 | §8 の図に p009（part F）→ p027 → {p010, p018, p019} と p010 が依る出力を書いた。**ws.md の p010・p018・p019 の依存の列は P2 の書ける範囲の外なので Q1 に依頼** |
| 14 | Low | ghci §12.9・§12.6 と §12.11 の食い違い、r6 の値、MS_CTRL の予約の言い方、標準の文書に WS の ID | 相互の参照の注、「the sample mask the rasterizer wrote」、design も「0（1 sample）として読む」に揃えた、WS の ID を外した（ghci §12.11・§15.9・§15.10・§15.14） |
| 15 | Low | design §3.2 の `mask` が「v1 は 0xFFFFFFFF」、noperspective の参照 | §3.2 を RTL の coverage に、参照を「§3.4.3 と同じ式（§3.8）」に |
| 16 | Low | 並行の段の衝突（GSI1 の道具、MS4 の parameter） | 道具は S1 が全部を先に直す、MS4 は S5 が通す（§15.17） |
| 17 | Low | 4x の FragCoord.z が [0, 1] の外になり得る | compiler が 4x の pipeline で `sat` を付ける（§15.4） |
| 18 | Low | firmware の resolve の手順がどこでも試験されない | p018 の受け入れに入れる項目を書いた（§15.11・§15.15） |

review 2 の後の残り（未確認）: Vulkan の「fragment の stage の無い pipeline」「bound の state が resolve を越えて残る」「4x の FragCoord.z の範囲」は reviewer も P2 も文面を取得していない（記憶・推測と明記）。3 回目の review はしない（Q1 の判断に任せる）。
