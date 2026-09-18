# WS031 引き継ぎ資料：Gen12（ADL-P）EU スレッド実行ハングの調査

作成 2026-09-18。対象は **zedBSD の i915 ドライバ（Linux-parity 経路）で、GPU に dispatch した EU スレッドが完了しない問題**です。この文書だけで作業に着手できるよう、問題の定義・現在地・除外済み事項・再現手順（QEMU 起動パラメータ込み）・コード地図・参照資料の索引をまとめています。ホスト名や IP、ユーザ名は意図的に書いていません。

- 実装台帳（時系列の全記録）: [`../results-ws031.md`](../results-ws031.md) — 増分 **E-16〜E-30**（big-bang 期の EU 調査）と **E-31〜E-105**（Linux-parity 移植）が本件。E-105 で同一ソースの回帰 4/4 と bilinear 4/4 PASS、表示／libvulkan の事前調査は [`notes/survey-display-lcd.md`](notes/survey-display-lcd.md)・[`notes/survey-libvulkan-path.md`](notes/survey-libvulkan-path.md)。**E-104 で texture 更新・binding 切替・context 再利用 9/9 PASS。リファクタ前の回帰基準は [`../regression-baseline.md`](../regression-baseline.md)。****E-102 で描画反復と compute↔3D 切替 12/12 PASS、E-103 でテクスチャ付きオフスクリーン描画が実機 PASS。****E-101 で修正済み命令列による PS 描画も実機 PASS（1024/1024 画素）。****E-98 で PIPELINE_SELECT の符号化誤り（0x6104、正は 0x6904）を発見、E-99 で修正版の C1 が実機 PASS（累積修正版で EU 実行・書込み・request 完了を確認）、E-100 で反復・同一 context の次 request・新 context も 6/6 PASS。§2.5 参照。**
- 前任専門家の指示書: [`expert-reports/`](expert-reports/)（`gen12-ps-hang-report1..29.md`）と、それに対する進捗報告 `report_35..53.md` / `ws031-report-30..34.md`。
- 設計メモ: [`notes/`](notes/)。増分ごとの結果メモ: [`increment-results/`](increment-results/)。生成ツール: [`tools/`](tools/)。Linux 陽性対照 VM の資材: [`linuxvm/`](linuxvm/)。

---

## 1. 依頼内容（何をしてほしいか）

zedBSD の parity 経路は、Linux 6.8.12 i915 の `i915_driver_probe()` 全経路（P0〜P7：PCI/MMIO/GGTT/DMA/表示電源/DMC/IRQ/表示 readout/`intel_gt_init`（WA 表・MOCS・RC6/RPS・execlists・context image・`__engines_record_defaults`・`__engines_verify_workarounds`・migrate）/PXP/表示 probe/driver register）を実機で完走します（E-96）。**しかしその GT 上で GPGPU_WALKER を投入すると、EU スレッドは完了せず、big-bang 期と同一署名で停止します（E-97）。** 同じ GPU・同じ VFIO 条件で Linux i915（execlists, `enable_guc=0`）は同一バイトの batch を完走させます（E-23/E-25）。

お願いしたいのは、**「Linux で動き zedBSD で動かない」差分の特定と、問題箇所の修正**です。修正後は元の担当（Claude）に戻し、以後の parity 作業を継続します。

## 2. 問題の定義と署名

### 2.1 試験（compute 陽性対照 C1）
- RCS0 に、`PIPELINE_SELECT(GPGPU)` → `STATE_BASE_ADDRESS` → `MEDIA_VFE_STATE(MaxThreads=559)` → `MEDIA_INTERFACE_DESCRIPTOR_LOAD` → `GPGPU_WALKER(1×1×1, SIMD8, 1 thread)` → `MEDIA_STATE_FLUSH` → post-sync `PIPE_CONTROL` → marker、という batch を投入する。
- カーネル（36 dword、Mesa `brw_compile_cs` 出力）は **無条件の A64 store（0xc0ffee02 → VA 0x100400c20）→ `send.ts EOT`**。
- 共有ページ VA `0x100400000`（IDD @896、カーネル @1024、marker @0xc00/0xc20/0xc28/0xc30）、batch VA `0x100401000`。全 DW 値は [`notes/compute-control-design.md`](notes/compute-control-design.md) と `src/drivers/gpu/i915/parity/eu_test.c`。
- この batch/カーネル/IDD/VA は、Linux i915 上で `EXECBUFFER2`（softpin、同一 VA）により **完走が実証済み**（E-25、[`linuxvm/linux-c2-replay.c`](linuxvm/linux-c2-replay.c)）。

