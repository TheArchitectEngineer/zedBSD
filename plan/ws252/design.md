# WS252 設計: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT

<!-- awesome-plan project=zedbsd record=ws252-design -->

- 版: 3.1（2026-10-19、P1、q1265、ws252-p001）。版 1 は q1262、版 2 は q1263、版 3 は q1264。[design-review-1.md](design-review-1.md) の対応は末尾の「レビュー 1 への対応」、[design-review-2.md](design-review-2.md)（Major N1〜N4・Minor m-a〜m-g）の対応は末尾の「レビュー 2 への対応」、[design-review-3.md](design-review-3.md)（短い確認、Blocker・Major 無し、Minor 1〜9）の対応は末尾の「レビュー 3 への対応」。p001 は版 3.1 で cleared。
- 範囲: GT（render・copy・video の engine、GuC/HuC の要否、kernel の中の Vulkan の executor `render/` と compiler `compiler/`）。display は [WS245](../ws245/ws.md)。
- 範囲外: Gen11 の兄弟の EHL・JSL（engine 4 つ、ppgtt 36 bit。Linux 6.8.12 `i915_pci.c:626-638` の `ehl_info`・`jsl_info`）。ICL の ID を足す時に混ぜない。
- ユーザーの決定（2026-10-19、クリック）:「設計まで」。この文書は Phase の計画で止め、実装は実機ができてから。
- この文書は手順の要約で、Linux の code・comment・構造を写さない。出典は節ごとに書く。

## 1. 今の code の形（WS244 の書き直しの後、`src/drivers/gpu/i915/`）

調べた事実（この worktree、基点 main）:

| 点 | 今の形 | 場所 |
| --- | --- | --- |
| 種類の判定 | PCI ID の表は 3 つ: attach の一致の `identifiers[]`（`i915.c:80-89`、ここに無い ID には i915 が付かない）と GT の種類の表（`device.c`、`intel/pci-ids.h`）は TGL と ADL-P 系（ADL-P・ADL-N・RPL-P・RPL-U）と Gen9 だけ。display の xd の表（`intel/display-tables-xd.h`）は WS245 の物で、**RKL・ADL-S・RPL-S・ICL の ID を既に無条件に持つ**（`i915_xd_ids_icl`・`_icl_no_f`・`_rkl`・`_adls`・`_rpls`、1053-1080 行を開いて確かめた。WS245 `design.md:413` も同旨）。attach を決めるのは `identifiers[]` だけ。Gen12 は `graphics_ver=12`、`platform=I915_PLATFORM_GEN12` の 1 種、ADL-P は `is_alderlake_p` の旗だけで区別 | `device.c:750-785`、`device-info.h:78-82` |
| submission | execlists だけ（Gen12 は ELSQ、Gen9 は ELSP）。GuC・HuC は load しない（`firmware.c` は display の DMC の読み込みだけ） | `submit.c:9-14`、`firmware.c` |
| engine | mask は `I915_START_ENGINE_MASK`（rcs0・bcs0・vcs0・vcs2・vecs0、TGL・ADL-P）、Gen9 は別の mask。media の fuse が mask から engine を落とす。engine の記述の行は `intel/engine-table.inc`。CCS は無い | `device.c:54,1383-1385`、`device-info.c:273` |
| GT の形 | Gen12 は 1 slice・6 DSS・16 EU を上限に fuse を読む。L3 bank の steering は fuse から | `device-info.c:90-95,256,372-406` |
| context の大きさ | render は Gen11・Gen12 で同じ値、他の版は MISSING_CASE の log | `device-info.c:127,536-544` |
| MOCS | Gen12 は 1 つの表（Linux の `gen12_mocs_table` の値）、global MOCS。uncached は index 3、unused は index 2。entry 63 は行が無く unused の値（L3 WB）を取る | `intel/mocs.h:118-119`、`workarounds.c:708-760`、`intel/mocs-table.inc:12-13` |
| workaround | Gen12 の list は platform・stepping の条件を持たない（Gen9 の条件だけが platform を見る） | `workarounds.c:1234-1300` |
| compiler | EU の encoding は Gen9 と Gen12 の 2 つ。他の版は誤り | `compiler/eu.c:532-544`、`intel/eu-encoding-gen{9,12}.h` |
| 3D・compute の state | genxml は Gen9 と Gen12（`intel/genxml-gen9.h`・`genxml.h`）。`render/` は `graphics_ver == 9` か、それ以外を 12 として扱う | `render/batch.c:124,162`、`render/blit.c:106-108` |

結論: Gen12 の道は「TGL と ADL-P の 2 つ」を前提に、platform の差を持たない形で書かれている。RKL・ADL-S は同じ Gen12（graphics 12.0）なので、差は「表の行」で足りる。ICL（Gen11）は compiler・genxml・MOCS・WA・forcewake の各所に第 3 の版を要する。

## 2. platform ごとの差（Linux 6.8.12 から学んだ事実）

出典（この節）: Linux v6.8.12 `drivers/gpu/drm/i915/i915_pci.c`（`icl_info`・`tgl_info`・`rkl_info`・`adl_s_info`・`adl_p_info`・`GEN11_FEATURES`・`GEN12_FEATURES`）、`gt/intel_mocs.c`（`get_mocs_settings`）、`gt/intel_workarounds.c`（`rcs_engine_wa_init`・`gen12_ctx_workarounds_init` の注記の platform の列）、`gt/uc/intel_uc.c`（`uc_expand_default_options`）、`gt/uc/intel_uc_fw.c`（firmware の表）、`gt/intel_engine_cs.c`（context の大きさ）。値は読んで要約しただけで、code にしていない。

