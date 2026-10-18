<!-- design-reviewer の短い確認（2026-10-19、design.md 版 3、main d186231c4 に対して）。Q1 が報告の本文をそのまま置いた -->

# WS252 design.md（版 3）の短い確認

対象は /home/awe/zedBSD-claude1/plan/ws252/design.md（版 3）と、その写しの ws.md・phase001/phase.md です。main（d186231c4、clean）の `src/drivers/gpu/i915/`、`plan/ws229/tests/gen12-golden/`、Linux 6.8.12（scratchpad の lx/、読むだけ）と照らしました。source・plan の変更、commit、build、QEMU、rm はしていません。`.internal/` も読んでいません。

**結論**: N1〜N4 と m-a〜m-g は、おおむね直っています。Blocker と Major はありません。版 3 で新しく入った誤りと矛盾は、どれも Minor です（下の 1〜9）。

## Blocker
ありません。

## Major
ありません。

## Minor

1. **xd の表には RKL・ADL-S・RPL-S・ICL の ID がもう入っている。版 3 で入った誤り**
   - §1 は 3 つの表を「どれも TGL と ADL-P 系と Gen9 だけ」と書いています。§3 冒頭も「3 つの PCI ID の表に無く」です。
   - 実際には `intel/display-tables-xd.h:1048-1077` に `i915_xd_ids_icl`・`_icl_no_f`・`_rkl`・`_adls`・`_rpls` があります。WS245 `design.md:413` にも「RKL・ADL-S/RPL-S・ICL の ID は xd-platform の記述だけが持つ」とあります。§6.1 と §3 の DMC の文（xd の表が rkl・icl の DMC を名指しする）も、§1 と食い違っています。
   - そのため、p003・p006 と ws.md の「3 つの表に行を足す」のうち、xd の分は作業になりません。D1=A の「build option の時だけ入る行」も、xd の表には当たりません。xd の行は今も無条件にあり、attach を決めるのは `i915.c:80-89` の `identifiers[]` だけです。
   - p002 の「1 つの表から 3 つを引く」は、WS245 の file（xd の表）の形を変えます。WS245 は incomplete なので、WS 間の調整が要ります。
   - 修正案: §1・§3・p003・p006・ws.md の p003 を「足すのは `identifiers[]` と `device.c` の 2 つ。xd は既にある」に直します。p002 の表の統合は「WS245 と合わせる（Q1）」と書きます。

2. **§6.2 の分け方の誤り（Linux を読めば決まるもの）**
   - `ggtt.c:257`: Linux の Gen11 は `gen8_ggtt_invalidate` です（`intel_ggtt.c:1277`）。ただし `needs_wc_ggtt_mapping` が ver ≥ 11 で false なので（`intel_ggtt.c:207`）、何も書きません。`GEN8_GTCR` は GuC の時の道だけです（`intel_ggtt.c:256`）。よって ICL は今の Gen12 側（書かない）で正しく、「直す」から「正しい」へ移せます。
   - `workarounds.c:470`（copy engine の MOCS、ver ≥ 12）: Linux も `GRAPHICS_VER >= 12` の時だけ `gen12_ctx_gt_mocs_init` を呼びます（`intel_workarounds.c:912-913`）。ICL は今のままで正しく、「未確認」は解けます。
   - `device-info.c:541,543`（context の大きさ）: 今の code は既に ver 11 を 12 と同じ値で受けています（`device-info.c:543` の `!= 11 && != 12`）。枝を足す必要もありません。
   - `device-info.c:245` を「SSEU・fuse」とだけ書いているのは足りません。245 は世代の記述ごとを選び、ICL は Gen12 の記述を取ります。その中の fault の register `0x0cec4` は ICL では誤りです。Linux の Gen11 は `GEN8_RING_FAULT_REG` 0x4094 を使います（`intel_gt.c:366-372`）。engine の表・domain・timestamp も同じ記述から来ます。「第 3 の世代の記述（fault の register を含む）」と書くべきです。
   - `tlb.c:174`: Linux 側の差は「Wa_2207587034 の対象外」だけではありません。ICL は Gen8 の register（`intel_engine_cs.c:1220-1222`、VCS2 は別扱い、1247 行）で、zedBSD の gen9 の layout に VCS2 と Gen11 の domain を足す形です。
   - `i915.c:407` は attach ではなく `i915_suspend`（387 行から）の Gen9 の D0 の枝です。
   - 未確認だった 3 行は Linux で確かめられ、結論は design のとおりです。`engine.c:168` は Wa_22011802037 で ver ≥ 11 が対象です（`intel_reset.c:1633-1644`、`intel_engine_cs.c:1666-1668`）。`defaults.c:326` は null state が版 6〜9 だけです（`intel_renderstate.c:15-31`）。`workarounds.c:1047` の GTT の WA は ICL にも要ります（WaIncreaseDefaultTLBEntries、ver 9〜11、`intel_gtt.c:457-460`）。