### 2.2 zedBSD での署名（E-97、parity 完走後の GT 上）
```
EU-TEST HANG: engine=rcs0 dss=5 max_threads=559 batch_dwords=322 timed_out=1 (2 s) wedged=1
  ready=c0ffee10  eu=dead0000  done=dead0000  cs=dead0000  idd_rb_ok=1 kernel_rb_ok=1
  ipehr=70040000 (walker 直後の MEDIA_STATE_FLUSH)  acthd=1:004014c0  instdone=ffdeffff
  row_instdone(0xe164,raw)=8610e87f  fault(0xcec4)=0  eu_dis(0x9134)=0  slice/ss01/ss23 ack=3/3/3
  ctx: CTX_CTRL=00090008 (default_state 継承, restore inhibit 無し)  PDP0=1:00213000
```
- walker 直前の marker（READY）は着地 → CS は batch を正しく実行。IDD とカーネル本文を CS が PPGTT 経由で読み戻した値も一致 → 写像は健全。
- walker 投入後、EU の store も EOT も完了せず、`MEDIA_STATE_FLUSH` で CS が待ち続ける。GPU fault なし。EU 電源 ack は 3、`eu_dis=0`。
- この署名は big-bang 期（自作 init、E-16〜E-30）と**完全に同一**。PS 描画（3DPRIMITIVE）でも同じ（PS/compute 共通＝EU スレッド実行の共通故障、E-16/E-17）。

### 2.3 これまでに除外されたもの（根拠は台帳 E 番号）
| 除外 | 根拠 |
|---|---|
| batch/kernel/IDD/VFE/walker の内容、VA（4 GiB 超／低位） | 同一バイトが Linux で完走（E-25）、低位 VA でも同一ハング（E-18/E-20） |
| store 形式・windower・PS 固定機能・RT surface | store 無しの `send.ts EOT` だけでも同一ハング（E-17） |
| GPU/VFIO/物理・GuC | Linux execlists（`enable_guc=0`）で陽性（E-23） |
| PAT/MOCS/caching、GLOBAL レジスタ群 | Linux と diff して一致（E-26/E-27） |
| golden context（`__engines_record_defaults` の image 継承）、indirect-ctx BB（Wa_18022495364 等） | 実装しても同一ハング（E-21/E-30、E-97） |
| GT/engine/context WA 表、MOCS、RC6/RPS、PAT、SSEU/RPCS、execlists/CSB、record_defaults | parity で正本どおり実装・実機検証（E-91〜E-96；WA は SRM で読み戻し一致 E-94）した上で同一ハング（E-97） |

### 2.4 残っている差分候補（着手順の提案）
1. **MCR（multicast）レジスタの実効値**（E-98 で 3 件を steered 読み戻し済み：ROW_CHICKEN2/ROW_CHICKEN4 は全 DSS 一致、SAMPLER_MODE bit15 は全 DSS で 0 だが E-27 の Linux 読み値も 0x3020 で差ではない）：`GEN8_ROW_CHICKEN2(0xe4f4)`, `GEN9_ROW_CHICKEN4(0xe48c)`, `GEN10_SAMPLER_MODE(0xe18c)`, `L3SQC`, `0x9xxx` 台など EU 向け WA は、CS の SRM では検証できません（正本も除外）。parity の MCR 書込み（`osdep/mmio.c` の multicast 経路）が全 DSS に着地しているかを **steering 付き MMIO 読み戻し**で確認するのが最も安価です。E-27 の diff は GLOBAL 中心で MCR は一部です。
2. **PDE のキャッシュ属性**：E-95 で入れた `parity_gt_ppgtt_alloc_range` の実ページ表 PDE が `PPAT_UNCACHED` になっていた（正本 `set_pd_entry` は `I915_CACHE_LLC`＝`PPAT_CACHED_PDE`）。修正済み。**E-98 で修正版（実表 walk で cached リンクを確認、表書込後の clflush 公開も追加）を実機で再試験 → 同一署名で HANG**。
3. parity が「適応／N/A」と記録した箇所：GGTT/WC 窓は big-bang 資産（P2）、retire は CSB ポーリング（割込み駆動でない）、`intel_pxp_init_hw`（Linux では mei_pxp の bind で KCR init/irq が走る）、runtime PM（autosuspend 非武装）、hwconfig（GuC 前提）。各増分の「記録した適応」節（台帳）に列挙。
4. 初期化以外の条件差：Linux 陽性対照は Linux ブート後の GPU、zedBSD は OVMF＋（ホスト側 FLR）後。fuse／クロック／電源状態の差はレジスタ全量 diff でしか詰められない。
5. **停止時の EU/TDL/GAM 状態の採取**：`row_instdone=0x8610e87f` の各ビットの意味付け（EU not-done）以上の分解ができていない。専門家指定のレジスタがあれば `eu_test.c` の hang dump に追加できる。
6. GuC submission の移植（Linux 既定）は、陽性対照が execlists で通っているため優先度は低い。