| 点 | TGL（今） | ADL-P（今） | RKL | ADL-S / RPL-S | ICL |
| --- | --- | --- | --- | --- | --- |
| graphics の版 | 12 | 12 | 12 | 12 | 11 |
| engine | rcs・bcs・vcs0・vcs2・vecs | 同左 | rcs・bcs・**vcs0 だけ**・vecs | TGL と同じ | TGL と同じ |
| DMA の幅 | 39 bit | 39 bit | 39 bit | 39 bit | 39 bit（差は無い。Linux は GEN8_FEATURES の `dma_mask_size = 39` を全部が引き継ぐ、`i915_pci.c:419-426`。zedBSD も 39 bit 固定、`device.c:59-60`） |
| MOCS | Linux は `tgl_mocs_table`（entry 1 が page table の値） | `gen12_mocs_table` | **TGL と同じ `tgl_mocs_table`** | `gen12_mocs_table` | `icl_mocs_table`、**global MOCS が無い**（engine ごと） |
| 既定の uC（Linux） | 使わない | GuC submission + HuC | 使わない | ADL-S: HuC の load だけ、RPL-S: GuC submission + HuC | 使わない（版 < 12） |
| render の WA | 共通 + Wa_1607297627 | 共通 + Wa_1607297627 | 共通 + Wa_1607297627 | 共通、**Wa_1607297627 は無い** | 別の list（ICL の gt・ctx・whitelist、`_3D_CHICKEN3`・GARBCNTL など） |
| ELSQ | 有り | 有り | 有り | 有り | 有り（Gen11 から）。ただし CSB は Gen8 の形（`gen8_csb_parse`、`intel_execlists_submission.c:1911-1915`） |
| context の大きさ | 同じ | 同じ | 同じ | 同じ | 同じ（Gen11 と Gen12 は同じ値） |
| EU の ISA | Gen12（SWSB） | 同左 | 同左 | 同左 | **Gen11（Gen9 系の encoding、SWSB 無し、fp64・int64 の扱いが Gen9 と違う）** |

要点:

1. **RKL・ADL-S/RPL-S は今の Gen12 の道に「表の行」を足すだけ**: engine の mask（RKL の vcs2）、Wa_1607297627 の有無（ADL-S・RPL-S で外す。今の `workarounds.c:166` は全部の Gen12 に無条件）、PCI ID（`identifiers[]` と `device.c` の 2 つ。xd の表は既に持つ、§1）。MOCS は今の `gen12_mocs_table` の値のまま（RKL の Linux は `tgl_mocs_table` だが、差は ABI の理由。§2.1）。DMA の幅は差が無い。compiler・genxml・submission は変えない。
2. **GuC/HuC は要らない（推測を含む）**: zedBSD は ADL-P で Linux の既定（GuC submission）と違って execlists で動いている（実機 5330 の証拠は WS244 の記録。RPL-P の機械は無く未確認）。ADL-S・RPL-S も execlists で足りるのは、Linux が `enable_guc=0` で execlists に戻れる事からの推測（`gt/uc/intel_uc.c:33-56` の既定の表は確かめた）。HuC は HEVC の一部・VDEnc の認証に要るが、今の video（`render/video-mfx.c`、H.264 の decode）は HuC を使わない。HuC を足すのは別の WS の判断（§8 D3）。
3. **ICL は「第 3 の版」**: Gen11 の EU encoding、3D・compute の state、CSB・LRC・request の emitter、PTE・PPAT、engine ごとの MOCS、ICL の WA、forcewake・MMIO の範囲・reset の domain・fuse。一覧は §6.2。

### 2.1 Gen12 の MOCS の Linux との差（WS252 の範囲の外。[BUG-335](../bugs/BUG-335.md)）

- Linux は TGL・RKL に `tgl_mocs_table` を、ADL-P 以降に `gen12_mocs_table` を使う。分けた理由は Linux の注記「For TGL/RKL, Can't be changed now for ABI reasons」（`gt/intel_mocs.c:530-531`）で、user の ABI を守るためで hardware の要求ではない。zedBSD の render は uncached の index 3 しか使わず（`render/heap.h:194-199`）、index 3 は 2 つの表で同じ値。よって TGL の表の差は不具合の候補とは言えない（hardware が index 0・1・未定義の index を自分で使う事が無ければ。未確認）。
- 全部の Gen12 に効く差: Linux の `GEN11_MOCS_ENTRIES` には entry 63（L3 UC、`gt/intel_mocs.c:259-261`）があるが、zedBSD は 63 の行を持たず unused の値（L3 WB）を入れる（`intel/mocs-table.inc:12-13`）。使う index ではないので影響は未確認。
- 版 3 で範囲を揃えた（レビュー 2 m-d）: entry 63 と TGL の表は今の TGL・ADL-P の道の話で、RKL・ADL-S・ICL の対応とは独立なので、**WS252 では扱わない**。版 2 の D4 と p002 の MOCS の作業は外した。[BUG-335](../bugs/BUG-335.md) として起票済み（`plan/known-bugs.md:205`。直すなら TGL の 5320 と ADL-P の 5330 の両方の回帰が要る）。
- RKL は今の Gen12 の表（TGL と同じ値）をそのまま使う。試験の golden は「Linux と違う事を意図した行」としてこの 2 つ（TGL・RKL の表、entry 63）を列に持つ（§4）。

## 3. attach の判断の材料（WS245 §7 #18・#20・#30）

- 今: RKL・ADL-S/RPL-S・ICL は `identifiers[]` と `device.c` の表（§1）に無く（xd の表には既にある）、i915 は付かず efifb（§7 #18「今のまま（efifb）」）。WS245 は ICL の attach を「WS252 の後の別の Phase（WS245 の外）」としている（`plan/ws245/design.md` §7 #30。レビュー 2 の引用、行は未確認）ので、ICL の attach の判断もこの WS の D1 で聞く（レビュー 2 N4）。
- WS245 の改訂 5: display だけの attach は作らない。present は GT の GGTT を要る（`display/resident.c:1864`）。よって **ID を足すと GT の初期化も走る**。
- 起動の順: `drv_i915_device_start` は display の start・outputs（`device.c:424-438`）を GT の start（`device.c:441`）より先に行う。初めての platform で GT が失敗すると、display は firmware から引き取られた後で、画面・console の振る舞いは未確認（推測: 画面が消えたまま node が出ない）。
- engine: RKL の vcs2 は media の fuse で落ちる可能性がある（`device-info.c:273`、RKL の fuse の報告は未確認）。
- DMC: image に入る DMC は `adlp_dmc.bin`・`tgl_dmc_ver2_12.bin` だけ（`userland/firmware/i915/Makefile:54-71`）。xd の表が名指しする `rkl_dmc_ver2_03.bin`・`adls_dmc_ver2_01.bin`・`icl_dmc_ver1_09.bin` は入らない。attach の Phase の依存に取得と照合を入れる。
- 実機: 5330 = ADL-P、5320 = TGL、SV7 = Gen9、13400F の desktop = RPL-S だが **F 型で iGPU が無い**。RKL・ADL-S・RPL-S（iGPU 有り）・ICL の機械は無い。
- 選択肢（§8 D1。RKL・ADL-S/RPL-S と ICL のそれぞれに当てる）:
  - **A（既定）: 既定の image では付けない**。ID は `identifiers[]` と `device.c` の表の 2 つに「build option が有る時だけ入る行」として足す（p003・p006。xd の表は既に無条件に持ち、attach を決めないので触らない）。試験の image は、WS の `tests/` の config.mk でその option を立てて build する（2026-10-04 の「config.mk での build＋個別ファイル」の規則に合う）。起動の語は増やさない。実機ができた時、試験の image で GT の受け入れ（p007）を通せる（レビュー 2 N1 の案 (a)）。build option の仕組みは既にある（レビュー 3 #9、開いて確かめた）: 前例は `CONFIG_AMDRDNA_CONTROL`（`config/kernel-options.list:12` の宣言、`Makefile:239` の `?=` の既定、`Makefile:628` の `-D…=1/0`、`src/drivers/gpu/amdrdna/control.c:36-40` の `#ifndef … 0` と `#if`）。同じ形で i915 の option（名前は p003 で決める、例 `CONFIG_I915_RKL_ADLS`・`CONFIG_I915_ICL`）を足す。共有の `Makefile` と `config/kernel-options.list` を変えるので、p003 の影響に Q1 の調整と menuconfig の試験（MAC-T001 の期待）の追従を入れる（§5）。
  - B: ID を既定の image に足し、起動の語で opt-in。gate は `i915_attach` の初め（`i915.c:270-284`、Gen9 の `i915.gen9` の gate と同じ所。driver の probe の欄は NULL）に置く。ユーザーは 2026-10-18 に `i915.gen9.display=` の語を廃止した（`plan/master.md:655`）ので、語を増やす事自体を聞く。
  - C: ID を既定で足す。確かめていない GT の道が既定で走り、GT の失敗は display の引き取りの後に起こる。推奨しない。
  - B・C で「display の引き取りの前に GT を確かめる」順の変更をするかは、attach の Phase の設計で決める（WS245 の範囲と重なる）。
