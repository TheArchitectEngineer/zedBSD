# WS252 設計: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT

<!-- awesome-plan project=zedbsd record=ws252-design -->

- 版: 2（2026-10-19、P1、q1263、ws252-p001）。版 1 は q1262。[design-review-1.md](design-review-1.md)（Blocker 1・Major 6・Minor 8）の対応は末尾の「レビュー 1 への対応」。再レビュー待ち。
- 範囲: GT（render・copy・video の engine、GuC/HuC の要否、kernel の中の Vulkan の executor `render/` と compiler `compiler/`）。display は [WS245](../ws245/ws.md)。
- 範囲外: Gen11 の兄弟の EHL・JSL（engine 4 つ、ppgtt 36 bit。Linux 6.8.12 `i915_pci.c:626-638` の `ehl_info`・`jsl_info`）。ICL の ID を足す時に混ぜない。
- ユーザーの決定（2026-10-19、クリック）:「設計まで」。この文書は Phase の計画で止め、実装は実機ができてから。
- この文書は手順の要約で、Linux の code・comment・構造を写さない。出典は節ごとに書く。

## 1. 今の code の形（WS244 の書き直しの後、`src/drivers/gpu/i915/`）

調べた事実（この worktree、基点 main）:

| 点 | 今の形 | 場所 |
| --- | --- | --- |
| 種類の判定 | PCI ID の表は TGL と ADL-P 系（ADL-P・ADL-N・RPL-P・RPL-U）と Gen9 だけ。Gen12 は `graphics_ver=12`、`platform=I915_PLATFORM_GEN12` の 1 種、ADL-P は `is_alderlake_p` の旗だけで区別 | `device.c:750-785`、`device-info.h:78-82` |
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

1. **RKL・ADL-S/RPL-S は今の Gen12 の道に「表の行」を足すだけ**: engine の mask（RKL の vcs2）、Wa_1607297627 の有無（ADL-S・RPL-S で外す。今の `workarounds.c:166` は全部の Gen12 に無条件）、PCI ID。MOCS の表の選びは D4 が「Linux に合わせる」の時だけ。DMA の幅は差が無い。compiler・genxml・submission は変えない。
2. **GuC/HuC は要らない（推測を含む）**: zedBSD は ADL-P で Linux の既定（GuC submission）と違って execlists で動いている（実機 5330 の証拠は WS244 の記録。RPL-P の機械は無く未確認）。ADL-S・RPL-S も execlists で足りるのは、Linux が `enable_guc=0` で execlists に戻れる事からの推測（`gt/uc/intel_uc.c:33-56` の既定の表は確かめた）。HuC は HEVC の一部・VDEnc の認証に要るが、今の video（`render/video-mfx.c`、H.264 の decode）は HuC を使わない。HuC を足すのは別の WS の判断（§8 D3）。
3. **ICL は「第 3 の版」**: Gen11 の EU encoding、3D・compute の state、CSB・LRC・request の emitter、PTE・PPAT、engine ごとの MOCS、ICL の WA、forcewake・MMIO の範囲・reset の domain・fuse。一覧は §6.2。

### 2.1 Gen12 の MOCS の Linux との差（この WS の範囲の外、Q1 へ報告）

- Linux は TGL・RKL に `tgl_mocs_table` を、ADL-P 以降に `gen12_mocs_table` を使う。分けた理由は Linux の注記「For TGL/RKL, Can't be changed now for ABI reasons」（`gt/intel_mocs.c:530-531`）で、user の ABI を守るためで hardware の要求ではない。zedBSD の render は uncached の index 3 しか使わず（`render/heap.h:194-199`）、index 3 は 2 つの表で同じ値。よって TGL の表の差は不具合の候補とは言えない（hardware が index 0・1・未定義の index を自分で使う事が無ければ。未確認）。
- 全部の Gen12 に効く差: Linux の `GEN11_MOCS_ENTRIES` には entry 63（L3 UC、`gt/intel_mocs.c:259-261`。レビューの 254-257 は 62 の行）があるが、zedBSD は 63 の行を持たず unused の値（L3 WB）を入れる（`intel/mocs-table.inc:12-13`）。使う index ではないので影響は未確認。
- どちらも §8 D4 で聞く。試験の golden は「Linux と違う事を意図した行」としてこの 2 つを列に持つ（§4）。

## 3. attach の判断の材料（WS245 §7 #18・#20・#30）

