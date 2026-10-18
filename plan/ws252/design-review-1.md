<!-- design-reviewer の報告（2026-10-19、design.md 版 1、main 9a810240d に対して）。Q1 が報告の本文をそのまま置いた -->

# WS252 design.md（版 1）の敵対的レビュー

対象は /home/awe/zedBSD-claude1/plan/ws252/design.md です。今の main（9a810240d、clean）の `src/drivers/gpu/i915/` と照らしました。Linux 6.8.12 は必要な file だけを /tmp/claude-1000/-home-awe-zedBSD-claude1/70c47280-c9e2-5e3f-b5d9-072cd0539dfc/scratchpad/lx/ に展開して読みました（削除はしていません。片付けは Q1 に任せます）。Mesa 25.0.7 は既に展開済みの /home/awe/zedBSD-worktrees/p1/build/mesa-tools/mesa-25.0.7 を読むだけにしました。source・plan の変更、commit、build、QEMU、実機はどれもしていません。

**結論**: RKL・ADL-S/RPL-S が「今の Gen12 の道に表の行を足すだけ」で済む、という大筋は Linux と合っています。engine の mask、MOCS の選び方、uC の既定、Wa_1607297627 の platform も合っています。ただし次の 3 点があり、p001 をこのまま cleared にはできません。
- 表の一部に事実の誤りがあります（DMA の幅、MOCS の規則、TGL の WA）。
- 試験の道（§4）は今の tree では成り立ちません。
- 既定の案がユーザーの決定「設計まで」と食い違っています。

ICL は変える所の一覧が大きく欠けています。

---

## Blocker

### B1. 既定の案 A と D2 の既定が、ユーザーの決定「設計まで（p001 で止める）」と食い違う。しかも p002 は動いている道を host 試験だけで変える
- **根拠**:
  - ユーザーの決定は `plan/master.md:640`「WS252 は「設計まで」（実機が無いので p001 で止める）」と `plan/ws252/ws.md` の決定の行です。
  - design §3 の案 A は「p002〜p003 の表と host の試験だけを入れ」、§8 D2 の既定は「RKL・ADL-S の p002 だけ」です。
  - p002 が変える物（§5・§6）: `kind`・`gt->info.platform`（`device.c:762-785,1171-1172`）、WA の Gen12 の条件（`workarounds.c:166` の Wa_1607297627 は今は全部の Gen12 に無条件）、MOCS の選び方（`workarounds.c:708-760`）、engine の mask（`device.c:54,1383-1385`）。どれも 5330（ADL-P）と 5320（TGL）で今動いている道です。
  - それなのに §5 の p002 の試験は「host の表の試験、build warning 0」だけです。
- **何が起こるか**: 判断が無いまま既定で進めると、承認されていない実装の Phase が予定に入ります。入れば、恩恵のある機械が 1 台も無い変更で ADL-P・TGL の起動の道が変わります。そのうえ実機の回帰が試験の列にありません（guardrail の i915 の方針「5330 で動くと確かめていない物を main に入れない」、`plan/guardrail.md:129-133` の趣旨）。
- **修正案**:
  - (a) §3・§5・§8 に「p002 以降は実機を得るまで planned のまま。設計まで（2026-10-19）」と書きます。案 A の意味を「今は何も入れない」に直します。
  - (b) 実機の前に p002 を入れるなら、そうしたいとユーザーに改めて聞きます。その場合は p002 の試験に T1 の 5330 の回帰 1 回と、5320 をユーザーの一覧に載せる事を加えます。

## Major

### M1. DMA の幅の行が誤り。全部 39 bit で、表で引く理由が無い
- **根拠**:
  - Linux では `i915_pci.c:419-426` の GEN8_FEATURES が `.dma_mask_size = 39` を持ち、GEN9_FEATURES（483-487）、GEN11_FEATURES（612-617）、GEN12_FEATURES（640-646）がそれを引き継ぎます。tgl_info・rkl_info・icl_info（619-660）の「既定」も 39 bit です。adl_s・adl_p（683-697）の 39 は同じ値を書き直しているだけです。
  - zedBSD は全部の版で 39 bit を固定しています（`device.c:59-60,1437-1462`、`memory.h:87-88`）。