- D1 は D2 の後に聞く（レビュー 3 #8、§8）。ICL の D1 は D2=全部の時だけ意味を持つ。
- 実機の前に p002 を入れる案（版 2 の A'・D6）は、2026-10-19 の「設計まで」（`plan/master.md:640`）で既に答えが出ているので聞かない。Q1 が入れたい理由を持つ時だけ聞き直す（その時の試験は T1 の 5330 の回帰 1 回、5320 はユーザーの試験の一覧）。

## 4. 実機が無い前提の試験の道

QEMU は Intel の GPU を模倣しない（GVT-g・passthrough も実機を要る）。QEMU では i915 が付かないので、QEMU の起動は p002 以降の変更を通らず、受け入れの証拠にしない。

1. **host の golden（既存の物を直して使う、p002 の作業）**: `plan/ws229/tests/gen12-golden/run.sh`（`golden.c`・`stubs.c`）は、基準の commit と今の tree の両方の i915 の source に golden を build し、Gen12 の GT の初期化の全部の register の書き込み、WA の list、context の image、request の ring、submission の書き込みを順に出して比べる（`run.sh` の冒頭の注記で確かめた。`plan/ws229/phase002/phase.md:41` で 543 行の PASS、レビュー 2 の引用）。新しい harness は作らず、これを p002 で次のように直す（レビュー 2 m-a）:
   - WS244 の書き直しの後の今の tree で build・PASS するかは**未確認**（今回は走らせていない）。直す量が見えるのは p002 の初め。
   - platform（TGL・ADL-P・RKL・ADL-S・RPL-S、ICL の時は ICL）ごとに記述を選ぶ引数を足す。
   - WS229 の Phase の中に置かず、`plan/tools/` か tests/ のシナリオに移して Master の試験の一覧に登録する（2026-10-06 の試験の整理の基準）。移す path と登録は Q1 に頼む。
   - gen12-golden は「基準の commit と今の tree の出力が同じか」だけを見る（`run.sh` の冒頭）。基準に無い RKL・ADL-S・RPL-S・ICL の列は比べる相手が無いので、新しい platform の誤りは下の 2 の期待でだけ見つかる（レビュー 3 #7）。
   - 見積もり: 版 2 の harness の 2 LW を 1 LW に下げる（直せない理由が p002 で分かれば 2 LW に戻す）。
2. **期待の列（新しく作る小さな script、版 3.1 で Phase と LW に入れた）**: Linux 6.8.12 の source から platform の行（engine の mask、WA の list、MOCS の表、ICL の時は forcewake の範囲など）を機械的に抜き出す script を作り、golden の新しい列と比べる。RKL・ADL-S・RPL-S の列は p002（0.5 LW）、ICL の列は p004（1 LW）。「新しい harness は作らない」は gen12-golden の本体の事で、この抜き出しの script は別の小さな道具。script が書けない所は実装と別の担当が照合する。実装する人が Linux を読んで手で書いた期待だけにしない（同語反復を避ける）。
3. **既存の platform の不変**: TGL・ADL-P の今の列を golden に固定（基準の commit との比較で、gen12-golden の今の使い方そのもの）。golden は「Linux と違う事を意図した行」（§2.1 の TGL・RKL の MOCS、entry 63）を列に持ち、Linux の期待と食い違う所はそこだけと示す。
4. **compiler（ICL のとき）**: Master の Tools に載る WS229 の 2 つの道具を広げる（新しい道具は作らない、レビュー 3 #7）: `plan/ws229/tests/eu-gen9-host.sh`（`--gen=skl` の往復、`plan/master.md:1176`）を `--gen=icl` に、`plan/ws229/tests/host/run.sh`（gen90・gen120.xml の decode、1177）を gen110.xml に。中で使う Mesa 25.0.7 の道具は次のとおり。Mesa の tree は P1 の worktree の `build/mesa-tools/`（`build-asm`・`mesa-25.0.7`）にだけあり、main の checkout の `build/` には無い（レビュー 3 の確認、自分では P1 側の存在だけ確かめた）。p005 の初めに置き場を Q1 と決める。
   - `brw_asm`・`brw_disasm`（`build/mesa-tools/mesa-25.0.7/src/intel/compiler/brw_asm_tool.c`・`brw_disasm_tool.c`、source がある事を確かめた）で、zedBSD の encoder が出す Gen11 の命令を `--gen=icl` で disasm し、同じ命令を `brw_asm` で組んだ bit と比べる。
   - 3D・compute の state は `src/intel/genxml/gen110.xml`（同じ tree にある事を確かめた）を decode の表にして、zedBSD の `render/` が出す packet を field ごとに読む。
   - host での build は今回試していない（未確認）。worktree の `build/mesa-tools/` の build 済みの binary の有無も確かめていない。
   - 意味（latency・依存）の誤りは捉えられない。