- 今: RKL・ADL-S/RPL-S・ICL は `device.c` の表に無く、i915 は付かず efifb（§7 #18「今のまま（efifb）」）。
- WS245 の改訂 5: display だけの attach は作らない。present は GT の GGTT を要る（`display/resident.c:1864`）。よって **ID を足すと GT の初期化も走る**。
- 起動の順: `drv_i915_device_start` は display の start・outputs（`device.c:424-438`）を GT の start（`device.c:441`）より先に行う。初めての platform で GT が失敗すると、display は firmware から引き取られた後で、画面・console の振る舞いは未確認（推測: 画面が消えたまま node が出ない）。
- engine: RKL の vcs2 は media の fuse で落ちる可能性がある（`device-info.c:273`、RKL の fuse の報告は未確認）。
- DMC: image に入る DMC は `adlp_dmc.bin`・`tgl_dmc_ver2_12.bin` だけ（`userland/firmware/i915/Makefile:54-71`）。xd の表が名指しする `rkl_dmc_ver2_03.bin`・`adls_dmc_ver2_01.bin`・`icl_dmc_ver1_09.bin` は入らない。attach の Phase の依存に取得と照合を入れる。
- 実機: 5330 = ADL-P、5320 = TGL、SV7 = Gen9、13400F の desktop = RPL-S だが **F 型で iGPU が無い**。RKL・ADL-S・RPL-S（iGPU 有り）・ICL の機械は無い。
- 選択肢（§8 D1・D6）:
  - **A（既定）: 今は何も入れない**。p002 以降は実機を得るまで planned（2026-10-19「設計まで」）。ID も表も足さない。
  - A'（D6 で聞く）: 実機の前に p002（platform の記述の行、ID は足さない）だけ入れる。動いている ADL-P・TGL の道を変えるので、試験に T1 の 5330 の回帰 1 回を入れ、5320 はユーザーの試験の一覧に載せる。
  - B: ID を足し、起動の語で opt-in。gate は `i915.c:271-283` の `i915.gen9` と同じく device start の前の probe に置く。ユーザーは 2026-10-18 に `i915.gen9.display=` の語を廃止した（`plan/master.md:655`）ので、起動の語を増やす事自体を聞く。
  - C: ID を既定で足す。確かめていない GT の道が既定で走り、GT の失敗は display の引き取りの後に起こる。推奨しない。
  - B・C で「display の引き取りの前に GT を確かめる」順の変更をするかは、attach の Phase の設計で決める（WS245 の範囲と重なる）。

## 4. 実機が無い前提の試験の道

QEMU は Intel の GPU を模倣しない（GVT-g・passthrough も実機を要る）。QEMU では i915 が付かないので、QEMU の起動は p002 以降の変更を通らず、受け入れの証拠にしない。

1. **host の harness（新しく作る、p002 の作業）**: 今の host 試験（`tests/contracts/run.sh:37-55`）には `workarounds.c`・`device-info.c`・`device.c` の platform の判定を link する物が無い。WA・MOCS の照合は実機の上の kernel 内の試験（`tests/execution/ktest-gt.c:896-945`）だけ。p002 で、mock の MMIO に WA・MOCS の書き込みを記録する harness と、PCI ID → platform の記述の解決を `device.c` から切り出した単位を作る。
2. **期待の列**: Linux 6.8.12 の source から機械的に抜き出す（script で表を取り出す）か、実装と別の担当が照合する。実装する人が Linux を読んで手で書いた期待だけにしない（同語反復を避ける）。
3. **既存の platform の不変**: TGL・ADL-P の今の列を golden に固定。golden は「Linux と違う事を意図した行」（§2.1 の TGL の MOCS、entry 63）を列に持ち、Linux の期待と食い違う所はそこだけと示す。
4. **compiler（ICL のとき）**: Gen11 の encoding を既知の命令の bit の列（Intel の公開の PRM の値）と host で比べる。意味（latency・依存）の誤りは捉えられない。
5. **5330・5320**: 動いている道を変える Phase（A' の p002 など）は T1 の 5330 の回帰 1 回（起動・内蔵 LCD・desktop）、5320 はユーザーの試験の一覧へ。
6. **実機で初めて分かる項目（全部未確認）**: RKL・ADL-S の fuse（2 DSS、L3 bank の数）、MCR の steering の先、WA の read-back（`verify-workarounds.c`）、forcewake の ack、GT が失敗した時の画面。実機ができたら、起動・GT の初期化・Vulkan の三角形・blit・video の decode の順。

## 5. Phase の分割と LW

（LW は既存の WS の換算。8 LW ≈ 2〜3 時間。**p002 以降は実機を得るまで planned**（2026-10-19「設計まで」）。D6 で A' を選んだ時だけ p002 を先に入れる。）