- **何が起こるか**: design §2 の表（TGL・RKL・ICL「既定」、ADL-S「**39 bit**」）と、§2 の要点 1・§5 の p002「DMA の幅を表で引く」が、要らない分岐と、誤った期待の列（§4-1）を作ります。
- **修正案**: 行を「全部 39 bit（差は無い）」に直します。p002 から DMA の幅を外します。`memory.h:87` の注記「on Alder Lake-P」は直すと良い（範囲外なら一言だけ）。

### M2. MOCS の記述の誤りと、§2.1 の見立ての弱さ
- **規則の誤り**: §1 の MOCS の行「unused は index 0 の規則」は誤りです。今の code は uncached が index 3、unused が index 2 です（`intel/mocs.h:118-119`、`workarounds.c:708-760`）。
- **TGL と RKL の表の差**: Linux が TGL・RKL に `tgl_mocs_table` を使うのは「Can't be changed now for ABI reasons」（`gt/intel_mocs.c:530-531`）のためです。user の ABI を守るための分け方で、hardware が求める物ではありません。zedBSD の render は uncached の index 3 しか使いません（`render/heap.h:194-199`、`render/*.c` の `drv_i915_gfx_mocs_uncached`）。index 3 は 2 つの表で同じ値です。よって D4 の既定「変えない」は強く支えられます。§2.1 の「不具合の候補」という書き方は弱めてよいです（推測: index 0・1・未定義の index を hardware が自分で使う事があれば話は別。確かめていません）。
- **本当に効く差**: 全部の Gen12 で entry 63 が Linux と違います。Linux 6.8.12 の `GEN11_MOCS_ENTRIES` には `MOCS_ENTRY(63, ..., L3_1_UC)` があります（`intel_mocs.c:254-257`）。zedBSD は 63 の行を持たず、unused の index 2 の値（L3 は **WB**）を入れます（`intel/mocs-table.inc` の注記）。§4-1（Linux の値と比べる）と §4-2（今の列を golden に）は、ADL-P でもここで必ず食い違います。design はこれを書いていません。
- **要点の矛盾**: D4 の既定どおりなら Gen12 の表は 1 つのままです。その場合、§2 の要点 1 と p002 の「MOCS の表の選び」は空の作業になります。
- **修正案**:
  - §1 の index を直します。
  - §2.1 に Linux の ABI の注記を引きます。
  - entry 63 の差を D4（または新しい項）に足します。
  - p002 の MOCS の作業は「D4 が Linux に合わせる時だけ」にします。

### M3. 試験の道（§4）が今の tree では成り立たない。成り立っても誤りを見つけにくい
- **host の試験の土台が無い**:
  - WA・MOCS・PAT の照合は、今は hardware の上で動く kernel 内の試験 `tests/execution/ktest-gt.c:896-945`（「ADL-P workaround manifest」）だけです。
  - host の試験 `tests/contracts/run.sh:37-55`（mmio・dma・pci・rpm・pte・sync・rps・memory・forget・reset）には `workarounds.c`・`device-info.c`・`drv_i915_product_kind`（2505 行の `device.c` の中）を link する物がありません。
  - §4-1 の「host 試験の形」は新しい harness（mock_mmio で WA の list を記録する、`product_kind` を `device.c` から切り出す等）を要します。§5 の 4 LW には入っていません。