### 2.5 E-98 で見つかった提出 bytes の差 → E-99 で修正版 C1 が実機 PASS
E-98 の再停止後、実際に提出した batch（`increment-results/e98-batch-as-submitted.hex`）を Linux で完走した replay（`linuxvm/linux-c2-replay.c`）と語単位で比べたところ、**PIPELINE_SELECT の dword が違った**：zedBSD は `0x61041310`／`0x61041312`（`vk/linux/3dstate-gen12.inc` の `GEN12_CMD_PIPELINE_SELECT 0x6104`）、Linux/replay は `0x69041310`／`0x69041312`（`gt/intel_gpu_commands.h` `PIPELINE_SELECT = (3<<29)|(1<<27)|(1<<24)|(4<<16)`）。0x6104 は「効かない PIPELINE_SELECT」ではなく、Gen12LP では **別命令 GPGPU_CSR_BASE_ADDRESS（SubType 0、3 dword）のヘッダ**で、かつ長さ・予約 bit も通常と一致しない不正な語。zedBSD の全 batch（big-bang の draw/C0〜C3/golden、parity の eu_test）がこの語を使っていた。**正しいパイプライン切替は保証できず、実際のパイプライン状態と副作用は未確認**（「3D のままだっただけ」とは言わない）。E-25 の「同一バイト」報告と、そこから導いた「batch／dispatch state は原因から除外」は撤回。

**E-99: 修正版（`.inc`／`eu_test.c` を 0x6904、提出 object から読む事前検査、固定参照語による独立試験）で同条件の C1 を 1 回 → 実機 PASS**（READY/EU/DONE/CS 全 marker、request 完了、reset なし、ktest 373/0、vmunix 96274bf3）。E-98 提出 batch との差は dword 6 と 261 の 2 語（XOR 0x08000000）のみ。記録は「累積修正版で EU 完了を確認」（PDE／clflush／default_state 継承の寄与は分離していない）。成果物: `increment-results/e99-c1-artifact.md`（SHA-256、VA、意図的な差）、`e99-c1-*.bin/.hex`、`e99-run-parity-hw-eu.log`、抽出・差分ツール `tools/eu_artifact.py`。PS 描画（3DPRIMITIVE）にも同じ不正語が入っていたので、修正済み命令列での PS 試験は独立に行う（未実施）。

**E-100: 同じ提出 bytes（artifact と byte 一致）で、初回＋同一 context 3 回（seqno 4/6/8）＋新規 context 2 回（seqno 2/4）を 1 起動で実行 → 6/6 PASS**（各回 marker 4 種、PPGTT 読み戻し、HWSP 生値＝seqno、park 完了、reset なし。vmunix 5fe21213、ログ `increment-results/e100-run-parity-hw-eu.log`）。parity 経路の EU 陽性基準はこれで確立。

**E-101: PS 描画の独立試験（`PARITY_DRAW_TEST=1`、C1 を先に流さない独立起動）→ 実機 PASS。** big-bang の draw fixture（`draw_fixture.h`＋`selftest.c` の wrapper、同じ関数が生成する命令列・state・PS kernel、変更は PIPELINE_SELECT 定数のみ）を C1 と同じ request 形で提出。CS marker 3 種、PS の A64 marker（0xc0ffee01）、RT 32×32 全 1024 画素 0xffff0000、request 完了、reset なし（vmunix 4d89ae7e、ktest 376/0、ログ `increment-results/e101-run-parity-hw-draw.log`、提出 bytes `e101-draw-*`）。legacy 側の処置（MMIO 直書き WA、ring 追加命令、EOT 敷き詰め、clflush、統計 SRM）は移していない。**「PIPELINE_SELECT が big-bang 期 PS ハングの唯一原因」とは書かない**（初期化・request 形・legacy 処置の有無も同時に違う。legacy＋修正語は未試験で、試す予定もない）。big-bang 期の切り分け結論（E-7〜E-30）は不正語を含む命令列の上での観測として保持し、除外判断には使わない。

