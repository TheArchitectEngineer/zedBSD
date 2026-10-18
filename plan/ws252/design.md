# WS252 設計: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT

<!-- awesome-plan project=zedbsd record=ws252-design -->

- 版: 1（2026-10-19、P1、q1262、ws252-p001）。design-reviewer は未通過（この Queue では子のエージェントを起動しない指示のため。§9）。
- 範囲: GT（render・copy・video の engine、GuC/HuC の要否、kernel の中の Vulkan の executor `render/` と compiler `compiler/`）。display は [WS245](../ws245/ws.md)。
- ユーザーの決定（2026-10-19、クリック）:「設計まで」。この文書は Phase の計画で止め、実装は実機ができてから。
- この文書は手順の要約で、Linux の code・comment・構造を写さない。出典は節ごとに書く。

## 1. 今の code の形（WS244 の書き直しの後、`src/drivers/gpu/i915/`）

調べた事実（この worktree、基点 main）:

| 点 | 今の形 | 場所 |
| --- | --- | --- |
| 種類の判定 | PCI ID の表は TGL と ADL-P 系（ADL-P・ADL-N・RPL-P・RPL-U）と Gen9 だけ。Gen12 は `graphics_ver=12`、`platform=I915_PLATFORM_GEN12` の 1 種、ADL-P は `is_alderlake_p` の旗だけで区別 | `device.c:750-785`、`device-info.h:78-82` |
| submission | execlists だけ（Gen12 は ELSQ、Gen9 は ELSP）。GuC・HuC は load しない（`firmware.c` は display の DMC の読み込みだけ） | `submit.c:9-14`、`firmware.c` |
| engine | 表は rcs0・bcs0・vcs0・vcs2・vecs0（TGL・ADL-P の mask）。CCS は無い | `intel/engine-table.inc` |
| GT の形 | Gen12 は 1 slice・6 DSS・16 EU を上限に fuse を読む。L3 bank の steering は fuse から | `device-info.c:90-95,256,372-406` |
| context の大きさ | render は Gen11・Gen12 で同じ値、他の版は MISSING_CASE の log | `device-info.c:127,536-544` |
| MOCS | Gen12 は 1 つの表（Linux の `gen12_mocs_table` の値）、global MOCS、unused は index 0 の規則 | `workarounds.c:52-59,708-741`、`intel/mocs-table.inc` |
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
| DMA の幅 | 既定 | 39 bit | 既定 | **39 bit** | 既定 |
| MOCS | Linux は `tgl_mocs_table`（entry 1 が page table の値） | `gen12_mocs_table` | **TGL と同じ `tgl_mocs_table`** | `gen12_mocs_table` | `icl_mocs_table`、**global MOCS が無い**（engine ごと） |
| 既定の uC（Linux） | 使わない | GuC submission + HuC | 使わない | ADL-S: HuC の load だけ、RPL-S: GuC submission + HuC | 使わない（版 < 12） |
| render の WA | Gen12 の共通の list | 共通 + Wa_1607297627 | 共通 + Wa_1607297627 | 共通、**Wa_1607297627 は無い** | 別の list（ICL の gt・ctx・whitelist、`_3D_CHICKEN3`・GARBCNTL など） |
| ELSQ | 有り | 有り | 有り | 有り | 有り（Gen11 から） |
| context の大きさ | 同じ | 同じ | 同じ | 同じ | 同じ（Gen11 と Gen12 は同じ値） |
| EU の ISA | Gen12（SWSB） | 同左 | 同左 | 同左 | **Gen11（Gen9 系の encoding、SWSB 無し、fp64・int64 の扱いが Gen9 と違う）** |

要点:

1. **RKL・ADL-S/RPL-S は今の Gen12 の道に「表の行」を足すだけ**: engine の mask、MOCS の表の選び、Wa_1607297627 の有無、DMA の幅、PCI ID。compiler・genxml・submission は変えない。
2. **GuC/HuC は要らない**: zedBSD は ADL-P・RPL-P で既に Linux の既定（GuC submission）と違って execlists で動いている（実機 5330 の証拠は WS244 の記録）。ADL-S・RPL-S も execlists で足りる。HuC は HEVC の一部・VDEnc の認証に要るが、今の video（`render/video-mfx.c`、H.264 の decode）は HuC を使わない。HuC を足すのは別の WS の判断（§8 D3）。
3. **ICL は「第 3 の版」**: Gen11 の EU encoding、Gen11 の 3D・compute の state、engine ごとの MOCS、ICL の WA、Gen11 の forcewake・MMIO の範囲・reset の domain・fuse・PPAT。Gen9 の道（engine ごとの MOCS など）を流用できる部分はあるが、compiler と genxml は新しい物になる。

