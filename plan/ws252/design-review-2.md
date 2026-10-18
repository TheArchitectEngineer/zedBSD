<!-- design-reviewer の再レビュー（2026-10-19、design.md 版 2、main c4345c4fc に対して）。Q1 が報告の本文をそのまま置いた -->

# WS252 design.md（版 2）の敵対的な再レビュー

対象は /home/awe/zedBSD-claude1/plan/ws252/design.md（版 2）です。main（c4345c4fc、clean）の `src/drivers/gpu/i915/` と、Linux 6.8.12（/tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/lx/linux-6.8.12）を読んで照らしました。source・plan の変更、commit、build、QEMU、rm はしていません。`.internal/` も読んでいません。

**結論**: B1 は直りました。M1・M2・M6 と m1〜m6・m8 も直っています。ただし M3・M4・M5・m7 は直し方が足りず、版 2 で新しい矛盾も入りました。Major は 4 件です（下の N1〜N4）。

## Blocker

ありません。

## Major

### N1. D1=A（既定）のままでは、実機ができても RKL・ADL-S の GT を動かせない。ws.md の受け入れと p007 の依存が成り立たない
- **根拠**:
  - ws.md の受け入れ「D1=A（既定）: RKL・ADL-S/RPL-S は efifb のまま。実機ができたら p002・p007 で、実機の上で GT の初期化・Vulkan の三角形・blit・video が動き…」。
  - attach は `i915.c:76-88` の `identifiers[]` で決まり、ここに無い ID には i915 が付きません。
  - A では ID を足さないので、GT は実機の上でも一度も走りません。
  - 同じ理由で、§5 の p007 の依存「p003 または p006」も満たせません。A では p003 を行わず、D2 が既定なら p006 も行いません。
- **何が起こるか**: 既定の組み合わせ（D1=A・D2=Future Work）を選ぶと、WS の受け入れ条件に到達する道がありません。
- **修正案**: 次のどちらかにします。
  - (a) A の意味を「既定の image では付けない」に変え、試験の config.mk（2026-10-04 の「config.mk での build＋個別ファイル」の規則）で ID を足す build option を p003 に入れる。この案なら起動の語も増えません。
  - (b) 「A の時は GT の受け入れが無い（WS は RKL の部分を Future Work へ）」と ws.md に書く。
  - どちらの場合も、p007 の依存を D の結果ごとに書き直します。

### N2. §6.2 の ICL の一覧が、まだ「9 か、それ以外は 12」の分岐の大半を落としている（M4 が直り切っていない）
- **載っている行は正しい**: 載っている zedBSD の行は全部確かめました（`submit.c:525`、`context.c:392`、`request.c:379`、`ggtt.c:257`、`tlb.c:174`、`reset.c:388`、`gt-power.c:178`、`device-info.c:245`、`device.c:1316,1347,1384,1965,2295,2413,2500`、`worker.c:360`、`render/vulkan.c:107`、`render/instance.c:307`、`render/batch.c:124,162`、`render/blit.c:106-108`、`workarounds.c:865-880`）。
- **抜けている行**: grep で見ると、次が一覧にありません。
  - **WA の list の行が丸ごと無い**: `workarounds.c:979-995`（GT・engine・ctx の list は「9 でなければ Gen12 の list」なので、ICL に Gen12 の WA が当たる）、`workarounds.c:496`（whitelist は版 12 だけ。Linux には `icl_whitelist_build` がある）、`workarounds.c:749`（版 12 未満は MOCS の表を作らずに戻る）、`workarounds.c:470`（copy engine の MOCS は版 12 以上だけ）、`workarounds.c:1009,1033,1047`。§2 の要点 3 は「ICL の WA」を挙げていますが、§6.2 に行がありません。
  - **compiler**: `compiler/compile.c:750`（版 9・12 以外は ENOTSUP）、`compile.c:3382,4967,4977`、`compiler/compile-compute.inc:395,719,780`。p005 に `eu.c` しか載っていません。
  - **render**: `render/state.c`（667・904・922・975・1248・1342・1435・1487・1620・1692・1824・1926・2581・3916）、`render/draw.c:1076,1132`、`render/compute.c:269,383`、`render/pipeline-prepare.c:804`、`render/blit.c:989`。
  - **そのほか**: `verify-workarounds.c:755`、`engine.c:168,217`、`defaults.c:326`、`irq.c:714`、`mmio-record.c:109`、`i915.c:277,407`。この中には ICL でも Gen12 側に入って正しい物があります。例えば `engine.c:217` は「graphics 11 から 12.70 未満」の WA です。`defaults.c:326` も正しい側で、Linux の null render state は版 6〜9 だけです（Linux の該当の関数を読んだ覚えからの推測で、今回は開いていません）。そういう物も「確かめて変えない」と書くべきです。
  - **display 側で GT の版を見る所**: `display/survey.c:705`（`!= 12U`）、`display/display.c:1190`、`display/bootfb.c:220`。WS245 の範囲ですが、p006 の ICL の attach で通る所です。
  - `device.c:1316` の行は、中身が forcewake の範囲の選び（Gen11 は Linux の `__gen11_fw_ranges`）である事が書かれていません。