### 2.6 E-102／E-103: 描画の再利用確認とテクスチャ描画
- **E-102（R1）**: 1 回の P0〜P7 の後、12 提出を順に実行（`PARITY_R1_TEST=1`）: context A draw×4／B draw×2／C draw→C1→draw／A C1→B draw→A C1。全 step で marker・1024 画素（RT は毎回 0x5a5a5a5a で初期化）・HWSP 生値・park が成立、reset なし。draw の state 頁と C1 の shared 頁は同じ VA で、完了・park 後に CPU が頁全体を書き換える。
- **E-103（T1＋T2）**: 8×8 R8G8B8A8_UNORM texture を nearest／LOD 0／clamp で 32×32 RT へ描く fixture。PS・prog_data・texture layout・RSS・SAMPLER_STATE・変更 packet 語はすべて `tools/reftex.c`（固定 Mesa @ab691a1c: brw_compile_fs＋isl＋genxml gen120）が生成 → `src/drivers/gpu/i915/tex_fixture_gen.inc`。独立起動（`PARITY_TEX_TEST=1`）で **1024/1024 画素一致、texture・guard 無変更、request 完了、reset なし**。1 回目は generator の誤り（画素座標の内部命令を直書き → compiler が X/Y 導出コードを出さず UV が常に 0）で全画素 texel(0,0)。front-end 形（gl_FragCoord）に直し、compiler が要求する source depth／W・GRF start 4 を state に反映して合格。
- 回帰 pin（GPU-free）: C1 batch FNV 5dfb47d3c10b0560、単色 draw batch FNV 241f478201bb3a81（実機 PASS bytes）。出典台帳 `../provenance-ledger.md`。
- **E-104（T3）**: `PARITY_T3_TEST=1`、1 起動 9 提出。texture A の内容更新、A↔B の binding 切替（binding table entry 1 の 1 dword だけが変わる）、同一 context の再描画、新規 context、別 context 後の旧 context。全 step 1024/1024、texture・guard 無変更、host 独立検証 9/9（`tools/eu_artifact.py verify-t3`）。vmunix f9731b24、ktest 383/0。
- **計画更新（2026-09-18）**: E-104 は「オフスクリーン描画の基準が完成」。機能追加は止めず、順に 残り 4 モードの回帰 → bilinear → LCD 参照確定・native 事前確認 → 通常稼働の寿命管理＋ディスプレイ／LCD（hotswap は入口と安全な下位処理まで）→ libvulkan 接続 → Vulkan アプリから LCD 表示 → native 総合受入 → 最後に著作権整理・`osdep_` 改名・`parity/` 整理。出典・元表示の保持だけは今から進める（入力: `../provenance-ledger.md`、`../license-inventory.md`）。基準は `../regression-baseline.md`（オフスクリーン基準。後で LCD 基準・Vulkan アプリ基準を追加）。

### 2.7 E-107／E-108: 表示（LCD）の入口 — 明示 VBT と、実 AUX からの DPCD／EDID 取得

- **VBT**（E-107）: OVMF 下では ASLS=0 で OpRegion が無い。採取済み VBT（8704 B、sha256 3bff4a09…24cd）を read-only firmware provider から**明示的に**供給する（build flag `PARITY_VBT_EXPLICIT=1` かつ PCI subsystem 1028:0b02 のときだけ。OpRegion があるふりはしない）。parser は正本 `intel_bios.c` から `tools/port_intel_bios.py` で生成（`parity/vbt/`）。child は実 VBT の 4 個（A=eDP、B=HDMI、TC1/TC2）で、既定 A/B/C には継ぎ足さない。
- **PPS／VDD／AUX**（E-108）: 正本 `intel_pps.c`／`intel_dp_aux.c` と DRM core の DPCD・I2C-over-AUX・EDID 読出しを `tools/port_dp_aux_pps.py` で生成（`parity/dp/`）。hardware と時間への依存は `struct parity_dp_env` 経由で、同じ本番関数を register model（`dp_fake_hw.c`）と実機で動かす。host 試験 63/0（`sh plan/ws031/tests/run-dp-host-test.sh`）、ktest 407/0。
- **実機**: `-DPARITY_AUX_TEST=1`（GPU 投入なし、明示 VBT も要求）で実 AUX から DPCD／EDID を取得、採取値と一致、VDD と電源参照を返却して PASS（2 回、`increment-results/e108*-run-parity-hw-aux.log`）。合格行: `AUX-TEST verdict: PASS (… dpcd_match=1 edp_dpcd_match=1 edid_match=1 wells_balanced=1)`。
- まだ正本どおりでない点は台帳 E-108 §1 に列挙（実行位置、遅延 VDD-off worker が timer 未接続、mutex は所有の表明、async put は即時）。常駐デバイス化で解消する。
- E-108 で `parity_dc_off_enable()` を正本どおり `gen9_set_dc_state()` 経由に修正（P7 後に DC_off を取る最初の経路で誤診断が出た）。