3. **grep の「全部」の漏れ**
   - `workarounds.c:428`（`graphics_ver < 4`、ICL でも正しい）が一覧にありません。
   - grep の型では `g->gen9`（`device-info.c:251,257,268,297,347`）と `render/state.c:974-975` の局所の `gen9` が拾えません。根の行（245・975）は載っていますが、その事を一言書くべきです。
   - 除いた試験として `eu-test.c` しか書いていません。`tests/fixtures/draw-fixture.c`（17 行）と `tests/execution/draw-test.c`（3 行）も版を見ており、ICL の時に試験の側も直す量があります。

4. **数の誤り**
   - §6.2 と「レビュー 2 への対応」の表は、`render/state.c` を「18 か所」としています。grep では 14 行で、列挙した 14 行がすべてです（「ほか」はありません）。
   - §6.3 の「版を見る所は GT で約 25」は、私の数え方（render・compiler・display・試験を除く）では約 37 行です。compiler・render の約 35 は合っています。

5. **LW の足し算が合わない**
   - §5 では p001 2・p002 5・p003 2・p004 6・p005 6・p006 2・p007 3・p008 2 です。RKL・ADL-S だけ（p001 を除く）が 12、ICL まで 26 で、ここは合っています。
   - ところが §6.3 は「ICL は p004〜p006 で 14 LW、通しを含めて約 15〜17」とし、§8 D2 は「全部（p004〜p006、約 15〜17 LW）」と書いています。p004〜p006 は 14 です。
   - phase.md の「ICL 15〜17、RKL・ADL-S 12、全部 26」では 12＋15〜17＝27〜29 になり、26 と合いません。p007 の 3 LW も、ICL の分で増えない形のままです。
   - 修正案: p007 を「RKL・ADL-S 3 ＋ ICL 1〜3」に分け、全部を約 27〜29 にします。D2 の括弧も直します。D2 の数字はユーザーに見せるので、朝の前に直すのがよいです。

6. **RKL・ADL-S の 12 LW が WS229 の見積もりと突き合わせていない（N3 の残り）**
   - `plan/ws229/phase001/phase.md:231-233` は、RKL・ADL-S の GT を各 0.3 LW と見積もっています。
   - 版 3 は ICL の差だけを説明し、RKL・ADL-S の約 20 倍（p002 5 LW の中身。表の統合、golden、`gt->is_alderlake_p` の削除など）を説明していません。p008 の 2 LW も WS229 の 0.5 より大きいです。
   - p002 の 5 LW の内訳を 1 行添えます。

7. **試験の道の穴（§4）**
   - gen12-golden（`run.sh` 冒頭）は「基準の commit と今の tree の出力が同じか」だけを見ます。基準に無い RKL・ADL-S・ICL の列は、比べる相手がありません。
   - 新しい platform の誤りを host で見つけるのは、§4-2 の「Linux から機械的に抜き出した期待」だけです。これは新しい道具なのに、どの Phase にも LW にも入っていません。「新しい harness は作らず」とも食い違います。
   - §4-4 は Mesa の道具を新たに使う形です。しかし Master の Tools には、既に WS229 の道具が載っています。`plan/ws229/tests/eu-gen9-host.sh`（`--gen=skl` の往復、master.md:1176）と `plan/ws229/tests/host/run.sh`（gen90/gen120.xml の decode、1177）です。これらを `--gen=icl`・gen110.xml に広げる、と書くのが筋です。
   - Mesa の tree は `/home/awe/zedBSD-worktrees/p1/build/mesa-tools/` にだけあります。main の checkout の `build/` にはありません（確かめた）。
   - 修正案: §4-2 の抜き出しの script を p002（RKL・ADL-S の列）と p004（ICL）の作業と LW に入れます。§4-4 は既存の 2 つの道具の拡張と書きます。