### 2.1 今の TGL について見つけた事（この WS の範囲の外、Q1 へ報告）

Linux は TGL・RKL に `tgl_mocs_table`（entry 1 を page table の値にし、未定義の index にその値を入れる）を使い、ADL-P 以降に `gen12_mocs_table` を使う。zedBSD の Gen12 は TGL も ADL-P も `gen12_mocs_table` の値（`intel/mocs-table.inc` の注記「Entries 0 and 1 are left to the platform and Gen12 does not set them」）。TGL の 5320 で表示・描画は動いている（UAT）が、Linux と表が違う。不具合の証拠は無い。直すかは Q1 の判断（Bug か、WS244 の残り）。RKL を足す時（p002）は同じ問いに当たるので、p002 は「TGL・RKL の表を Linux の分け方にそろえるか」をこの判断に従う。

## 3. attach の判断の材料（WS245 §7 #18・#20・#30）

- 今: RKL・ADL-S/RPL-S・ICL は `device.c` の表に無く、i915 は付かず efifb（§7 #18「今のまま（efifb）」）。
- WS245 の改訂 5: display だけの attach は作らない。present は GT の GGTT を要る（`modeset.c:2290`）。よって **ID を足すと GT の初期化も走る**。GT 側の表（この WS の p002）が無いまま ID を足すと、RKL で vcs2 を起こそうとするなど、実機で確かめていない道が走る。
- 実機: 試験の機械（5330 = ADL-P、5320 = TGL、SV7 = Gen9、13400F の desktop = RPL-S だが **F 型で iGPU が無い**）。RKL・ADL-S・RPL-S（iGPU 有り）・ICL の機械は無い。
- 選択肢（ユーザーの判断、§8 D1）:
  - A（既定の案）: 今のまま efifb。p002〜p003 の表と host の試験だけを入れ、ID は足さない。実機ができた時に ID を足す 1 行の Phase。
  - B: ID を足すが、起動の語（例 `i915.untested=1`）で opt-in にする。既定は efifb。実機を持つ利用者が試せる。危険は opt-in の利用者だけ。
  - C: ID を既定で足す。確かめていない道が既定で走る。WS245 §7 #21「全部 xd を既定に」と同じ考え方だが、GT は display より失敗の影響が大きい（GPU hang、reset の失敗で起動が止まる）。推奨しない。

## 4. 実機が無い前提の試験の道

QEMU は Intel の GPU を模倣しない（GVT-g・passthrough も実機を要る）。よって確かめられるのは次だけ。

1. **host の表の試験**（`src/drivers/gpu/i915/tests/` の host 試験の形）: platform ごとに、engine の mask、MOCS の表の選択と値、WA の list（register・mask・値の列）、DMA の幅、context の大きさを列にして、Linux 6.8.12 の値から作った期待の列と比べる。期待の列は Linux から値だけを引用する（データの引用は WS258 の規則に従い出典を block に）。
2. **既存の platform の不変**: TGL・ADL-P の列が変わらない事を同じ host 試験で固定する（今の列を先に golden にする）。これで 5330・5320 の回帰を host で守る。
3. **compiler（ICL のとき）**: Gen11 の encoding を、Mesa の disassembler 相当が無いので、既知の命令の bit の列（Intel の公開の PRM の値）と host で比べる。実行の確認は実機まで「未確認」。
4. **QEMU の起動の回帰**: ID を足さない（案 A）なら i915 は QEMU で付かないので、QEMU は「起動が変わらない」事だけ（T1、WS の最後に 1 回）。
5. **実機**: 「未実施」と書く。実機ができたら、起動・GT の初期化の log・Vulkan の三角形・blit・video の decode の順。

## 5. Phase の分割と LW

（LW は既存の WS の換算。8 LW ≈ 2〜3 時間。実装は実機ができてから、ユーザーの「設計まで」による。）