## 3. 現在地（コードの状態）

- parity 経路は `CONFIG_DRIVER_PCI_I915_PARITY=y` でビルドしたときだけ有効（`src/drivers/gpu/i915/i915.c` の `#if CONFIG_DRIVER_PCI_I915_PARITY` で通常 attach を止め、runner に登録）。GPU は **公開されない診断経路**（`/dev/gpu0` は出ない）。
- runner（`parity/runner.c`）：起動後の readiness で 1 スレッドを起動し、GPU があれば **attach（probe P0〜P7）→ teardown → ktest**、無ければ ktest のみ。結果は `runner-result:` 行。
- 実機で `attach end: outcome=STOPPED where="i915_driver_probe complete"`、`runner-result: probe=COMPLETE`、`ktest 373 checks, 0 failures`、EU 有効ビルド（`-DPARITY_EU_TEST=1`）では `EU-TEST PASS`＋`EU-REPEAT PASS`（E-100、vmunix 5fe21213）、draw 有効ビルド（`-DPARITY_DRAW_TEST=1`）では `DRAW-TEST PASS`（E-101）、`-DPARITY_R1_TEST=1` では `R1 PASS`（E-102、vmunix 8164e787）、`-DPARITY_TEX_TEST=1` では `TEX-TEST PASS`（E-103、vmunix 48bb8b71、ktest 381/0）。試験フラグは互いに排他で、1 起動 1 モード。
- EU 試験は `PARITY_EU_TEST=1` を定義したビルドでのみ実行（既定 0）。
- git は未コミット（ユーザ管理）。作業ツリーは build host の `~/zedBSD`。

## 4. ビルドと実行

### 4.1 ビルド（build host、`~/zedBSD`）
```bash
# 通常（EU 試験なし）。成果物: build/amd64/vmunix, build/amd64/uefi/BOOTX64.EFI, build/amd64/hdd-image.img（UEFI+BIOS ハイブリッド、rootfs/data/swap 同梱）
make CONFIG_DRIVER_PCI_I915_PARITY=y disk-image

# EU 試験入り（driver_register 完了後に compute 陽性対照を 1 回投入し、HANG なら dump + reset）
make CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=-DPARITY_EU_TEST=1 disk-image
```
- `ZEDBSD_TEST_CPPFLAGS` は `Makefile` の設定 stamp に含まれ、変更時は全体が再コンパイルされる。
- カーネル像は物理 2 MiB にリンク、UEFI ローダが空いていなければ 1 GiB 未満の 2 MiB 整列空きへ再配置する（E-93、`docs/howto/boot-and-storage.md` の `kernel_phys=`）。像上限は 16 MiB。
- ビルドログの `error:`/`warning:` は 0 が前提（`-Werror`、`-Wframe-larger-than=8192`、カーネルスタック 16 KiB）。

### 4.2 実機（KVM ホスト、IGD を vfio-pci にバインド済み）
参照条件（E-49 で統一。**この条件以外で実機評価をしない**）：
```bash
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd vg-parity.fd
sudo timeout 240 qemu-system-x86_64 \
  -machine q35,accel=kvm,memory-backend=mem \
  -cpu host,host-phys-bits-limit=39 \
  -m 4096 -smp 4 \
  -object memory-backend-memfd,id=mem,size=4G,share=on \
  -device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=vg-parity.fd \
  -drive file=guest-parity.img,format=raw,if=ide,index=0 \
  -vga std -display none -monitor none -serial none -nic none \
  -debugcon file:run-parity.log -no-reboot
```
- `guest-parity.img` は `build/amd64/hdd-image.img` をそのまま置いたもの。**カーネルログは `-debugcon` のファイル**（ポート 0xe9）。
- `host-phys-bits-limit=39` は必須（IOMMU MGAW=39、無いと VFIO の DMA map が -22。E-23/E-42/E-43）。`x-igd-opregion=on` でも本機では ASLS=0（OpRegion 不在）。`rombar=0`。
- ホストの IGD は `vfio-pci` に残す（drm を blacklist しない）。ホスト側で `i915` を bind してはいけない（規約）。
- 1 回の起動で attach → teardown → ktest まで約 60〜90 s。HANG 時は 2 s のタイムアウト後にエンジンをリセットして続行する。

### 4.3 GPU なし（OVMF のみ、同じホストまたは任意の KVM ホスト）
上の行から `-device vfio-pci,...` を除くだけ（`run-parity-nogpu.sh`）。runner は SYNC_ONLY で ktest（fake MMIO の GPU-free 試験、現在 367 checks）だけを走らせる。