- **§4-1 と §4-2 が矛盾する**: TGL の MOCS（M2）と entry 63 の 2 か所で、「Linux の期待」と「今の golden」が必ずぶつかります。どちらを正とするかが書かれていません。
- **期待の列が同語反復になる**: 期待の列を、実装する人と同じ人が Linux を読んで作ります。読み違えは code と期待の両方に入り、試験は通ってしまいます。
- **QEMU では何も見えない**: §4-4・p003 の「QEMU の起動の回帰」は、QEMU では i915 がそもそも付かないので、p002・p003 の変更を一切通りません。
- **host では捉えられない誤り**: 例えば次の物です。
  - RKL・ADL-S の fuse の読み方（2 DSS、少ない L3 bank）
  - MCR の steering の先
  - 実機で WA の read-back が保たれるか（`verify-workarounds.c`）
  - forcewake の ack
- **修正案**:
  - p002 に host の harness の作業と LW を足します。
  - 期待の列は、Linux の source から機械的に抜き出すか、別の担当が照合する形にします。
  - 「Linux と違う事を意図した行」（TGL の MOCS・entry 63）の一覧を golden に持たせます。
  - QEMU の回帰を受け入れの証拠から外し、5330 の T1 と 5320 の一覧に替えます。
  - 実機で初めて分かる項目を「未確認」として列にします。

### M4. ICL の範囲の見積もりが「9 か、それ以外は 12」の分岐の大半を見落としている
- **今の分岐の形**: §6 は render/ と compiler/ の分岐だけを挙げます。しかし GT の core も同じ二値の分岐で、ICL（graphics 11）を入れると黙って Gen12 の道に入ります。
- **Linux で Gen11 が Gen12 と違う所**（zedBSD の今の分岐の位置）:
  - CSB の解き方: Gen11 は `gen8_csb_parse`（`intel_execlists_submission.c:1911-1915`）。zedBSD は `submit.c:525` が Gen9 か Gen12 かの二択。ICL は「ELSQ で出す + Gen8 の CSB」の第 3 の layout になる。§2 の「ELSQ 有り」だけでは足りない。
  - LRC の layout: ICL は `gen11_rcs_offsets` と `gen9_xcs_offsets`（`intel_lrc.c:682-718`）、indirect ctx の offset 0xaa と GEN11 の既定（800-825）。zedBSD は `context.c:392`、`intel/lrc-offsets.h`。
  - request の emitter: `gen11_emit_flush_rcs`・`gen11_emit_fini_breadcrumb_rcs`、xcs は gen8 の物（`intel_execlists_submission.c:3456-3459,3526-3531`）。zedBSD は `request.c:379`。
  - PTE の encode と cache level → PAT: Gen11 は `gen8_pte_encode`・`LEGACY_CACHELEVEL`（`gen8_ppgtt.c:1040-1043`、`i915_pci.c:46-60`）。PPAT は `icl_setup_private_ppat`（`intel_gtt.c:684-687`）。zedBSD は `ggtt.c`・`ppgtt.c`・`workarounds.c` の Gen12 の PAT。
  - GGTT の invalidate: GEN8_GTCR（`intel_ggtt.c:250-256`）。zedBSD は `ggtt.c:257`。
  - TLB: Gen11 は Wa_2207587034 の対象外（`intel_tlb.c:86-92`）。zedBSD は `tlb.c:174` が Gen12 の layout を当てる。
  - RC6・RPS: GEN12_RPSTAT1 などの版の分岐（`intel_rps.c:2067-2108`、`intel_rc6.c:136-153`）。zedBSD は `gt-power.c:178`。
  - PXP: Linux では Gen12 だけ（`i915_pci.c:645`）。zedBSD は `device.c:1965` が「9 でなければ初期化」なので、ICL で PXP を作ってしまう。
  - そのほかの二値の分岐: `reset.c:388`、`worker.c:360`（video）、`render/vulkan.c:107`（video の申告）、`render/instance.c:307`（名前）、`device.c:1316,1347,1384,2295,2413,2500`、`device-info.c:245`（SSEU・fuse は Gen11 が 8 subslice × 8 EU の形）。
  - engine ごとの MOCS の VCS2 の base（Linux の `__GEN11_VCS2_MOCS0`、`intel_mocs.c:653`）が `workarounds.c:865-880` に無い。