| Phase | 内容 | 依存 | 試験 | LW |
| --- | --- | --- | --- | --- |
| p001 | この調べと設計 | — | design-reviewer | 2 |
| p002 | Gen12 の platform の記述の行（TGL・ADL-P・RKL・ADL-S・RPL-S）: PCI ID の解決 1 つから engine の mask・Wa_1607297627 の有無・名前を引く（§6.1）。MOCS の表の選びは D4 が Linux 側の時だけ。ID は足さない。host の harness を作る | p001、実機（または D6=A'）、D4 | §4-1〜3、build warning 0、T1 の 5330（§4-5） | 6（harness 2 を含む） |
| p003 | attach（D1 が B・C のときだけ）: RKL・ADL-S・RPL-S の ID、B なら probe の gate、DMC の firmware の取得と照合 | p002、WS245 の xd の行、実機 | host（ID → 記述）、実機 | 2 |
| p004 | ICL の GT の core（D2 が「全部」のとき）: §6.2 の GT の core の各所 | p002、実機 | host、実機 | 10 |
| p005 | ICL の compiler と state: Gen11 の EU の encoding、genxml の第 3 の版、`render/` の分岐 | p004 | §4-4、実機 | 9 |
| p006 | ICL の video・PXP の除外・attach の gate・ICL の DMC | p005、WS245 p045 | host、実機 | 3 |
| p007 | 実機の通し（Vulkan・blit・video、5330 の回帰） | p003 または p006、実機 | 実機、T1 の 5330 | 3 |
| p008 | 規約の全文の見直し（WS の変えた C） | 最後の実装の Phase | build、host 試験 | 2 |

合計: RKL・ADL-S だけ（p002・p003・p007・p008）で約 13 LW。ICL まで（p004〜p006 を加える）で約 35 LW。見積もりの根拠は §6.3。

## 6. 既存の code への影響（code は書いていない）

### 6.1 RKL・ADL-S（p002）

- platform の差は、WS245 の方針（`plan/ws245/design.md:412`「記述の field で書き、`kind` で分岐しない」）に合わせ、記述の行の field（engine の mask、Wa_1607297627 の bit、MOCS の表、名前）で持つ。版 1 の「Gen9 の条件の関数に寄せる」は取り下げる。
- PCI ID の解決は 1 つにする。xd の表（`intel/display-tables-xd.h:1035-1085`、Linux 6.8.12 の `i915_pciids.h` と一致とレビューが確認）と `device.c` の表（`intel/pci-ids.h`、v6.19 から引用）が別にある。GT の記述を xd の解決と共有するか、1 つの表から両方を引く形を p002 で決める。v6.19 と 6.8.12 の ID の差は未確認。
- `gt->is_alderlake_p`（`device.c:1171`、`gt.h:99`）は読む所が無い（grep で確かめた）。p002 で消す。display の DMC の判定（`display/dmc-shell.c:110-112,338`）は display 側の `dev->is_alderlake_p` で、別物。
- `memory.h:87` の注記「on Alder Lake-P」は全部の版に当たる値なので、p002 で直す（M1）。
- `render/`・`compiler/` は RKL・ADL-S で変えない。

### 6.2 ICL（p004〜p006）で「9 か、それ以外は 12」の分岐を直す所

今の zedBSD の位置は全部この worktree で確かめた。Linux の位置はレビューの値で、`*` を付けた物は自分で読んで確かめた（他は未確認）。

| 項目 | zedBSD の位置 | Linux 6.8.12 で Gen11 の扱い | Phase |
| --- | --- | --- | --- |
| CSB の解き方（ELSQ + Gen8 の CSB の第 3 の形） | `submit.c:525` | `gen8_csb_parse`（`intel_execlists_submission.c:1911-1915`*） | p004 |
| LRC の layout・indirect ctx | `context.c:392`、`intel/lrc-offsets.h` | `gen11_rcs_offsets`・`gen9_xcs_offsets`（`intel_lrc.c:682-718,800-825`） | p004 |
| request の emitter | `request.c:379` | `gen11_emit_flush_rcs` ほか（`intel_execlists_submission.c:3456-3459,3526-3531`） | p004 |
| PTE・PAT・PPAT | `ggtt.c`・`ppgtt.c`・`workarounds.c` の Gen12 の PAT | `gen8_pte_encode`・`icl_setup_private_ppat` | p004 |
| GGTT の invalidate | `ggtt.c:257` | GEN8_GTCR | p004 |
| TLB | `tlb.c:174` | Wa_2207587034 の対象外 | p004 |
| reset | `reset.c:388` | Gen11 の domain | p004 |
| RC6・RPS | `gt-power.c:178` | 版の分岐（`intel_rps.c`・`intel_rc6.c`） | p004 |
| SSEU・fuse（8 subslice × 8 EU） | `device-info.c:245` | Gen11 の形 | p004 |
| engine ごとの MOCS（VCS2 の base が無い） | `workarounds.c:865-880` | `__GEN11_VCS2_MOCS0`（`intel_mocs.c:625`*、`intel_gt_regs.h:1257`*） | p004 |
| device の版の分岐 | `device.c:1316,1347,1384,2295,2413,2500` | — | p004 |
| PXP（Gen12 だけ。今は「9 でなければ初期化」） | `device.c:1965` | `has_pxp` は GEN12_FEATURES（`i915_pci.c:645`*） | p006 |
| video の worker・申告 | `worker.c:360`、`render/vulkan.c:107` | MFX の Gen11 の差 | p006 |
| device の名前 | `render/instance.c:307` | — | p005 |
| EU の encoding | `compiler/eu.c:532-544` | Gen11 | p005 |
| 3D・compute の state | `render/batch.c:124,162`、`render/blit.c:106-108`、genxml | Gen11 | p005 |