### 4.4 KVM の無い build host で（TCG、遅い）
```bash
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/vars.fd
timeout 300 qemu-system-x86_64 -machine q35 -m 4096 -smp 2 -display none -no-reboot \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/tmp/vars.fd \
  -drive file=build/amd64/hdd-image.img,format=raw,if=ide -debugcon file:/tmp/log.txt
```
TCG では ktest の実時間タイムアウト系が FAIL する（既知、E-93）。`-d int,cpu_reset -D int.log` を付けるとフォルトの RIP/CR2 が取れる（E-93 で使用）。

### 4.5 ホスト側の純粋 C 試験
```bash
cc -std=c11 -O1 -g -Wall -Wextra -Werror -I. -Iinclude -Iinclude/uapi \
   bootloader/uefi/elf64.c src/hal/amd64/bsp-pcat/handoff-validation.c plan/ws031/tests/kernel-placement-host.c -o /tmp/t && /tmp/t
python3 plan/ws025/tests/run-memory-host.py plan/ws025/temp/<出力dir>   # メモリ系ホスト試験一式
```

## 5. ログの読み方

`run-parity.log`（debugcon）の parity 行はすべて `i915: parity ` で始まる。
- `P0`〜`P7`：各段の実行記録（例 `P6c record_defaults: rc=0 …`, `P7 driver_register: … wells_on 8 -> 2 dc_state=0x2`）。
- `attach end: reached=P3 outcome=STOPPED|BLOCKED|FAILED where=<関数名> err=<errno>`：probe の終着。`outcome=STOPPED where="i915_driver_probe complete"` が完走。`BLOCKED` は未実装依存に当たった正確な関数名、`FAILED` は実行した段の失敗。
- `teardown: …`：driver remove 相当（unregister → pxp fini → engines stop&reset → objects release → … → PM put）。
- `EU-TEST PASS|HANG|ERROR: …` と `rcs0 dump(eu-test): …`、`EU-TEST hang: ipehr=…`：EU 試験（`PARITY_EU_TEST=1` のみ）。
- `ktest (completion+workqueue): N checks, M failures`：GPU-free 試験の総括。**この行が無ければ ktest は完走していない**（フォルトは黙って止まることがある）。`ktest FAIL: <試験名>` が個別失敗。
- `runner-result: selftest=PASS|FAIL probe=NOT_RUN|COMPLETE|BLOCKED|FAILED last_op=… blocked_at=…`：最終行。
- `tr[n] note|unimpl …`：適応層の trace（未実装到達点の記録）。

## 6. 再現手順（EU ハング）

1. `make CONFIG_DRIVER_PCI_I915_PARITY=y ZEDBSD_TEST_CPPFLAGS=-DPARITY_EU_TEST=1 disk-image`
2. `hdd-image.img` を KVM ホストへ `guest-parity.img` として配置し、§4.2 の起動。
3. `grep -n "EU-TEST\|dump(eu-test)" run-parity.log`。期待（現状）：§2.2 の HANG 署名。PASS なら `eu=c0ffee02 done=c0ffee20 cs=c0ffee30`。
4. 試験は `parity/eu_test.c`（`parity_eu_test_run`）。batch 生成は `parity_eu_test_build_batch()`（GPU-free 試験 `EU-BATCH` で語順を照合）。hang dump を増やすなら `parity_eu_test_run()` 末尾の `kern_logf` に追加。

## 7. コード地図（`src/drivers/gpu/i915/parity/`）

正本は `plan/ws031/linux-parity/linux-reference/ubu-i915-src/`（Ubuntu 6.8.0-139 ＝ upstream v6.8.12。取得記録 `manifest.txt` と参照 kernel config `config-6.8.0-139-generic` は親ディレクトリ `linux-reference/`）。各ファイル冒頭コメントに対応する正本関数と「記録した適応」を書いてある。