- **何が起こるか**: p004・p005 の範囲と LW を少なく見積もります。ICL の WA の list の誤りは実機でしか分からない種類で、計画の上で落ちたままになります。
- **修正案**: grep の全部の行を「直す／Gen12 の道で正しい（理由）」に分けて §6.2 に載せます。WA の list（gt・engine・ctx・whitelist・MOCS の build）の行を p004 に足します。

### N3. §6.3 の見積もりの根拠の引用が誤り。WS229 の p001 の ICL の見積もりとも突き合わせていない
- **根拠**:
  - §6.3 は「WS229（Gen9 の GT・EU・state）は見積もり 14.5 LW」と書いています。しかし `plan/ws229/phase001/phase.md:42` では、14.5 LW は display controller の 6 LW（p006）と規約の見直しの 0.5 を含みます。GT・EU・state・Vulkan の通しは **8 LW** です（p002 2.5、p003 2.0、p004 2.0、p005 1.5）。
  - 同じ file の 231 行は、ICL の GT を「別の仕事（2〜3 LW）」と見積もっています。その中身は「EU は Gen9 の符号化（SWSB 無し）だが message は Gen11 の形、genxml は gen110（Gen12 と近い）」です。
  - 今の Gen9 の encoder は Align1 だけで、3 source を持ちません（`compiler/eu.c:1366`、`intel/eu-encoding-gen9.h:11`）。よって Gen11 の encoding は Gen9 の物にかなり近い、と推測します。
- **何が起こるか**: design は「Gen9 と同じ量＋α、一部流用できる」と書きながら、p004〜p006 を 22 LW としています。これは Gen9 の 8 LW の約 2.75 倍です。根拠の数字と結論が合いません。D2 でユーザーに示す「約 25 LW」の信頼性もここで決まります。
- **修正案**: 引用を「GT 系 8 LW（display を除く）」に直します。WS229 p001 の ICL の 2〜3 LW との差の理由（版 2 の §6.2 の項目の数など）を書くか、見積もりを出し直します。WS229 の実績（commit の数、p005 の実機の回数）を記録から取れるなら添えます。

### N4. ICL の attach の判断がどの D にも無いのに、p006 が「attach の gate」を含む
- **根拠**:
  - §8 D1 の問いは「RKL・ADL-S/RPL-S に i915 を付けるか」だけです。
  - WS245 は ICL の attach を「WS252 の後の別の Phase（この WS の外）」としています（`plan/ws245/design.md:920` の §7 #30、413 行）。
  - それなのに §5 の p006 には「attach の gate・ICL の DMC」が入っています。
- **何が起こるか**: D2=全部の時、ICL の ID を足す方法（opt-in か既定か、起動の語か config か）がユーザーの判断無しに p006 で決まります。2026-10-18 に起動の語を減らした決定（`plan/master.md:655`）と同じ論点が、ICL では聞かれないまま残ります。
- **修正案**: D1 を「RKL・ADL-S/RPL-S・ICL」に広げるか、D2 の「全部」の中に attach の A/B/C を含めます。p006 の依存に D1（または D2 の attach）を書きます。

## Minor