5. **5330・5320**: 動いている道を変える Phase（p002 など）は T1 の 5330 の回帰 1 回（起動・内蔵 LCD・desktop）、5320 はユーザーの試験の一覧へ。
6. **実機で初めて分かる項目（全部未確認）**: RKL・ADL-S の fuse（2 DSS、L3 bank の数）、MCR の steering の先、WA の read-back（`verify-workarounds.c`）、forcewake の ack、GT が失敗した時の画面。実機ができたら、起動・GT の初期化・Vulkan の三角形・blit・video の decode の順。

## 5. Phase の分割と LW

（LW は既存の WS の換算。8 LW ≈ 2〜3 時間。**p002 以降は実機を得るまで planned**（2026-10-19「設計まで」）。）

| Phase | 内容 | 依存 | 試験 | LW |
| --- | --- | --- | --- | --- |
| p001 | この調べと設計 | — | design-reviewer | 2 |
| p002 | Gen12 の platform の記述の行（TGL・ADL-P・RKL・ADL-S・RPL-S）: PCI ID の解決から engine の mask・Wa_1607297627 の有無・名前を引く（§6.1）。ID は足さない。gen12-golden を直して platform ごとに。Linux から期待を抜き出す script（RKL・ADL-S・RPL-S の列） | p001、実機（D5） | §4-1〜3、build warning 0、T1 の 5330（§4-5） | 5（内訳: 記述の行と PCI ID の解決 1.5、`identifiers[]`・`device.c` の ID の解決を 1 つにする（xd の表との統合は WS245 と合わせる、Q1） 1、`gt->is_alderlake_p` の削除と注記 0.5、gen12-golden の直し 1、抜き出しの script 0.5、5330 の回帰の準備と記録 0.5） |
| p003 | RKL・ADL-S・RPL-S の attach: `identifiers[]`（`i915.c:80-89`）と `device.c` の表の 2 つに行を足す（xd の表は既に持つ）。D1=A なら build option（`CONFIG_AMDRDNA_CONTROL` と同じ形、§3）の時だけ、B なら起動の語の gate（`i915_attach` の初め）、C なら既定。DMC の firmware の取得と照合。**影響**: D1=A では共有の `Makefile`・`config/kernel-options.list` を変えるので Q1 と調整し、menuconfig の試験（MAC-T001）の期待を追従させる | p002、D1、実機 | host（ID → 記述）、MAC-T001、実機 | 2 |
| p004 | ICL の GT の core（D2 が「全部」のとき）: §6.2 の p004 の行（WA の list・whitelist・MOCS の build・第 3 の世代の記述を含む）。抜き出しの script の ICL の列 | p002、D2=全部、実機 | host（golden の ICL の列と抜き出した期待）、実機 | 6（script 1 を含む） |
| p005 | ICL の compiler と state: Gen11 の EU の encoding、genxml の第 3 の版、`compiler/`・`render/` の分岐（§6.2 の p005 の行） | p004 | §4-4、実機 | 6 |
| p006 | ICL の video・PXP の除外・attach（`identifiers[]`・`device.c` の ICL の行、D1 の ICL の選択に従う）・ICL の DMC | p005、D1（ICL）、WS245 p045 | host、実機 | 2 |
| p007 | 実機の通し（GT の初期化・Vulkan の三角形・blit・video、5330 の回帰） | 下の表 | 実機、T1 の 5330 | RKL・ADL-S 3 ＋ ICL 1〜3（D2=全部の時） |
| p008 | 規約の全文の見直し（WS の変えた C） | 最後の実装の Phase | build、host 試験 | 2 |

p007 の依存（D の結果ごと、レビュー 2 N1・N4）:

D1 は群ごとに値を持つ（D1-R: RKL・ADL-S/RPL-S、D1-I: ICL。D1-I は D2=全部の時だけ聞く、§8）。p007 は群ごとに、その群の D1 の値で次のとおり（2 つの群の値が違う時は 1 つの image に両方の形を入れる。例 D1-R=A・D1-I=B なら、試験の config.mk で RKL・ADL-S の option を立て、ICL の ID は既定で入り起動の語で付く image）:

| 群の D1 | p007 の依存（その群の分） | その群の image の形 |
| --- | --- | --- |
| A（既定） | RKL・ADL-S: p002・p003／ICL: p004〜p006。その群の実機 | 試験の config.mk でその群の ID の option を立てる |
| B | 同上（p003・p006 は p002 に推移的に依存） | 既定の image ＋起動の語 |
| C | 同上 | 既定の image |

D2 が Future Work（既定）か外すなら、ICL の行は無く D1-I も聞かない。

合計（D1 の A・B・C で同じ。p003 は A でも行う）: RKL・ADL-S だけ（p002 5・p003 2・p007 の RKL・ADL-S 分 3・p008 2）で約 12 LW。ICL まで（p004〜p006 の 14 と p007 の ICL 分 1〜3 を加える）で約 27〜29 LW。見積もりの根拠は §6.3。

## 6. 既存の code への影響（code は書いていない）

### 6.1 RKL・ADL-S（p002・p003）

- platform の差は、WS245 の方針（`plan/ws245/design.md:412`「記述の field で書き、`kind` で分岐しない」）に合わせ、記述の行の field（engine の mask、Wa_1607297627 の bit、名前）で持つ。
- PCI ID の表は 3 つある（レビュー 2 m-b）: attach の一致の `identifiers[]`（`i915.c:80-89`、`I915_PCI_*_IDS` の macro の列）、GT の種類の表（`device.c`、`intel/pci-ids.h`、v6.19 から引用）、xd の表（`intel/display-tables-xd.h:1035-1085`、Linux 6.8.12 の `i915_pciids.h` と一致とレビュー 1 が確認）。**xd の表は RKL・ADL-S・RPL-S・ICL の行を既に無条件に持つ**（レビュー 3 #1、確かめた）ので、p003・p006 で足すのは `identifiers[]` と `device.c` の 2 つ。p002 は `identifiers[]` と `device.c` の ID の解決を 1 つにする。3 つを 1 つの表から引く形（xd の表の形を変える）は、incomplete の WS245 の file に触るので **WS245 と合わせる（Q1 の調整）**。調整がつかなければ xd の表は今のまま別に持つ。`identifiers[]` を変えない限り attach しない。v6.19 と 6.8.12 の ID の差は未確認。
- `gt->is_alderlake_p`（`device.c:1171`、`gt.h:99`）は読む所が無い（grep で確かめた）。p002 で消す。display の DMC の判定（`display/dmc-shell.c:110-112,338`）は display 側の `dev->is_alderlake_p` で、別物。
- `memory.h:87` の注記「on Alder Lake-P」は全部の版に当たる値なので、p002 で直す。
- `render/`・`compiler/` は RKL・ADL-S で変えない。

