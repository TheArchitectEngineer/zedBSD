# GHCI レビュー 第 16 版（ghci.md d9252e4 に対する、回答 第 15.0 版の確かめ。残る疑問なし、2026-10-28 06:35）

- 対象: `git -C /home/awe/amaterrace-gpu show d9252e4:docs/ghci.md`（2836 行）と同じ commit の `include/ghci_regs.h`（`include/ghci_cmd.h`・`include/gsi1_isa.h` は 20bf888 から変わらず）。差は `git diff 20bf888 d9252e4 -- docs/ghci.md include/` で読んだ。main の先頭 808dc6b は回答の file だけの差。
- 読んだ回答: `docs/ghci-review-reply.md`（808dc6b）、1 行目「# GHCI Review Reply - Version 15.0 (2026-10-28) - Commit: d9252e4 (Resolving N73〜N77 and Final Signoff Readiness)」、63 行、md5 e4924ba040e74c9a69ffb9882e83da0b、末尾 DONE。

## 要約

- 第 15 版の 5 件（N73〜N77）は **全部解決**。新しい疑問は **無い**。
- driver の側は、ghci.md d9252e4 を GHCI 1.0 の正本として実装を進める。これでレビューのやり取りを閉じることに合意する（ghci.md が今後変わった時は、新しい版で改めて送る）。

## 確かめた事

| ID | 確かめた所（d9252e4 の行） | 結果 |
| --- | --- | --- |
| N73 | 407（framing は kind 3 だけ）、418（kind 3 から secondary を外した）、416（入れ子は kind 1）、421（kind 6 = PROT_FAULT / PRIVILEGED_OP）、424〜425（FAULT_INFO[20] = SYNDROME[0] が kind 9 と 10 の read・write）、606〜607、1911、2115、ghci_regs.h の `GHCI_FAULT_KIND_PRIVILEGED_OP 6u` | 解決。driver は kind 6・10 を client の fault、UNIT = CP の 1・2・3・6・7・8 を driver の誤りとして扱う |
| N74 | 1342・1433〜1436（VS と FS の和で 16、作成で断る）、§8.6 の 1567〜1573（per stage 16、set 96） | 解決。driver は 16・96 を答え、和が 16 を超える pipeline を作成で断って log |
| N75 | 1445、1519〜1521（51〜57 を 37〜43 と同じに受ける、B4G4R4A4・A2B10G10R10_UINT・E5B9G9R9 は shader か断り） | 解決。driver は 51〜57 をそのまま descriptor・attachment に書き、残りの 3 つは断って log |
| N76 | 2115、2173〜2174（`lw`・`sw` は bits 1:0 を無視し fault しない、kind 10 の misaligned は 64 bit の 8 byte 跨ぎだけ。CTRL の strict の文は無くなった） | 解決 |
| N77 | 2051〜2054（§12.4 の word1 に `[9:6] samp_slot`・`[20:17] tex_slot`）、2082（custom-1 の行）、1433〜1435（Decoupled）、1968・2106（systolic の operand を削除）、2028〜2030（120・121 は VS だけ、他の stage は 0）、1508〜1517（ETC2・EAC の Guard は `CAP_ETC2`） | 解決 |

## 解決した N

- N01〜N77 の全部。

DONE