| 領域 | ファイル | 正本の対応 |
|---|---|---|
| 入口・順序 | `probe.c`（P0〜P7 を実行順に配線、~1900 行）, `runner.c`, `parity.h` | `i915_driver_probe` |
| 適応層 | `osdep/{pci,mmio,dma,sync,runtime_pm,firmware,trace}.{c,h}`, `backend_{pci,mmio,sync,dma}.c` | uncore/forcewake（`gt_fw_ranges.inc`＝`__gen12_fw_ranges` 生成）, DMA API, completion/waitq, runtime PM |
| P1 GT MMIO | `gt_mmio.{c,h}` | `intel_gt_init_mmio`（engine mask, SSEU, fuses） |
| P1 reset / 待機 / PCODE | `reset.{c,h}`, `wait.{c,h}`, `pcode.{c,h}`, `timer_calc.c` | `intel_gt_reset`, `wait_for`, `skl_pcode_request` |
| P2 DRAM/BW/OpRegion/VBT | `dram_bw.{c,h}`, `bios.{c,h}` | `intel_dram_detect`, `intel_bw_init_hw`, `intel_bios_init` |
| P3 表示電源 | `power_domains.{c,h}`（xelpd 30 wells, DC_off ops）, `display_core.{c,h}`（`icl_display_core_init`）, `combo_phy.c`, `cdclk.c`, `pch.c`, `vga.c`, `dmc.c` + `firmware_adlp_dmc.c` | `intel_power_domains_init/init_hw`, DMC ロード |
| P4 IRQ | `irq.{c,h}` | `intel_irq_install`（gen11 reset/postinstall/handler, MSI 分離） |
| P5 表示 nogem | `display_state.{c,h}`, `display_nogem.{c,h}`, `drm_device.c` | `intel_display_driver_probe_nogem`（readout/sanitize、DRM object model は最小表現） |
| P6 GT | `gt_init.h`, `gt_init_base.c`, `gt_wa_adlp.c`（WA 表/MOCS/RC6/RPS）, `gt_mem.{c,h}`（GT object・GGTT 窓・ppgtt）, `gt_engine.{c,h}`（HWSP/ELSQ/CSB）, `gt_lrc.{c,h}` + `gt_lrc_offsets.inc`（context image・indirect ctx BB）, `gt_request.{c,h}`（flush/LRI/breadcrumb）, `gt_submit.{c,h}`（ELSQ 投入・CSB）, `gt_resume.{c,h}`（`intel_engines_init`/`intel_gt_resume`）, `gt_defaults.{c,h}`（`__engines_record_defaults`）, `gt_verify_wa.{c,h}`, `gt_migrate.{c,h}` | `intel_gt_init` 一式（execlists） |
| P7 | `pxp.{c,h}`, `driver_probe.{c,h}` | `intel_pxp_init`, `intel_display_driver_probe`, `i915_driver_register` |
| 試験 | `ktest.{c,h}`（GPU-free、fake MMIO/PCODE/CSB、367 checks）, `eu_test.{c,h}` | — |

big-bang 期の自作ドライバ（`src/drivers/gpu/i915/*.c`、`selftest.c` に C0/C1/C2/golden の実装）は同じツリーにあり、parity 無効ビルドで動く。parity はそれを置き換える別経路（P2 の GGTT/WC 窓など一部資産を流用）。

## 8. 規約・制約（前任から引き継ぐもの）

- git add/commit/push はユーザが行う（作業者はしない）。
- ホストの IGD は vfio-pci のまま。drm を blacklist しない。実機評価は §4.2 の参照条件のみ。
- HAL の外部インタフェース不変（10 ms tick、`kernel_timer_handler`、`KERN_CLOCK_HZ=100`、waitq deadline 単位）。HAL 変更が必要なら事前提示。
- MSI 分離・runner・共有 sync backend・attach 先行（selftest より先に attach）を維持（E-67：selftest 後の attach でデバイスが idle-suspend し config が 0xffff になる）。
- 実機描画（PS）再試験・EU 試験は明示解除時のみ。EU ハングの原因探索そのものは、今回はこの引き継ぎで専門家が行う。
- 正本にない挙動を足すときは「記録した適応」として台帳と該当ファイルの冒頭コメントに書く。空成功 stub で先へ進めない。
- 各増分：GPU-free 試験（ktest）→ 実機 1 回 → 台帳追記。

## 9. Linux 陽性対照の再現（`linuxvm/`）

同じ GPU を Ubuntu 24.04 ゲストへ VFIO で渡し、i915（`enable_guc=0`）で動かした環境。起動パラメータは [`linuxvm/boot-dev.sh`](linuxvm/boot-dev.sh)（VFIO あり、SSH は hostfwd）、[`linuxvm/boot-nogpu.sh`](linuxvm/boot-nogpu.sh)。要点：cloud image に `linux-modules-extra` を入れる（i915 非同梱）、`i915.enable_guc=0`（GuC fw 欠如で wedge するため）、`host-phys-bits-limit=39`。cloud-init の user-data（鍵・パスワード）は同梱していない（ユーザ `awe`、password ログイン可の一般的な cloud-init）。
- [`linuxvm/eu_positive_control.py`](linuxvm/eu_positive_control.py)：OpenCL で 1 work-item を 3 回実行する陽性対照（E-23）。
- [`linuxvm/linux-c2-replay.c`](linuxvm/linux-c2-replay.c)：zedBSD の compute batch を直接 ioctl（GEM_CREATE/MMAP_OFFSET/EXECBUFFER2、softpin、`CONTEXT_PARAM_ENGINES {RENDER,0}`）で Linux 上に流す（E-25、L-MI/L-C0/L-C2/L-C1 全 PASS）。
- [`linuxvm/mmio_read.c`](linuxvm/mmio_read.c)：BAR0 mmap のレジスタ読み（E-27 の diff に使用、`intel_reg` 併用）。