### 6.3 見積もりの出し直し（WS229 との比較）

- WS229（Gen9 の GT・EU・state）は見積もり 14.5 LW（ws229 p001）。p002〜p004 は host の受け入れまで済み、通しは実機の p005 で cleared になった。実績の LW は記録に無く未確認。
- ICL は Gen9 と同じ量（GT の core・EU・state の第 3 の版）に、CSB の第 3 の形・PXP の除外・engine ごとの MOCS の VCS2 が足される。Gen9 の道（engine ごとの MOCS、Gen8 の CSB の考え方）を一部流用できる。よって ICL（p004〜p006）は 22 LW、実機の通しを含めて約 25 LW とする（版 1 の 23 LW は根拠が無かった）。

## 7. 危険

- ID を足すと display（xd）と GT が同時に初めての platform で走り、GT の失敗は display の引き取りの後に起こる。失敗の切り分けが実機無しではできない（§3 で案 A を既定にする理由）。
- **ICL は実機が無いと動くかどうか全く分からない**。host の試験は表と bit の列だけで、§6.2 の大半（CSB・LRC・emitter・reset・RC6）は実機でしか確かめられない。
- ICL の compiler は今の 2 つの encoding と別の第 3 の物で、host の bit 比較だけでは命令の意味の誤り（latency・依存の扱い）を見つけられない。
- §2.1 の MOCS の差は、RKL で Linux に従うと TGL と RKL だけが別の表になり、TGL の今の動作を変えるかの判断を伴う。

## 8. ユーザーの判断が要る点（朝に Q1 がクリックで 1 つずつ聞く）

| # | 問い | 選択肢 | 既定 | 理由 |
| --- | --- | --- | --- | --- |
| D1 | RKL・ADL-S/RPL-S に i915 を付けるか（WS245 §7 #18 と合わせて） | A 付けない（efifb）／B 起動の語で opt-in（語を増やす事も含めて）／C 既定で付ける | A | 実機が無く、GT の失敗は display の引き取りの後。2026-10-18 に起動の語を減らした方向とも合う |
| D2 | ICL の GT をこの WS でやるか | 実機ができたら全部（p004〜p006、約 25 LW）／Future Work へ移す（WS の題・目標から ICL を外す）／対象から外す | Future Work へ | ICL は実機無しでは動くか分からない。WS の題を変えるので受け入れ条件も変わる（ws.md） |
| D3 | GuC・HuC | 使わない（execlists）／HuC だけ別の WS（video の拡張と一緒） | 使わない | 今の H.264 の decode は HuC を使わない。ADL-P は execlists で動いている |
| D4 | Gen12 の MOCS を Linux に合わせるか（§2.1: TGL・RKL の表、entry 63） | 変えない／entry 63 だけ合わせる／TGL・RKL も `tgl_mocs_table` に | 変えない | Linux の分け方は ABI のためで、zedBSD は index 3 しか使わない。合わせると 5320 の回帰が要る |
| D5 | 実機の入手 | 無いまま（設計まで）／RKL・ADL-S（iGPU 有り）の機械を用意／ICL も用意 | 無いまま | 2026-10-19 の決定 |
| D6 | 実機の前に p002（記述の行と host の harness、ID は足さない）だけ入れるか | 入れない（planned のまま）／入れる（T1 の 5330 の回帰 1 回、5320 は一覧） | 入れない | 恩恵のある機械が無いのに、動いている ADL-P・TGL の道を変える |

## 9. 未実施・制限

- 版 1 は design-reviewer を通した（design-review-1.md）。版 2 の再レビューは未実施。p001 は cleared にせず in-progress。
- Linux の値は 6.8.12 で読んだ（版 2 の確かめはレビューが展開した scratchpad の tree を読んだだけ）。v6.19 の pciids との差は未確認。
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