8. **D1 を 2 つの群で別々に聞く場合が、p007 の表と ws.md に無い**
   - §8 D1 は「2 つの群を別々に聞いてよい」としていますが、§5 の p007 の依存の表と ws.md の受け入れは、D1 を 1 つの値として書いています。例えば RKL=A・ICL=B の時の image が決まっていません。
   - D2 の既定（Future Work）では ICL の D1 は意味を持ちません。D2 を先に聞き、D2=全部の時だけ ICL の D1 を聞く順にすると、問いが減ります。
   - p007 の表の B・C の行は p002 を書いていません。p003 が p002 に依存するので、推移的には満たされます。

9. **build option の仕組みと写しの古さ**
   - 仕組みは既にあります。`config/kernel-options.list:12` の `CONFIG_AMDRDNA_CONTROL`、`Makefile:239`（`?=` の既定）、`Makefile:628`（`-D…`）、`src/drivers/gpu/amdrdna/control.c:36-40` が前例です。§3・§9 の「未確認」は解けます。
   - ただしこの道では、共有の `Makefile` と `config/kernel-options.list` を変えます。p003 の影響の欄にこれを書き、Q1 との調整と menuconfig の試験（MAC-T001 の期待）の追従を入れるべきです。
   - BUG-335 は既に起票済みです（`plan/known-bugs.md:205`）。§2.1・§8・ws.md は「Q1 に起票を依頼」のままなので、BUG-335 への link に直します。
   - ws.md の冒頭の Status が「q1262 で実施中」のままです（Resume point は版 3）。
   - ws.md の「前提」の「ICL は WS245 の p014」は古く、p014 は canceled で p045 に移っています（`plan/ws245/ws.md:164,215`）。
   - phase.md の「読んだ物」が版 1 のままです。

## 指摘ごとの判定（版 2 → 版 3）

| 指摘 | 判定 | 根拠 |
| --- | --- | --- |
| N1 | 直った | (a) を採用。p003 は D1 のどの値でも行い、p007 の表と ws.md の受け入れも揃った。仕組みの前例は上の 9 |
| N2 | ほぼ直った | 載っている zedBSD の行は全部実在し、中身も合う。WA の list・whitelist・MOCS・PAT・GTT・RPS・forcewake（`device.c:1316`）も入った。残りは上の 2・3 |
| N3 | 一部 | 引用は 8 LW（`ws229/phase001/phase.md:42`）に直り、231 行との差の理由も書かれた。残りは上の 4・5・6 |
| N4 | 直った | D1 が ICL を含み、p006 の依存に D1（ICL）がある。WS245 `design.md:920` の #30 と合う。残りは上の 8 |
| m-a | 一部 | gen12-golden を使う方針。file の一覧の source は全部ある（存在だけ確かめた）。残りは上の 7 |
| m-b | 直った（ただし新しい誤り） | `i915.c:80-89` の `identifiers[]` が入った。xd の表の誤りは上の 1 |
| m-c | 直った | D6 は朝の一覧から外れ、D5 は「決定の解釈」と書き分けた |
| m-d | 直った | D4 と p002 の MOCS の作業が外れた。BUG-335 の link は上の 9 |
| m-e | 直った | `i915.c:258` が `i915_attach`、gate は 270-284、driver の probe の欄は NULL（`i915.c:95`、確かめた） |
| m-f | 一部 | phase.md の要点と ws.md の p002〜p008 が入り、master.md:21・577 も版 3 になった。古い所は上の 5・9 |
| m-g | 直った | 12 LW が全部の D1 に共通 |

## ユーザーの決定との対応
- 2026-10-19「設計まで」（`plan/master.md:640`）: p002 以降は実機まで planned、D6 は聞かない、D5 は解釈と明記しており、合っています。
- 2026-10-18 22:12 の起動の語の廃止（`plan/master.md:655`）: 既定の A は起動の語を増やさず、config.mk の build option です。B は「語を増やす事自体を聞く」としています。合っています。2026-10-04 の「試験の image は config.mk＋個別 file」とも合います。
- HAL・UAPI の変更は要りません。

## 確かめていない事
- ICL の割込みの layout が Linux の Gen11 の系統か（`i915_irq.c` は手元の tree に無いため）。
- gen12-golden と Mesa の道具が今の tree・host で build・PASS するか（実行していない）。
- v6.19 と 6.8.12 の PCI ID の差。
- WS229 の実績の LW。
- RKL・ADL-S・ICL の実機での動き（機械が無い）。

## p001 を cleared にしてよいか
設計としては cleared にしてよいです（Blocker・Major はありません）。ただし、朝にユーザーへ D1・D2 を聞く前に、5 の LW の数字（D2 の「約 15〜17 LW」）と 1 の xd の表の記述だけは直すのがよいと考えます。