- **m-a. M3 の「host には照合の土台が無い」は事実と違う**:
  - §4-1 は「WA・MOCS の照合は実機の上の kernel 内の試験だけ」と書きます。しかし `plan/ws229/tests/gen12-golden/run.sh`（golden.c・stubs.c）は、Gen12 の GT の初期化の全部の register の書き込み、WA・ctx・whitelist の表、context の image を host で記録し、基準の commit と比べる物です（`plan/ws229/phase002/phase.md:41` で 543 行の PASS）。
  - Master の Tools には載っておらず、WS244 の書き直しの後に今も build できるかは確かめていません。
  - 新しく作る前に、これを直して使うか（使うなら master の試験の一覧に登録）、使えない理由を書きます。2 LW の見積もりも変わります。
  - §4-4 の compiler の照合も、PRM の bit の列より強い物が手元にあります。WS229 で使った Mesa の `brw_asm`・`brw_disasm`（`--gen=icl`）と `gen110.xml` の decode です（/home/awe/zedBSD-worktrees/p1/build/mesa-tools/mesa-25.0.7 に `gen110.xml` がある事を確かめた）。
- **m-b. PCI ID の表は 3 つある（M5 の残り）**: §6.1 は「xd と `device.c` の 2 つを 1 つに」と書きます。しかし attach の一致の表 `i915.c:76-88` の `identifiers[]` も別の列です。p003 の作業にこの表の変更が無く、`device.c` だけを足しても attach しません。
- **m-c. D6（と D5 の一部）は、決定済みの事を聞き直している**:
  - 2026-10-19 の「設計まで（実機が無いので p001 で止める）」（`plan/master.md:640`）が、「実機の前に p002 を入れるか」に既に答えています。
  - 聞き直すなら「Q1 が入れたい理由がある時だけ」とし、朝のクリックの一覧からは外すのが筋です。
  - D5 の既定「無いまま」は、この決定の解釈です（決定は入手について何も言っていない）。そう書き分けます。
- **m-d. §2.1 と D4 の範囲の食い違い**:
  - §2.1 の題は「この WS の範囲の外、Q1 へ報告」です。それなのに D4 は WS252 で聞き、p002 が実装します。どちらかに揃えます（entry 63 は TGL・ADL-P の今の道の話なので、別の Bug・WS が自然です）。
  - ws.md の D4「合わせる」の受け入れは「5320 の回帰」だけです。entry 63 は ADL-P にも効くので、T1 の 5330 も要ります。
- **m-e. §3 の案 B の言い方**: 「device start の前の probe」とありますが、今の gen9 の gate は `i915_attach` の中です（`i915.c:270-284`、driver の probe の欄は NULL）。「attach の初め」と書くのが正確です。
- **m-f. 写しの食い違い（記録の整合）**:
  - phase001/phase.md の「結果（要点）」が版 1 のままです（「DMA の幅」「約 23 LW」「D1〜D5」「試験は host の表の比較」）。
  - `plan/master.md:21` の朝の判断は D1〜D5 だけで D6 が無く、577 行は「p001 設計の担当の割り当て待ち」のままです。
  - ws.md の Phase の表は p001 だけで、p002〜p008 の planned の行がありません。
  - master は Q1 の持ち物なので、Q1 への依頼として扱います。
- **m-g. LW の内訳の前提**: §5 の「RKL・ADL-S だけで約 13 LW」は p003 を含みます。p003 は D1 が B・C の時だけなので、既定の D1=A では 11 LW です（N1 を直せば変わります）。§5 と §6.3 の足し算（13、22、25、35）は合っています。

## 指摘ごとの確認（版 1 → 版 2）