## 10. 参照資料の索引

- 台帳 [`../results-ws031.md`](../results-ws031.md)：E-5/E-6（3D パイプラインが実機で走る）、E-7〜E-13（PS ハング精密切り分け）、E-14〜E-18（compute 陽性対照 → EU 共通故障）、E-19〜E-21（indirect ctx WA）、E-22〜E-25（Linux 陽性対照）、E-26〜E-30（基盤監査・レジスタ diff・golden context）、E-31〜E-34（parity 方針・適合層）、E-38〜E-59（P0〜P2）、E-60〜E-90（時間基盤・DRM・P3〜P5）、E-91〜E-92（P6 GT・record_defaults 成功）、E-93（カーネル配置）、E-94〜E-96（verify_wa・migrate・P7）、E-97（EU 試験 HANG）、E-98（PDE 修正版再試験 HANG・MCR 読み戻し・PIPELINE_SELECT 誤り発見）、E-99（修正版 C1 実機 PASS）、E-100（C1 反復・次 request・新 context 6/6 PASS）、E-101（PS 描画 PASS）、E-102（描画反復・compute↔3D 切替）、E-103（テクスチャ描画 PASS）、E-104（T3 9/9 PASS、回帰基準の固定）。
- 移植台帳 [`../linux-parity/ledger.md`](../linux-parity/ledger.md)：正本 revision の固定、関数単位の状態語（PORTED/VERIFIED/NOT_TAKEN）と根拠。
- 前任専門家の指示書 [`expert-reports/gen12-ps-hang-report*.md`](expert-reports/)（1〜29）と進捗報告 `report_35..53.md`、`ws031-report-30..34.md`。**E-31 以降の方針（Linux-parity 移植）は report 30〜34 と gen12-ps-hang-report27〜29 に経緯がある。**
- 設計メモ [`notes/`](notes/)：`compute-control-design.md`（C1 の全 DW）、`roadmap-to-eu-test.md`（P3→EU 試験の工程）、`plan-p5-nogem.md`、`plan-p6-gem-gt-init.md`、`hal-h-proposed-diff.md`。
- 増分結果メモ [`increment-results/`](increment-results/)：`e10..e27-results.md`（big-bang 期の各試験の生データ）、`report-e86..e97-*.md`（parity 期の報告書）。
- 生成ツール [`tools/`](tools/)：`gen_lrc_offsets.py`（`gt_lrc_offsets.inc` を正本 `intel_lrc.c` から生成）、`gen_fw_ranges.py`（`gt_fw_ranges.inc`）、`gen_refcs*.py`/`gen_refps_marker.py`（Mesa `build-gentool` でカーネルを生成、`plan/ws031/mesa-refs/`）、`engine_sseu.py`、`make_reloc_image.sh`（`zedbsd.cfg` を差し替えた試験用イメージの作り方）。
- Mesa 参照 `plan/ws031/mesa-refs/`（gentool/refcs/refps の standalone ビルド）。
- 参照メモリ配置と `kernel_phys=`：`docs/howto/boot-and-storage.md`。

## 11. 引き渡し時点の未確定・注意

- E-99/E-100: PIPELINE_SELECT 修正版で C1 実機 PASS（計 7 回、反復・次 request・新 context を含む。PASS 経路の HWSP 記録行も追加済み）。E-101 で修正済み命令列での PS 描画も PASS。次の候補は描画の反復／次 draw／新 context、C1 と draw の混在順、その先は parity 残作業（GEM／DRM object model、runtime PM、device 公開）。実機試験は明示解除ごと。
- `intel_power_domains_verify_state` は well 側の照合のみ（domain use-count は未追跡）。
- `intel_initial_commit` は active crtc 0 の形のみ実装（本機は active pipe 0）。
- ktest は GPU-free で走るが、実機起動時も attach 後に実行される（ktest 中のログは fake 経路のもの。`BIOS left unused DC_off …` 等は fake の P5-d 試験）。
- カーネルは kernel-mode の #PF を必ずしも出力しない（黙って止まる）。ktest の「checks,」行の有無で完走を判断する（E-97 の教訓）。