### 6.2 ICL（p004〜p006）で版を見る所の全部

grep（`src/drivers/gpu/i915/` の `graphics_ver` と 9・11・12・`GEN9_GRAPHICS_VER` の比較。試験の `tests/execution/eu-test.c`・`tests/fixtures/draw-fixture.c`・`tests/execution/draw-test.c` は除く。後の 2 つも版を見ており、ICL の時は p005 で試験の側も直す）の結果を全部載せる。この grep の型は `g->gen9`（`device-info.c:251,257,268,297,347`）と `render/state.c:974-975` の局所の `gen9` を拾わない。根の行（`device-info.c:245`・`render/state.c:975`）は載せたので、p004・p005 ではその根から派生する所も合わせて直す（レビュー 3 #3）。zedBSD の行は全部この worktree で開いて確かめた。「正しい」は ICL（graphics 11）が今の Gen12 側の枝に入って Linux と合う物で、変えない（確かめた範囲を理由に書く）。Linux の位置で `*` を付けた物は版 2 で自分で読んで確かめた物、他はレビューの値で未確認。

**直す（p004、GT の core）**

| 項目 | zedBSD の位置 | Linux 6.8.12 の Gen11 | 
| --- | --- | --- |
| CSB の解き方（ELSQ + Gen8 の CSB） | `submit.c:525` | `gen8_csb_parse`（`intel_execlists_submission.c:1911-1915`*） |
| LRC の layout・indirect ctx | `context.c:392`、`intel/lrc-offsets.h` | `gen11_rcs_offsets`・`gen9_xcs_offsets`（`intel_lrc.c:682-718,800-825`） |
| request の emitter | `request.c:379` | `gen11_emit_flush_rcs` ほか（`intel_execlists_submission.c:3456-3459,3526-3531`） |
| TLB | `tlb.c:174` | Gen8 の register の組（`intel_engine_cs.c:1220-1222`、VCS2 は別扱い 1247 行、レビュー 3 の値）。zedBSD の gen9 の layout に VCS2 と Gen11 の domain を足す形。Wa_2207587034 の対象外でもある |
| reset | `reset.c:388` | Gen11 の domain |
| RC6・RPS | `gt-power.c:178`、`workarounds.c:1009`（RPS の初期化の枝） | 版の分岐（`intel_rps.c`・`intel_rc6.c`） |
| **第 3 の世代の記述**（SSEU・fuse 8 subslice × 8 EU、fault の register、engine の表・domain・timestamp） | `device-info.c:245`（世代の記述ごとを選び、ICL は今 Gen12 の記述を取る。その中の fault の register `0x0cec4` は ICL では誤り） | Gen11 は `GEN8_RING_FAULT_REG`（`intel_gt.c:366-372`、版 3.1 で開いて確かめた） |
| forcewake・MMIO の範囲 | `device.c:1316`（版 9 か否かで forcewake の範囲の表を選ぶ。ICL は `__gen11_fw_ranges` の第 3 の表が要る） | `__gen11_fw_ranges` |
| device の版の分岐 | `device.c:1347,1384,2295,2413,2500` | — |
| **GT の WA の list** | `workarounds.c:979`（9 でなければ Gen12 の list。ICL に Gen12 の WA が当たる） | ICL の gt の WA |
| **engine・ctx の WA の list** | `workarounds.c:988` | ICL の engine・ctx の WA |
| **whitelist** | `workarounds.c:496`（版 12 だけ作る。ICL は無しになる） | `icl_whitelist_build` |
| **MOCS の表の build** | `workarounds.c:749`（版 12 未満は表を作らずに戻る）、`workarounds.c:738` | `icl_mocs_table` |
| engine ごとの MOCS（VCS2 の base が無い） | `workarounds.c:865-880` | `__GEN11_VCS2_MOCS0`（`intel_mocs.c:625`*、`intel_gt_regs.h:1257`*） |
| PAT・PPAT | `workarounds.c:1033`（9 なら Gen8 の private PAT、他は Gen12 の PAT） | `icl_setup_private_ppat` |
| GTT の WA | `workarounds.c:1047`（Gen9 だけ） | ICL にも要る: WaIncreaseDefaultTLBEntries は版 9〜11（`intel_gtt.c:457-460`、確かめた）。ICL に枝を広げる |
| WA の read-back の範囲 | `verify-workarounds.c:755`（9 でなければ Gen12 の multicast の範囲） | Gen11 の MCR の範囲 |
| （確かめる）割込みの layout | `irq.c:714`（9 なら Gen8 の layout、他は Gen11+ の layout） | ICL は Gen11 の layout なので**正しい側の可能性が高い**（Linux の `gen11_gt_irq_handler` の系統と推測、未確認）。p004 で確かめて、合えば変えない |
| GT の WA の build の基本 | `workarounds.c:448`（版 9 以上） | 正しい（ICL も版 9 以上） |

**直す（p005、compiler と state）**

| 項目 | zedBSD の位置 |
| --- | --- |
| EU の encoding（今は 9 と 12 だけ） | `compiler/eu.c:532-544`（538・544）、`compiler/compile.c:750`（9・12 以外は ENOTSUP） |
| compiler の版の枝 | `compiler/compile.c:3382,4967,4977`、`compiler/compile-compute.inc:395,719,780` |
| 3D・compute の state | `render/batch.c:124,162`、`render/state.c`（`GEN9_GRAPHICS_VER` との比較 14 行: 667・904・922・975・1248・1342・1435・1487・1620・1692・1824・1926・2581・3916 で全部。`grep -c` で 14 を確かめた）、`render/draw.c:1076,1132`、`render/compute.c:269,383`、`render/pipeline-prepare.c:804`、`render/blit.c:106-108,989`、genxml の第 3 の版（gen110） |
| device の名前 | `render/instance.c:307` |

**直す（p006、video・PXP・attach）**

| 項目 | zedBSD の位置 | Linux |
| --- | --- | --- |
| PXP（Gen12 だけ。今は「9 でなければ初期化」） | `device.c:1965` | `has_pxp` は GEN12_FEATURES（`i915_pci.c:645`*） |
| video の worker・申告 | `worker.c:360`、`render/vulkan.c:107` | MFX の Gen11 の差 |
| attach の gate・suspend | `i915.c:277`（Gen9 の `i915.gen9` の gate）、`i915.c:407`（`i915_suspend`（387 行から）の Gen9 の D0 の枝、確かめた） | — |

