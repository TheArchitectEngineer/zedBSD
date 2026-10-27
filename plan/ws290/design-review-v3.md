# WS290 design.md 版 3（書きかけ）の敵対的レビュー

- 対象: [design.md](design.md) の版 3 で変えた所（commit f5fea4c2e、branch agent/p3-ws290）と [ghci-questions.md](ghci-questions.md)。design-reviewer、2026-10-27。
- 照合: amaterrace-gpu `docs/ghci.md` ce9725a（`git show`、2076 行。下の「ghci.md N 行」はこの版）、同 RTL・include・`config.mk.template`（ce9725a）、zedBSD の driver・libvulkan。amaterrace-gpu と zedBSD には書いていない。build・試験は無し。
- **状態: 未反映**（G01 の回答とユーザーの中断の指示で、版 3 は書きかけのまま止めた。再開の時に、修正後の ghci.md で各指摘が残るかを確かめてから反映する）。V20 だけは ghci-questions.md の行の番号の直しとして反映済み。
- Vulkan 1.0 の必須の値のうち repo の中で確かめていない物は「未確認」。

| ID | 重さ | 指摘 | 根拠 | 提案 |
| --- | --- | --- | --- | --- |
| V01 | Critical | block の無い discrete（UMA = 0）で DEVICE_LOCAL の memory type が無くなり、libvulkan が physical device を拒む。§24.3 の [0] と §5.6 の表の頭が食い違う | libvulkan `instance.c` 1121〜1127 行（DEVICE_LOCAL が無いと `VK_ERROR_INCOMPATIBLE_DRIVER`）、今の `vk-device.c` 692〜702 行は host の type に DEVICE_LOCAL を付ける、RTL `ATG_DEF_DEVICE_CAPS`（atg_cp_regs.v 149〜152 行）の bit 0 = 0、profile `minimal` は UMA = 0 | block の無い discrete は unified と同じ 1 heap（DEVICE_LOCAL・HOST_VISIBLE・HOST_COHERENT）。p007 に libvulkan の受け入れの条件の検査 |
| V02 | High | attach でわざと未知の opcode の fault を起こす（§5.1 7a）の副作用が未定: service thread が起きる前の RECOVER、fault の嵐・session の数えに入るか、reset の後の IRQ_ENABLE・CTRL の書き直し、0x34 が将来の indirect dispatch と重なる恐れ（推測） | §5.3・§5.4、ghci.md §7.2 551〜569 行、258 行 | 7a の間は FAULT を IRQ から外し poll、W1C、数えない、6 の段を書き直す。望ましくは capability の bit が立つ時だけ command を出す（G41 の推しに） |
| V03 | High | CAPS[30] から `independentBlend`・`depthBounds`・`occlusionQueryPrecise` を出すが PSO・command で書けない。G45 の推しと §24.4 が食い違う | ghci.md §8.2 735〜782 行、299・1622・1754 行、G23、今の `vk-device.c` 590〜608 行（1.1 の写し） | flag に依らず false。§24.4 と G45 を揃える |
| V04 | High | CAPS[29] の feature と sample の数: `sampleRateShading` は false であるべき、`alphaToOne` は PSO に field が無い、sample の数が §24.3（4）・G48（1）・§12（LIMITS）で割れる | ghci.md §12.11 1674〜1675・1686・1552 行 | sampleRateShading・alphaToOne は false、CAP_MSAA なら 4 と一か所で |
| V05 | High | CAPS[26] で imageCubeArray・shaderImageGatherExtended を出すのは ghci.md と逆。無い時の不足（2D array・3D・cube・compare・gather・LOD の問い合わせ）が §24.3・U8 に無い | ghci.md 1553〜1554・1479〜1482・1512・1526・1534 行 | 2 つの feature は常に false、無い時の断りと U8 に足す |
| V06 | High | §24.1 の「重なる bit は AND」が無条件で、block 無し（0）と AND すると全部の device が拒まれるか executor が出ない。HW_CURSOR の 0 が「無い」か「分からない」か区別できない | design §24.1・§24.3・§13 | AND は `sources & FROM_BLOCK` の時だけ。判定は全部 `sources` を見る。p002 に試験 |
| V07 | High | RTL の DEVICE_CAPS は実は CAPS の layout を返す（DEVICE_CAPS の parameter が 0 で CAPS が既定でなければ CAPS を返す）。G に無く、§19.2 の参考の文が誤り | atg_cp_regs.v 425〜426 行、atg_cp.v 220〜228・275〜277 行、sim/tb/atg_ghci_slim_tb.v 55・237〜239 行（0x00500002 を期待） | 新しい G（仮 G50）としてユーザーに示す。参考の文を直す。RTL の値を profile に |
| V08 | Medium | MEM の WINDOW_CTRL は書く register なので読むだけの block では代われない。DISP2 の無い discrete では boot framebuffer の範囲が分からない | ghci.md 1027・1011 行、atg_cp_regs.v 318〜319 行 | G43 の推しを「MEM の register（少なくとも WINDOW_CTRL）は要る」に。DISP2 の無い discrete は window を触らず local を ENOTSUP か範囲の外から |
| V09 | Medium | G41 に反対側の根拠が抜け、推しが弱い: Appendix A は CAPS・CAPS2 を 1.1 の側と書く、0x020・0x024 は 1.1 の offset、RTL の slim は 0x080・0x084 を読む。ユーザーの補足は変化の説明で register を禁じる決定ではない（原文と解釈を分ける、AGENTS.md §8）。直す所は 3 か所より多い | ghci.md 2064・214〜215 行、atg_cp_regs.v 365〜367・445〜446 行 | 両案を同じ重さで示し、直す所を数え直す |
| V10 | Medium | CAPS2 の layout が 3 通り（ghci.md・include・atg_cp.v の注）。1.1 の CAPS2 から取っていた limit（attribute・sampler・sampled image・bank_words）の出所が消える。L2 256 が 8 bit に入らない、LDS の値が config と食い違う | include/ghci_regs.h 270〜275 行、atg_cp.v 227〜228 行、`vk-device.c` 446〜447 行、ghci.md 309・313 行、config.mk.template 44〜45 行 | G47・G48 に足し、flag に依らない limit を ghci.md の定数から書く |
| V11 | Medium | Vulkan 1.0 の事実の誤り・抜け（未確認）: sample count の必須 1 と 4、D16_UNORM、sRGB の COLOR_ATTACHMENT・BLEND、R16G16B16A16_SFLOAT・整数の必須、BLIT_SRC・DST が無い・TRANSFER は 1.1、maxComputeWorkGroupInvocations 128（NWARPS 2 で 64）、maxImageDimensionCube・3D、vkCmdDispatchIndirect | Vulkan 1.0 の Required Limits・Required Format Support（repo の外） | 「Vulkan 1.0 の必須と各 flag が欠けた時の不足」の表を作り U8 をそこから |
| V12 | Medium | CMD_DISPATCH_INDIRECT の欠けが G に無い。vkCmdDispatchIndirect の扱いが無い | ghci.md 258 行、§7.2 551〜569 行 | G を足し、答えまで記録で断る |
| V13 | Medium | SFU が無い時に compile を失敗にすると、compiler は source に依らず RCP を出す（perspective の 1/w、cube の座標）のでほぼ全部失敗する。XU の exp2・log2・fract も対象 | `compiler/gsi1-io.c` 1140 行、`gsi1-texture.c` 614 行、ghci.md 1359 行、config.mk.template 40〜41 行 | 対象を XU の全部に。SFU = 0 の device では executor を出さない案 |
| V14 | Medium | U8 の「ghcictl の list に出す」に経路が無い（`GPU_GET_INFO` だけ、ioctl を足さない方針） | `userland/base/ghcictl/main.c` 195 行、design §14・U1 | 経路を U8 の判断に含めるか、log だけに |
| V15 | Medium | native の apiVersion は 1.1 のまま答える必要がある（libvulkan が 1.1 未満を拒む） | libvulkan `instance.c` 1068〜1070 行、`vk-device.c` の `DEVICE_API_VERSION` | §24.5 に書き試験に入れる |
| V16 | Medium | host 試験が危ない所を覆わない: block 無しの列・AND・libvulkan の受け入れ・Vulkan の必須を見ない、期待の差は同じ設計の表が元、PSO の byte と profile の照合が無い、7a の場面の抜け（古い RECOVER、opcode を飛ばす CP、壊れた block、uncached の view） | design §24.6・§15.3 | profile `rtl-slim`、受け入れと必須の検査を全 profile に、PSO と profile の照合 |
| V17 | Medium | 「block 無し」の既定が楽観（GRAPHICS・STENCIL・BILINEAR・SFU・TRILINEAR）と悲観（MSAA・CORE・TEX_EXT・LDS 0・MRT 1）で混ざる。今の全部の HW が block 無し | design §24.3、RTL の既定 | 各列に根拠を書き、どちらに倒すかをユーザーに確かめる |
| V18 | Medium | §24.4 の「常に」の format に根拠が無い。FMT が有っても読まない方針は ghci.md §4「A driver reports its format properties from these」に反する。sRGB の FILTER_LINEAR を TEX_FFILT で絞るのは合わない | ghci.md 1508〜1509・403〜404・1559 行 | FMT が有れば正とし、表は無い時だけ。「常に」は推しとし G24 に |
| V19 | Medium | G28 を閉じて FB_BASE・FB_NEXT・CURSOR_BASE・record の address の空間（bus か VA か）の問いが宙に浮いた | design §5.1 の 5・§13 | G49 に移す |
| V20 | Medium | ghci-questions.md の行の番号が 6 か所で design.md と違う（design.md が正） | ce9725a | **反映済み**（2026-10-27） |
| V21 | Low | 版 2 の register 前提の記述が残る: §2 の §13.2 の行、§4.1 の ghci-regs.h の「MEM・FMT・DISP2 の offset」、§15.2 の model の EXTCAP の窓、p010 の MEM、§12 の版 2 の段落、§4.4・§17 の出典の版、§11.6 の「最小の値」の曖昧さ | design の各節 | 直す |
| V22 | Low | ws.md・phase001 が版 2 のまま、U3 が「確認中」、ユーザーの補足の原文が ws.md に無い | ws.md、phase001/phase.md | 同じ commit で直す（中断の記録で一部反映） |
| V23 | Low（§0.1 の file の数は反映済み） | p008・p011 の LW が増えた場面のまま。§0.1 の「f718918..ce9725a は minimal_fit_report.md だけ」は誤り（3 file、tools/vivado-synth-l2.tcl を含む。docs/・include/ の差が無い結論は正しい）。読んだ物に ghci_regs.vh・atg_dc_pq_lut.vh が無い | `git diff --stat` | 直す |
| V24 | Low | 小さな抜け: ghci.md 411 行の FILTER_LINEAR は「bit 1」だが Vulkan では bit 12（未確認）、CAPS[2] が TO-BE に無い ISA_ID を指す（280 行）、§14 1991 行の「32 lanes」、include の GHCI_CTRL_RESET が 102 行と 279 行で違う値、RTL の slim は CTRL を 1.1 の index へ送る（推測） | ghci.md・include・RTL | G に足す |
| V25 | Low | `struct ghci_caps` に block の word 12〜15（window の bytes・vendor・device・clock）の field が無い。vendorID に FourCC を使う案は PCI の慣行と合わない | design §24.1・§5.1・§11.6 | field を足す、vendorID は PCI の ID |

件数: 25（Critical 1・High 6・Medium 13・Low 5）。