- **見積もりの根拠**: 23 LW には、比べる元の根拠がありません。Gen9 の GT と EU（WS229）は見積もり 14.5 LW で、実機（p005）があってやっと通りました。
- **修正案**:
  - p004 の一覧に上の各項目を file と行の付きで入れます（§6 で後回しにしている「grep の一覧」は今作れます）。
  - 見積もりは WS229 の実績と比べて出し直します。
  - D2 で示す LW を直します。
  - 実機が無いと ICL は「動くかどうか全く分からない」事を、§7 に明記します。

### M5. PCI ID の表が 3 つ目になる。WS245 の「記述の field で分ける」方針ともずれる
- **根拠**:
  - xd には RKL・ADL-S・RPL-S・ICL の ID が既にあります（`intel/display-tables-xd.h:1035-1085`、Linux 6.8.12 から引用）。これは Linux の `i915_pciids.h:565-583,623-629,640-648,678-686` と一致する事を確かめました。
  - `device.c` の ID（`intel/pci-ids.h`）は v6.19 から引用しています。
  - §9 は「p003 で版をそろえて引く」と言い、どちらの表を正にするかを決めていません。
  - WS245 design（`plan/ws245/design.md:412`）は「platform の差は記述の field で書き、`kind` で分岐しない」と決めています。design §6 の「Gen9 の条件の関数（`workarounds.c:1234-1300`）と同じ形に寄せる」はこれと逆向きです。
- **何が起こるか**: GT 側と display 側で ID の集合や platform の判定がずれる事がありえます（推測。例: GT は付くのに xd の行が無い ID）。
- **修正案**:
  - GT の記述の行（engine の mask、MOCS の表、WA の bit、名前）を 1 つの PCI ID の解決（xd の表を使うか共有する）から引く形を、p002 の設計に書きます。
  - v6.19 と 6.8.12 の ID の差は確かめていない、と記録します。

### M6. attach の危険の見立て（§3・§7）に、起動の順と firmware が抜けている
- **起動の順**:
  - `drv_i915_device_start` は display の start・outputs（`device.c:424-438`）を GT の start（441）より先に行います。初めての platform で GT が失敗すると、display はもう firmware から引き取られた後です。console・efifb がどうなるかは確かめていません（推測: 画面が消えたまま node が出ない）。
  - opt-in の gate（案 B）は `i915.c:271-283` の `i915.gen9` と同じく、device start の前の probe に置く必要があります。
  - ユーザーは 2026-10-18 に `i915.gen9.display=` の語を廃止しています（`plan/master.md:655`）。起動の語を増やす案 B は、この方向と合わせてユーザーに聞くべきです。
- **DMC の firmware**: image に入る DMC は ADL-P と TGL だけです（`userland/firmware/i915/Makefile:54-71`）。xd の表が名指しする `rkl_dmc_ver2_03.bin`・`adls_dmc_ver2_01.bin`・`icl_dmc_ver1_09.bin`（`display-tables-xd.h:2927,2979,3031,3295`）は入りません。p003 の依存にありません。
- **修正案**: §3 の案 B・C に「display の引き取りの前に GT を確かめるか」「gate の位置」「DMC の firmware の取得と照合」を足します。

## Minor