**Gen12 の道で正しい（変えない。p004 で確かめるだけ）**

| zedBSD の位置 | 理由 |
| --- | --- |
| `engine.c:168` | 9 でなければ prefetcher を止める。Wa_22011802037 で ver ≥ 11 が対象（`intel_reset.c:1633-1644` は開いて確かめた、`intel_engine_cs.c:1666-1668` はレビュー 3 の値）。正しい |
| `engine.c:217` | 注記「graphics 11 から 12.70 未満」の WA で、ICL は対象（正しい） |
| `defaults.c:326` | null render state は Gen9 の render だけ。Linux の null render state も版 6〜9 だけ（`intel_renderstate.c:15-31` の switch、確かめた）。正しい |
| `ggtt.c:257` | GGTT の invalidate。Linux の Gen11 は `gen8_ggtt_invalidate`（`intel_ggtt.c:1277`）だが `needs_wc_ggtt_mapping` が ver ≥ 11 で false（207 行）なので何も書かない。`GEN8_GTCR` は GuC の道だけ（256 行）。ICL は今の Gen12 側（書かない）で正しい（確かめた） |
| `workarounds.c:470` | copy engine の MOCS は版 12 以上だけ。Linux も `GRAPHICS_VER >= 12` の時だけ `gen12_ctx_gt_fake_wa_init` → `gen12_ctx_gt_mocs_init`（`intel_workarounds.c:895,912-913`、確かめた）。正しい |
| `device-info.c:541,543` | context の大きさ。今の code は既に版 11 を 12 と同じ値で受ける（543 行の `!= 11 && != 12`）。枝の追加も要らない |
| `workarounds.c:428` | `graphics_ver < 4`。ICL でも正しい |
| `mmio-record.c:109` | Gen12 の display の register だけを記録する観察の道具。ICL は記録しない（害は無い、記録したいなら別の判断） |

**display 側（WS245 の範囲。p006 の ICL の attach で通る所として載せる）**: `display/survey.c:705`（`!= 12U`）、`display/display.c:1190`、`display/bootfb.c:220`、`display/xd9-flip.c:229`、`display/xd9-backlight.c:112`。ICL の display は WS245 p045（p014 は canceled で p045 へ、`plan/ws245/ws.md:164,215`）。p006 はこれらの所で ICL が正しい枝に入るかを WS245 と確かめる。

### 6.3 見積もりの出し直し（WS229 との比較、レビュー 2 N3）

- WS229 の GT 系（GT・EU・state・Vulkan の通し、display を除く）の見積もりは **8 LW**（`plan/ws229/phase001/phase.md:42`: p002 2.5、p003 2.0、p004 2.0、p005 1.5。全体 14.5 LW は display の 6 と規約の 0.5 を含む）。実績の LW は記録に無く未確認。
- 同じ file の 231 行は ICL の GT を「別の仕事（2〜3 LW）」とした。この差の理由:
  - WS229 p001 の時点では「9 か、それ以外は 12」の構造が無かった。WS229・WS244 の後、§6.2 の grep で版を見る所は GT で約 37 行（render・compiler・display・試験を除く、レビュー 3 の数え方）、compiler・render で約 35 になり、ICL はその全部に第 3 の枝（または確かめ）を要する。2〜3 LW は機能の項目の数（ELSQ・CSB・forcewake・割込み・MOCS・WA・EU・genxml）から出した物で、分岐の数を数えていない。
  - 2〜3 LW は「EU は Gen9 の符号化」で compiler を流用できる前提。今の Gen9 の encoder は Align1 だけで 3 source を持たない（`compiler/eu.c:1366`、`intel/eu-encoding-gen9.h:11`、レビュー 2 の引用、自分では未確認）ので、Gen11 の encoding は Gen9 の物にかなり近いと推測する。この点は 2〜3 LW の側を支える。
- 出し直し: p004 6（GT の core と WA の list）、p005 6（Gen9 の encoder を流用、genxml gen110、render の約 30 か所）、p006 2。ICL は p004〜p006 で 14 LW（p004 の抜き出しの script 1 を含む）、p007 の ICL 分 1〜3 を足して約 15〜17 LW（±50 %）。全部は RKL・ADL-S の 12 と合わせて約 27〜29 LW。版 2 の 22 LW（Gen9 の 8 LW の 2.75 倍）は、Gen9 の道の流用を見込まず多すぎた。Gen9 の GT 系 8 LW の約 1.75 倍で、新しい世代を起こす事と分岐が多い事を足した。
- RKL・ADL-S の 12 LW と WS229 の「各 0.3 LW」（`plan/ws229/phase001/phase.md:231-233`、確かめた）の差（レビュー 3 #6）: 0.3 LW は「ID と VCS2 無し」だけの見積もりで、版 3.1 の 12 LW は次を含む。p002 の 5（内訳は §5: 記述の行、ID の解決の統合、`is_alderlake_p` の削除、gen12-golden の直し、抜き出しの script、5330 の回帰）、p003 の 2（build option と共有の Makefile の調整、MAC-T001、DMC の取得）、p007 の 3（実機の通し）、p008 の 2。0.3 LW に当たるのは p003 の ID の行そのものだけ。p008 の 2 LW は WS229 の 0.5 より大きいが、p004〜p006 を含む時の上限として置いた（RKL・ADL-S だけなら 0.5〜1 に下がる見込み、p008 の初めに見直す）。

## 7. 危険

- ID を足すと display（xd）と GT が同時に初めての platform で走り、GT の失敗は display の引き取りの後に起こる。失敗の切り分けが実機無しではできない（§3 で案 A を既定にする理由。A では試験の image だけで起こる）。
- **ICL は実機が無いと動くかどうか全く分からない**。host の試験は golden の register の列と bit の列だけで、§6.2 の大半（CSB・LRC・emitter・reset・RC6・WA の効き目）は実機でしか確かめられない。ICL の WA の list の誤りは実機でしか分からない種類。
- ICL の compiler は今の 2 つの encoding と別の第 3 の物で、Mesa の disasm との比較でも命令の意味の誤り（latency・依存の扱い）は見つけられない。
- gen12-golden が今の tree で動かない場合、p002 の LW が 1 増える。

## 8. ユーザーの判断が要る点（朝に Q1 がクリックで 1 つずつ聞く。順は D2 → D1-R →（D2=全部の時だけ）D1-I → D3 → D5、レビュー 3 #8）