| Phase | 内容 | 依存 | 試験 | LW |
| --- | --- | --- | --- | --- |
| p001 | この調べと設計 | — | — | 2（済み） |
| p002 | Gen12 の platform の種類を分ける（TGL・ADL-P・RKL・ADL-S・RPL-S）: `kind` に platform を持たせ、engine の mask・MOCS の表・Wa_1607297627・DMA の幅を表で引く。ID は足さない（D1 が A のとき）。TGL・ADL-P の列は不変 | p001、D1・D2（§2.1） | host の表の試験（§4-1・2）、build warning 0 | 4 |
| p003 | attach（D1 が B・C のときだけ）: RKL・ADL-S・RPL-S の PCI ID を `device.c` に、B なら起動の語の gate | p002、WS245 の xd の RKL・ADL-S の行（WS245 p044） | host（ID → 種類の列）、T1 の QEMU の起動の回帰 1 回 | 1 |
| p004 | ICL の GT の土台（D2 が「全部」のとき）: Gen11 の forcewake・MMIO の範囲・reset の domain・fuse・PPAT、engine ごとの MOCS（ICL の表）、ICL の WA の list | p002 | host の表の試験 | 8 |
| p005 | ICL の compiler と state: Gen11 の EU の encoding、Gen11 の 3D・compute の state（genxml の第 3 の版）、`render/` の版の分岐を 9/11/12 に | p004 | host の encoding の試験（§4-3） | 12 |
| p006 | ICL の video（MFX の Gen11 の差）と attach の gate | p005、WS245 p045 | host | 3 |
| p007 | 規約の全文の見直し（WS の変えた C） | 最後の実装の Phase | build、host 試験 | 2 |

合計: RKL・ADL-S だけ（p002・p003・p007）で約 7 LW。ICL まで（p004〜p006 を加える）で約 30 LW。

## 6. 既存の code への影響（p002 の想定、code は書いていない）

- `device.c` の `kind`（`is_alderlake_p` の旗）を platform の値に広げる。`device-info.h` の `I915_PLATFORM_*` に Gen12 の subplatform を足すか、別の field にするかは p002 で決める（Gen9 の条件の関数 `workarounds.c:1234-1300` と同じ形に寄せるのが自然）。
- `intel/engine-table.inc` は行を変えず、platform の mask で vcs2 を除く（RKL）。
- `workarounds.c` の Gen12 の list に platform の条件の列を 1 つ（Wa_1607297627）。
- `render/`・`compiler/` は RKL・ADL-S で変えない。ICL は `graphics_ver` の分岐が「9 か、それ以外は 12」になっている所（`render/batch.c`・`render/blit.c` ほか）を全部 3 値に直す必要がある（p005 で grep の一覧を作る）。

## 7. 危険

- ID を足すと display（xd）と GT が同時に初めての platform で走る。失敗の切り分けが実機無しではできない（§3 で案 A を既定にする理由）。
- ICL の compiler は今の 2 つの encoding と別の第 3 の物で、host の bit 比較だけでは命令の意味の誤り（latency・依存の扱い）を見つけられない。
- §2.1 の MOCS の差は、RKL で Linux に従うと TGL と RKL だけが別の表になり、TGL の今の動作を変えるかの判断を伴う。

## 8. ユーザーの判断が要る点

| # | 問い | 既定の案（判断が無い時） | 選択肢 |
| --- | --- | --- | --- |
| D1 | RKL・ADL-S/RPL-S の ID を足して i915 を付けるか（WS245 §7 #18 と合わせて） | A: 今のまま efifb。表と host 試験だけ | B: 起動の語で opt-in、C: 既定で付ける |
| D2 | ICL の GT をやるか | 後回し（RKL・ADL-S の p002 だけ。ICL は Future Work） | 全部（p004〜p006、約 23 LW 増）、ICL を対象から外す |
| D3 | GuC・HuC | 使わない（execlists、ADL-P と同じ） | HuC だけ（video の拡張と一緒に別の WS） |
| D4 | §2.1 の TGL の MOCS の表を Linux に合わせるか | 変えない（5320 で動いている）、RKL も TGL と同じ zedBSD の表 | Linux に合わせ TGL・RKL を `tgl_mocs_table` の値に（5320 の回帰が要る） |
| D5 | 実機の入手 | 無いまま「設計まで」（2026-10-19 の決定どおり） | RKL・ADL-S（iGPU 有り）の機械を用意 |

## 9. 未実施・制限

- design-reviewer は未通過（Q1 の指示「子のエージェントを起動しない」）。ws.md の p001 の内容は「design-reviewer を通す」なので、p001 は cleared にせず in-progress。Q1 が別に通す。
- Linux の値は 6.8.12 で読んだ。今の code の出典の注記は v6.19 の pciids.h を挙げる物もあり、RKL・ADL-S の ID の表は p003 で版をそろえて引く。
- 実機・QEMU の試験は無し（設計だけ）。