- **m1. TGL の render の WA の行**: Linux は Wa_1607297627 を TGL にも当てます（`intel_workarounds.c:2414` の `IS_ROCKETLAKE || IS_TIGERLAKE || IS_ALDERLAKE_P`）。zedBSD も全部の Gen12 に無条件で当てています（`workarounds.c:166`）。行を「共通 + Wa_1607297627」に直します。ADL-S だけが外れる、という結論は正しいです。
- **m2. engine の mask の場所**: mask は `intel/engine-table.inc` ではなく `device.c:54` の `I915_START_ENGINE_MASK` と `device.c:1383-1385` の選び方にあります。media の fuse が mask から engine を落とします（`device-info.c:273`）。§3 の「RKL で vcs2 を起こそうとする」は言い過ぎかもしれません（推測: RKL の fuse が VDBOX2 を無効と報告するなら落ちる）。
- **m3. 古い参照**: `modeset.c:2290` という file は今はありません。該当の文は `display/resident.c:1864` です。
- **m4. 内部の食い違い**: §5 の p002 の依存「D1・D2（§2.1）」は D4 の誤りです。§3 の案 A の「p002〜p003」も、p003 が B・C の時だけという定義と食い違います。
- **m5. D3 の根拠**: 「ADL-P・**RPL-P** で execlists で動いている」のうち、RPL-P の試験の機械はありません（試験の機械は 5330・5320・SV7・13400F）。ADL-S・RPL-S で execlists で足りるのは Linux の `enable_guc=0` からの推測、と書きます。uC の既定の表そのもの（`gt/uc/intel_uc.c:33-56`）は合っています。
- **m6. 呼び手の一覧**: `gt->is_alderlake_p`（`device.c:1171`、`gt.h:99`）を読む所はありません。display の DMC の判定（`display/dmc-shell.c:110-112,338`）は xd の kind から来ます。§6 に、この旗を消すか置き換えるかと、実際の呼び手を書くと良いです。
- **m7. WS の受け入れ条件**: ws.md に完了の条件がありません。D2 の既定（ICL を Future Work へ）は WS の題（ICL を含む）と目標を変えます。D1・D2 の結果ごとの受け入れ条件を設計に置きます。
- **m8. 範囲の注記**: Gen11 の兄弟（EHL・JSL: 4 engine、ppgtt 36 bit、`i915_pci.c:626-638`）を範囲外と一行書くと、後で ID を足す時に混ざりません。

## 確かめて問題が無かった主張

- **engine の mask**（`i915_pci.c:619-624,648-660,683-698`）: RKL だけ VCS2 が無く、TGL・ADL-S・ADL-P・ICL は rcs・bcs・vcs0・vcs2・vecs。RPL-S は adl_s_info を使う。
- **MOCS の選び方**（`intel_mocs.c:530-545`）: TGL・RKL は tgl の表、それ以外の Gen12 は gen12 の表、ICL は icl の表で engine ごと（`has_global_mocs` は GEN12 だけ）。
- **RKL・ADL-S の WA**: Wa_1607297627 以外は TGL・ADL-P と同じ。Linux の gt・ctx・rcs の list（`intel_workarounds.c:1483-1504,693-749,2371-2425`）と TLB の Wa_2207587034（`intel_tlb.c:86-92`、zedBSD `tlb.c:52` で無条件）で確かめた。
- **context の大きさ**（`intel_engine_cs.c:288-290`）、Gen12 の forcewake の範囲（`intel_uncore.c:2588`）、L3 bank の steering（`intel_gt_mcr.c:202-208`）: 4 platform で同じ。
- **render の資源**: RKL・ADL-S の GT1 は L3 が 4 bank（Mesa `intel_device_info.c:1008-1042`）。way は 16 KB で、URB の 32 way は 512 KB になる（`common/intel_l3_config.c:357-363`）。zedBSD の URB の配分（push 32 KB + VS 3576×64 B ≒ 256 KB、`intel/genxml.h:367-393`）はこれに収まる。VFE の thread 数は DSS の数から作る（`render/compute.c:434`）。この 2 点は Mesa から計算しただけで、実機では確かめていない。
- **HAL と UAPI**: HAL の変更は要らない。capset・Vulkan の properties は PCI ID を報告するだけで、userland に ID の表は無い（`render/instance.c:302-315`、userland の grep）。

## 確かめていない事

- Linux v6.19 の pciids と 6.8.12 の差（v6.19 は手元に無い）。
- RKL・ADL-S・RPL-S・ICL の実機での動き（全部、機械が無い）。
- ハードウェアが index 0・1 の MOCS を自分で使うか。
- GT が失敗した時の画面と console の振る舞い。