| 指摘 | 判定 | 根拠 |
| --- | --- | --- |
| B1 | 直った | §3 の A「今は何も入れない」、§5 冒頭と ws.md「p002 以降は実機まで planned」、A' に T1 の 5330 と 5320 の一覧。D6 の聞き直しは m-c |
| M1 | 直った | Linux `i915_pci.c:419-426`（GEN8 の `dma_mask_size = 39`）、612・640 で継承。zedBSD `device.c:59-60`、`memory.h:87` の注記は p002 で直す。phase.md の写しは古いまま（m-f） |
| M2 | 直った | `intel/mocs.h:118-119`（UC 3・unused 2）、Linux `intel_mocs.c:530-531`（ABI の注記）、259-261（entry 63 は L3_1_UC）、`mocs-table.inc:12-13`。index 3 は `GEN11_MOCS_ENTRIES`（171 行）で両方の表に共通。TGL の表の unused は `I915_MOCS_PTE`（497 行）で、これも §2.1 の「意図した差」に入る |
| M3 | 一部 | QEMU を証拠から外した事、機械的な抽出、golden の「意図した差」は良い。既存の gen12-golden と Mesa の道具の見落とし（m-a） |
| M4 | 一部 | 載せた行と、`*` の付いた Linux の行（`intel_execlists_submission.c:1911-1915`、`intel_mocs.c:625`、`intel_gt_regs.h:1257`、`i915_pci.c:645`）は正しい。`*` の無い `intel_lrc.c:682-718,800-825`、`intel_execlists_submission.c:3456-3459,3526-3531` も確かめて合っていた。一覧の漏れ（N2）と LW の根拠（N3） |
| M5 | 一部 | field で持つ方針、WS245 `design.md:412` の引用は正しい。3 つ目の表（m-b） |
| M6 | 直った | `device.c:424-441` の順、`Makefile:54-71` の DMC は ADL-P と TGL だけ、`i915.c:271-283`、`master.md:655` |
| m1 | 直った | Linux `intel_workarounds.c:2414`（RKL・TGL・ADL-P）、zedBSD `workarounds.c:166` |
| m2 | 直った | `device.c:54,1383-1385`、`device-info.c:273` |
| m3 | 直った | `display/resident.c:1864-1866` |
| m4 | 直った | D4、p003 は B・C の時だけ |
| m5 | 直った | `gt/uc/intel_uc.c:30-56` |
| m6 | 直った | `gt->is_alderlake_p` を読む所は無い（grep。書くのは `device.c:1171` だけ）。display は `dmc-shell.c:110-112` の別の旗 |
| m7 | 一部 | ws.md に D ごとの条件はある。ただし D1=A の条件が成り立たない（N1） |
| m8 | 直った | `i915_pci.c:626-638`（EHL・JSL、ppgtt 36） |

## 確かめて問題が無かった事

- §2 の表と Linux の対応は、全部の列で合っています。
  - engine の mask（`i915_pci.c:619-698`）
  - ICL の ELSQ（GEN11_FEATURES）と CSB の形（`gen8_csb_parse`）
  - context の大きさ（`intel_engine_cs.c:288-290`、Gen11 と Gen12 は同じ）
  - uC の既定
  - RKL・ADL-S の WA は Wa_1607297627 だけが違う（`intel_workarounds.c:2371-2425`、gt の list 1483 以降に platform の差は無い）
  - TLB の Wa_2207587034 は RKL・ADL-S も対象（`intel_tlb.c:85-92`）
  - GT の code の中で IS_ROCKETLAKE・IS_ALDERLAKE_S を見る所は、WA・mocs・tlb・uc だけ（grep）
- ユーザーの決定との対応:
  - 「設計まで」（`master.md:640`）: D1 の既定 A、D2 の既定 Future Work、D6 の既定「入れない」と合っています（聞き直しの点は m-c）。
  - 2026-10-18 の起動の語の廃止（`master.md:655`。`i915.gen9.display=` を廃止し、切り分けは `i915.debug=display` と `i915.start=manual` だけ）: D1 の B で「語を増やす事も含めて聞く」としており、既定 A の理由にも書いてあります。廃止された語の名前と行番号も正しいです。なお `i915.gen9=on/off`（`i915.c:497-520`）は今も残っており、design の「i915.gen9 の gate」の記述もこれと合っています。
- §1 の行（`device.c:750-785`、`device-info.h:78-82`、`device-info.c:90-95,256,372-406,127,536-544`、`compiler/eu.c:532-544`、`submit.c:9-14`）は全部合っています。
- HAL・UAPI の変更は要りません（版 1 のレビューのとおり）。

## 確かめていない事

- Linux v6.19 の pciids と 6.8.12 の ID の差（v6.19 の tree を探さなかった）。
- `plan/ws229/tests/gen12-golden/run.sh` が WS244 の後の今の tree で build・PASS するか（実行していない）。
- Gen11 の EU の encoding が今の Gen9 の物とどこまで同じか（N3 の推測は、WS229 p001 の記述と今の encoder が Align1 だけである事から）。
- N2 で「Gen12 の道で正しい」とした行の一部（`defaults.c:326`、`irq.c:714`）は、Linux の該当の所を全部は読んでいません。
- WS229 の実績の LW。
- RKL・ADL-S・ICL の実機での動き（機械が無い）。

## p001 を cleared にしてよいか

まだ cleared にしません。N1〜N4 は範囲の小さい書き直し（受け入れの道、一覧の補完、LW の根拠、ICL の attach の問い）で済みます。版 3 でこれらと m-f の写しを直し、短い確認を通した後なら、設計として受け入れてよいと考えます。