| # | 問い | 選択肢 | 既定 | 理由 |
| --- | --- | --- | --- | --- |
| D2（先に聞く） | ICL の GT をこの WS でやるか | 実機ができたら全部（p004〜p006 と p007 の ICL 分、約 15〜17 LW。WS 全体は約 27〜29 LW）／Future Work へ移す（WS の題・目標から ICL を外す。WS は約 12 LW）／対象から外す | Future Work へ | ICL は実機無しでは動くか分からない。WS の題を変えるので受け入れ条件も変わる（ws.md） |
| D1-R | RKL・ADL-S/RPL-S に i915 を付けるか（WS245 §7 #18 と合わせて） | A 既定の image では付けない（試験の config.mk の build option で ID を足す、起動の語は増やさない）／B 起動の語で opt-in（語を増やす事も含めて）／C 既定で付ける | A | 実機が無く、GT の失敗は display の引き取りの後。2026-10-18 に起動の語を減らした方向と合う。A でも実機ができれば試験の image で受け入れを通せる |
| D1-I（D2=全部の時だけ） | ICL に i915 を付けるか（WS245 §7 #30 と合わせて） | D1-R と同じ A／B／C | A | 同上。D2 が既定の Future Work なら聞かない |
| D3 | GuC・HuC | 使わない（execlists）／HuC だけ別の WS（video の拡張と一緒） | 使わない | 今の H.264 の decode は HuC を使わない。ADL-P は execlists で動いている |
| D5 | 実機の入手 | 無いまま／RKL・ADL-S（iGPU 有り）の機械を用意／ICL も用意 | 無いまま | **既定は決定の解釈**: 2026-10-19 の「設計まで（実機が無いので p001 で止める）」は実機の入手については何も言っていない。今は無いので止める、と読んだだけ |

- 版 2 の D4（Gen12 の MOCS）は WS252 の外に出した（§2.1、[BUG-335](../bugs/BUG-335.md) として起票済み）。番号は欠番のまま（D5 の番号は変えない）。
- 版 2 の D6（実機の前に p002 を入れるか）は**決定済み**（2026-10-19「設計まで」、`plan/master.md:640`）。朝のクリックでは聞かない。Q1 が入れたい理由を持つ時だけ聞く。

## 9. 未実施・制限

- 版 1・版 2 は design-reviewer を通した（design-review-1.md・design-review-2.md）。版 3 の短い確認（design-review-3.md）は Blocker・Major 無しで、Minor 1〜9 を版 3.1 で当てた。p001 は cleared。
- Linux の値は 6.8.12 で読んだ（版 2 で確かめた分と、版 3.1 でレビュー 3 の scratchpad の tree で開いた `intel_ggtt.c:207,250-258,1275-1278`・`intel_workarounds.c:885-913`・`intel_gt.c:364-373`・`intel_engine_cs.c:1218-1223`・`intel_reset.c:1640-1644`・`intel_renderstate.c:15-31`・`intel_gtt.c:455-461`）。`intel_engine_cs.c:1247,1666-1668`・`intel_reset.c:1633-1639` はレビュー 3 の値で未確認。v6.19 の pciids との差は未確認。
- `plan/ws229/tests/gen12-golden/run.sh` が今の tree で build・PASS するか、Mesa の `brw_asm`・`brw_disasm` が host で build できるかは未確認（試していない）。
- `irq.c:714` の Linux 側（ICL の割込みが Gen11 の系統か）は未確認（`i915_irq.c` を開いていない）。`engine.c:168`・`defaults.c:326`・`workarounds.c:470,1047`・`ggtt.c:257` は版 3.1 で解けた（§6.2）。
- build option の仕組みは前例 `CONFIG_AMDRDNA_CONTROL` がある（§3、確かめた）。共有の Makefile の調整は p003 で Q1 と。
- 抜き出しの script（§4-2）はまだ無い。書けるかは p002 の初めに分かる。
- 実機・QEMU の試験は無し（設計だけ）。

## レビュー 1 への対応（版 2）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| B1 | 直した。案 A は「今は何も入れない、p002 以降は実機まで planned」。実機の前に入れる案は A'（D6）として聞き、その場合の試験は T1 の 5330 の回帰 1 回と 5320 の一覧 | §3・§5・§8 D6 |
| M1 | 直した（`i915_pci.c:419-426`・`device.c:59-60` を確かめた）。DMA の幅を p002 から外し、`memory.h:87` の注記を p002 で直す | §2・§6.1 |
| M2 | 直した（`mocs.h:118-119`、`intel_mocs.c:530-531`、entry 63 は `intel_mocs.c:259-261` で確かめた。レビューの 254-257 は 62 の行）。MOCS の作業は D4 が Linux 側の時だけ | §1・§2.1・§8 D4 |
| M3 | 直した。harness を p002 に（LW 2）、期待は機械的な抜き出しか別の担当、意図した差の列を golden に、QEMU を証拠から外し 5330・5320 に、実機の未確認の列 | §4・§5 |
| M4 | 直した。zedBSD の位置は全部確かめた。Linux の位置は一部だけ確かめ（`*`）、残りは未確認。見積もりを約 25 LW に | §6.2・§6.3・§7 |
| M5 | 直した。記述の field で持つ形に改め、PCI ID の解決を 1 つにする事を p002 の設計に。v6.19 との差は未確認 | §6.1 |
| M6 | 直した（`device.c:424-441`、`Makefile:54-71` の DMC を確かめた）。起動の順・gate の位置・DMC・起動の語の方向 | §3・§5 p003 |
| m1 | 直した（`intel_workarounds.c:2414`、`workarounds.c:166`） | §2 |
| m2 | 直した（`device.c:54,1383-1385`）。vcs2 の言い方を弱めた | §1・§3 |
| m3 | 直した（`display/resident.c:1864`。`modeset.c` は無い事を確かめた） | §3 |
| m4 | 直した（D4 に、p003 は B・C の時だけ） | §3・§5 |
| m5 | 直した（RPL-P は未確認、ADL-S・RPL-S は推測） | §2 要点 2 |
| m6 | 直した（読む所が無い事を grep で確かめた）。p002 で消す | §6.1 |
| m7 | 直した。D の結果ごとの受け入れ条件を ws.md に | ws.md |
| m8 | 直した（`i915_pci.c:626-638` を確かめた） | 冒頭 |

## レビュー 2 への対応（版 3）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| N1 | 直した。案 (a) を採った: D1=A は「既定の image では付けない、試験の config.mk の build option で ID を足す（p003）、起動の語は増やさない」。p003 は D1 のどの値でも行う。p007 の依存を D1・D2 の組み合わせごとの表に。ws.md の受け入れも合わせた。build option の仕組みの有無は未確認 | §3・§5・§8 D1・ws.md |
| N2 | 直した。grep（`graphics_ver` と 9・11・12・`GEN9_GRAPHICS_VER`）の全部の行（試験の `eu-test.c` を除く）を自分で開いて、p004・p005・p006 の「直す」、「正しい（理由）」、display 側に分けた。WA の list（979・988）・whitelist（496）・MOCS の build（738・749）・copy の MOCS（470）・PAT（1033）・GTT の WA（1047）・RPS（1009）を p004 に。`device.c:1316` が forcewake の範囲の選びである事を書いた。`render/state.c` は 18 か所あり、行の全部の列挙は p005 の初めで数え直す | §6.2 |
| N3 | 直した。引用を GT 系 8 LW に。WS229 p001 の 2〜3 LW との差（分岐の数を数えていない／Gen9 の encoder の流用は 2〜3 LW の側を支える）を書き、ICL を 14 LW（通しを含め約 15〜17）に出し直した。WS229 の実績 LW は記録に無く未確認 | §6.3・§5 |
| N4 | 直した。ICL の attach を D1 に含め（RKL・ADL-S/RPL-S と ICL を別々に聞ける形）、p006 の依存に D1（ICL）を書いた | §3・§5・§8 D1 |
| m-a | 直した。新しい harness をやめ、`plan/ws229/tests/gen12-golden/run.sh` を直して使う（Master の試験の一覧への登録は Q1 に依頼）。compiler は Mesa 25.0.7 の `brw_asm`・`brw_disasm`（`--gen=icl`）と `gen110.xml` を使う（source が worktree の `build/mesa-tools/mesa-25.0.7` にある事を確かめた）。build・PASS は未確認。golden の LW を 2 → 1 | §4-1・§4-4・§5 |
| m-b | 直した。3 つ目の表 `i915.c:76-88` の `identifiers[]`（開いて確かめた）を §1・§6.1 と p003 の作業に | §1・§5・§6.1 |
| m-c | 直した。D6 を朝の一覧から外し「決定済み、Q1 が入れたい理由のある時だけ聞く」に。D5 の既定は決定の解釈と書き分けた | §3・§8 |
| m-d | 外す方に揃えた。entry 63 と TGL の MOCS は WS252 の外（Bug の候補）として §2.1 に残し、D4 と p002 の MOCS の作業と ws.md の D4 の受け入れを外した。Bug の起票（5320・5330 の両方の回帰が要る事を含めて）を Q1 に依頼 | §2.1・§8・ws.md |
| m-e | 直した。gate の位置を「`i915_attach` の初め（`i915.c:270-284`、probe の欄は NULL）」に（270-284 を開いて確かめた。probe の欄が NULL なのはレビューの値で未確認） | §3 |
| m-f | 直した。phase.md の結果の要点を版 3 に、ws.md の Phase の表に p002〜p008 を planned で足した。master.md（朝の判断の行・WS 一覧の行）は Q1 への依頼 | phase001/phase.md・ws.md |
| m-g | 直した。p003 が D1 のどの値でも入る形になったので、RKL・ADL-S だけで 12 LW（p002 5・p003 2・p007 3・p008 2）を全部の D1 に共通の値とした | §5 |

## レビュー 3 への対応（版 3.1）

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| 1 | 直した。xd の表は RKL・ADL-S・RPL-S・ICL の ID を既に無条件に持つ（`display-tables-xd.h:1053-1080` を確かめた）。p003・p006 で足すのは `identifiers[]`（`i915.c:80-89`、行を確かめ直した）と `device.c` の 2 つ。p002 の 3 つの表の統合は WS245 と合わせる（Q1 の調整）、つかなければ xd は今のまま | §1・§3・§5・§6.1・ws.md・phase.md |
| 2 | 直した。`ggtt.c:257`・`workarounds.c:470`・`device-info.c:541,543` を「正しい」へ、`device-info.c:245` を第 3 の世代の記述（fault の register）に、`tlb.c:174` の Linux 側を Gen8 の register の組に、`i915.c:407` を `i915_suspend` に、`engine.c:168`・`defaults.c:326`・`workarounds.c:1047` の未確認を解いた（Linux の行は §9 の分だけ自分で開いた） | §6.2・§9 |
| 3 | 直した。`workarounds.c:428` を「正しい」に、grep の型が拾わない `g->gen9`・局所の `gen9` と根の行を一言、除いた試験に draw-fixture.c・draw-test.c と ICL の時の試験の直し | §6.2 |
| 4 | 直した。`render/state.c` は 14 行で全部（`grep -c` で確かめた）。GT の版を見る所は約 37 行（レビュー 3 の数え方、自分では数えていない） | §6.2・§6.3 |
| 5 | 直した。p007 を RKL・ADL-S 3 ＋ ICL 1〜3 に分け、全部を約 27〜29 LW、ICL を約 15〜17 LW（p004〜p006 の 14 ＋ p007 の ICL 分）、D2 の括弧を直した | §5・§6.3・§8・ws.md・phase.md |
| 6 | 直した。p002 の 5 LW の内訳と、WS229 の各 0.3 LW（`ws229/phase001/phase.md:231-233` を確かめた）との差、p008 の 2 LW の理由 | §5・§6.3 |
| 7 | 直した。gen12-golden は新しい platform の列を比べられない事、Linux からの抜き出しの script を p002（0.5）・p004（1）の作業と LW に、§4-4 を既存の 2 つの道具（master.md:1176・1177）の拡張に、Mesa の tree は P1 の worktree にだけある事 | §4・§5 |
| 8 | 直した。D2 を先に聞き、D2=全部の時だけ ICL の D1（D1-I）を聞く。p007 の表を群ごとの D1 の値で書き、値が違う時の image の例を書いた。B・C の p002 は推移的と明記 | §3・§5・§8・ws.md |
| 9 | 直した。build option の前例 `CONFIG_AMDRDNA_CONTROL`（`kernel-options.list:12`・`Makefile:239,628`・`amdrdna/control.c:36-40`、確かめた）、p003 の影響に共有の Makefile・kernel-options.list・MAC-T001 の追従と Q1 の調整。BUG-335 への link。ws.md の Status・前提（WS245 p014 → p045）、phase.md の読んだ物 | §2.1・§3・§5・§8・§9・ws.md・phase.md |
