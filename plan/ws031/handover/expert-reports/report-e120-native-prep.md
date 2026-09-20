# WS031 E-120 報告 — E-119 の境界確認と native 受入の準備（ベアメタル起動の直前まで）

日付: 2026-09-19　対象: Latitude 5330（ADL-P）。VM 試験は従来どおり VFIO/QEMU、明示 VBT、10 ms tick、execlists。
HAL・既存 UAPI の変更はありません。git の commit／push もしていません。**ベアメタル起動はしていません**（この報告の範囲は、その直前までの準備です）。

## 0. 要約

| 項目 | 結果 |
|---|---|
| E-119 の固定 | `increment-results/e119-freeze.md`: 2bf790a4 ＋ `e97-e119-changes.patch`（E-119 最終 source と一致することを確認済み）、patch と最終 sweep の全 build 出力の sha256、実行条件 |
| 報告の訂正（レビュー②） | buffer をまたぐ同一性は、E-119 の表では未確認でした → E-120 で確認（§4）。周期の説明、`event_rc` 表記、TLB の done の意味を訂正 |
| TLB の完了条件（③） | 実装は正本と同じく **done bit が 0 に戻るのを待つ**ことを確認。fake を「受け付け後に数回 1、その後 0」に変更し、待機が実際に poll していることを試験。意図的な fault の log に `backend=MODEL test=… expected_fault=1` と区間の目印を付与 |
| flip の event と IRQ の同期（④） | **欠落を 1 件発見して修正**: timeout した event の vblank 参照が、停止しても返されていませんでした。停止経路（`intel_crtc_vblank_off`）で event を取り消し、参照をちょうど 1 回返すように修正。host で timeout → 停止 → 再点灯 → 古い event では完了しない、を確認 |
| vblank evasion の sleep 経路（⑤） | model で 2 ケース（範囲内から正常に抜ける／vblank が来ない）。**実機で通過を確認**: scanline 1074（範囲 1073〜1079）で開始 → IRQ を有効にして sleep 1 回 → 起床後に再読 → arm → flip 完了 |
| buffer をまたぐ variant 比較 | LCD-D の順序を A0 B1 A2 B3 A1 B2 A3 B0 に変更。**全 4 variant で A と B の hash が一致**、写真も一致 |
| N0（native の事前確認） | 最初の display write（P3.6）の前に**読むだけ**の事前確認を実装。OpRegion → VBT の取得元の判定、VT-d、firmware の framebuffer、pipe の状態を記録し、PROCEED か「書込み前に STOP」かを判定。host 試験（実機の OpRegion dump と判定規則）と VM 実行で確認 |
| native で予想される最初の結果 | firmware（GOP）が pipe A を点灯したまま渡すため、**「pipe が active（N1 の引継ぎ未移植）」で、display へ何も書く前に STOP** する見込みです（§6.4） |
| 同じ context を再利用する renderer（⑦） | 未着手です（native の準備を優先しました） |
| 回帰（最終 source） | **13/13 PASS**（`sweep_e120.sh`、各 ktest 536/0、13 run すべてで N0 が PROCEED、LCD-D の evasion も REACHED） |

## 1. E-119 の固定と報告の訂正

- 固定: `e119-freeze.md` に、基点 2bf790a4、`e97-e119-changes.patch` と `e119-review-changes.patch` の sha256、最終 sweep（`sweep_e119f.sh`、13/13）で作った各 mode の vmunix／hdd-image の sha256、run script と flag を記録しました。E-120 の変更はこの固定の後から入れています。
- 訂正:
  - E-119 の LCD-D の表では、A には variant 0, 2 だけを、B には 1, 3 だけを描いています。そのため「buffer に関係なく同じ hash」のうち、**buffer をまたぐ部分は未確認**でした。確認できていたのは、同じ buffer に同じ variant を描いたときの再現性です。buffer をまたぐ比較は E-120 で行いました（§4）。
  - 周期の説明は次のとおりです: 同じ buffer は 2 round 後に再利用され、そのとき variant は 2 進んでいるので、直前の内容とは必ず異なります。したがって「描かずに前の内容が残る」故障を検出できます。
  - 表の `event=0` は戻り値なので `event_rc=0` と表記します（実機 log も `event_rc=` に変更）。
  - TLB の「done = BIT(instance)」は、その bit が **0 に戻れば完了**という意味です（§2）。

## 2. TLB の完了条件（gt_tlb.c と fake の対応）

| 対象 | 確認結果 |
|---|---|
| 本番の待機 | `parity_wait_reg(m, reg, mask = done, value = 0, 100 µs, 4 ms)`。正本の `wait_for_invalidate()` と同じく、mask が 0 に戻るのを待ちます |
| fake の成功応答 | 変更後: 要求を書くと、done bit は次の 3 回の read で 1、その後 0（受け付け → 所定の時点で clear）。TLB-POLL: 5 engine 分で poll が 8 回以上 |
| TLB-STUCK | 要求 bit が残り続ける（clear されない）→ -ETIMEDOUT、seqno は進まない |
| seqno | 成功したときだけ +2。失敗した無効化を「完了した世代」として後続が使うことはありません |

正本との違い（`gt_tlb.h` に記録済み）:
- 正本は void で timeout を log するだけです。こちらはエラーを返し、呼び出し側は page を解放しません（安全側の適応）。
- 正本は awake でない engine を飛ばしますが、ここでは飛ばしません（software の park は電源を落とさないため。E-119 の指示どおり）。
- MCR lock は取りません（ADL-P の engine の TLB register は MCR ではありません）。
- forcewake はすぐに返します（正本は遅延 put）。
- 呼び出し側は所有者 1 人で直列なので、`invalidate_lock` は持ちません。

意図的な fault の log は、次のように識別できるようにしました:
```
i915: parity ktest section begin: lcdg (backend=MODEL; lines tagged expected_fault=1 below are intended)
i915: parity TLB invalidation did not complete in 4ms (reg 0xced8 done bit 0x1 still set) backend=MODEL test=TLB-STUCK expected_fault=1
  … test=REL-TLB … / test=RTMAP-TLB …（各 4 行）
i915: parity ktest section end: lcdg
```
実機の release で timeout が起きた場合は `backend=HW test=- expected_fault=0` になります。

## 3. flip の event と IRQ の同期、timeout 後の寿命

**構造**: event の記録（armed、pipe、arm 時の frame）は、待機する thread だけが持ちます。IRQ handler は pipe の vblank 計数を IRQ lock の下で進めるだけで、event 本体には触れません。完了の判定は、thread が lock 下の snapshot（新しい IRQ ＋ frame counter の前進）と `PLANE_SURFLIVE` を読んで行います。したがって、未公開の event や別世代の event を IRQ 側が完了させる経路はありません。上位の `vblank_time_lock` などを no-op にしている根拠は、この「event 本体は thread 専有、IRQ と共有するのは lock 下の計数だけ」という分離です（報告の旧記述「所有者 1 人だから」は根拠として不十分でした）。

**見つかった欠落と修正**: flip が TIMEOUT すると、event の vblank 参照（`intel_pipe_update_end` で取るもの）は保持されます（後から完了しうるため）。しかし停止経路の `intel_crtc_vblank_off` は、単一 buffer 時代の判断（「event は存在しない」）のまま no-op で、**この参照は返されていませんでした**。正本の `drm_crtc_vblank_off()` は、保留中の event を送り、その参照を落とします。そこで修正しました:
- modeset object に `flip_event_ref`（event が参照を持っているか）を持たせる。
- `intel_crtc_vblank_off` → `parity_lcd_ms_vblank_off()`: backend の `cancel_event`（armed を忘れる: 以後の wait は拒否、遅れて来た vblank は何も完了させない）→ `vblank_put` を**ちょうど 1 回** → `flip_event_ref = 0`。
- kernel backend にも `k_cancel_event` を追加しました。

host section J（lcd-modeset **123/0**）:
- J1: flip を arm → timeout（参照 1 を保持）→ 停止で取消、参照は 1 回だけ返る（underflow なし）→ 遅れて来た完了は拒否 → 再点灯 → 次の flip は自分の新しい event で完了し、取消した event を二重に数えない。
- J2／J3 は §5 で説明します。J3 の後の停止でも event が取り消され、参照はすべて 1 回ずつ返りました。

## 4. buffer をまたぐ variant 比較（LCD-D、実機）

描画順は 0:A v0、1:B v1、2:A v2、3:B v3、4:A v1、5:B v2、6:A v3、7:B v0 です。同じ buffer の直前の内容とは、常に異なる variant になります。

| variant | A の hash | B の hash | 判定 |
|---|---|---|---|
| 0 | 8de54ee98047eb25 | 8de54ee98047eb25 | SAME |
| 1 | 14dde5a8ccd30b25 | 14dde5a8ccd30b25 | SAME |
| 2 | c84455e3e965c325 | c84455e3e965c325 | SAME |
| 3 | 0e2b92c2aaa7c325 | 0e2b92c2aaa7c325 | SAME |

描画 8/8、flip 7/7（すべて event_rc=0、+1 frame で live が切り替わる）、停止確認、RT 2025/2025×2 と TLB（timeouts 0）、両 buffer を回収しました。写真 9 枚（`e120-photos/lcdd-e120b-*.jpg`、一覧 `lcdd-e120b-sheet.jpg`）でも、同じ variant 同士（r1=r4、r2=r5、r3=r6、r0=r7）が同じ画像で、停止後は消灯していました。

## 5. vblank evasion の sleep 経路

**model（host J2／J3）**:
- J2: 最初の scanline を範囲の先頭（1073、範囲 1073〜1079、vblank start 1080）にしました。sleep は 1 回で、入るときに IRQ は有効でした（`sleep_irq_off=0`）。起床後に scanline を再読し、arm して DONE。IRQ の状態と参照は元に戻りました。
- J3: 範囲内のまま vblank が来ない場合: evasion は timeout で有限に終わり、update error として報告されます。flip は完了扱いにならず（TIMEOUT）、IRQ は元に戻り、保持されるのは event 自身の参照だけです。停止で取り消されます。

**kernel**: `k_vblank_sleep` は入るたびに CPU の実際の IRQ 状態を読み（`kern_irq_disable()` の戻り値を見てすぐ復元）、無効なら `sleep_irq_off` に数えます。

**実機（限定確認）**: 通常の flip 本体を、harness が選んだ時刻に呼ぶだけです（scanline 値や成功 flag の偽装はしていません）。試行は最大 16 回で、成立しなければ NOT-REACHED と記録します。
- 1 回目の実行（固定の先行量 2〜30 line、範囲の手前で開始）: 16 回とも sleep 0 → NOT-REACHED（log 保存済み）。trigger から update の最初の読出しまでの遅れを測るように直しました。
- 2 回目以降: 測った遅れは 0 line（trigger 時点の scanline と update の最初の読出しが一致）。trigger を scanline 1074（範囲内）にした 1 回目で、**sleep 1 回 → 起床 → arm → DONE**（event_rc=0、update errors 0、IRQ off での sleep 0）。VM 実行 3 回（e120b／c／d）すべてで REACHED でした。

## 6. N0: native の事前確認

### 6.1 対象機の native 側の事実（KVM host＝対象機そのものの Linux から、読むだけで取得）

| 項目 | 値 |
|---|---|
| ASLS | 0x614e5018（VM では 0 = QEMU は OpRegion を渡していない） |
| OpRegion | 2.1、8 KiB、mboxes 0x1d、**RVDA 0x2000（相対）／RVDS 8704** → VBT は OpRegion の外、ASLS＋0x2000 |
| VBT | Linux がそこから読んだ `i915_vbt.bin` の sha256 は 3bff4a0920d55c9a…＝**明示 blob の pin と同一** |
| DMAR | flags 0x05（INTR_REMAP、**DMA_CTRL_PLATFORM_OPT_IN**）。GPU 専用の DRHD 0xfed90000（device scope 00:02.0） |
| firmware framebuffer | efifb: 0x4000000000（＝GMADR の先頭）、1920×1080、stride 7680 → **GOP は pipe A を GGTT page 0 から点灯したまま渡す** |
| subsystem | 1028:0b02 |

### 6.2 実装（`parity/native_precheck.c`, `native_decide.c`, `opregion_vbt.c`）

- 位置: P3.4（power-domain 表の構築。MMIO なし）の後、**P3.6 `intel_power_domains_init_hw`（最初の display write）の前**。それより前の段階を確認しました: P3.1〜P3.5 は software か読むだけの処理です（drm_dev_init、vblank_init、bios_init＝option ROM の読出し、vga_register＝arbiter の client 登録、power-domain 表、pmdemand_init_early）。P2 の GGTT は scratch PTE を符号化するだけで、PTE を書くのは P6 以降です。P1.5 の GT reset は engine だけが対象です。
- 読むだけ（register、OpRegion、VT-d unit のどれにも書きません）:
  - **OpRegion → VBT**: `hal_space_map_device` で 8 KiB を read-only でコピーし、正本の順序で判定します（2.0 は物理、2.1 以上は相対の RVDA、なければ mailbox #4。ASLE_EXT の有無で長さを決める）。RVDA の場合は RVDS 分を map して妥当性を検査し、sha256 を明示 pin と比較します。
  - **VT-d**: GFXVTBAR は GPU BAR の MCHBAR mirror（0x145400）から読みます。unit を map して VER／GSTS（TES）／PMEN（EPM／PRS）を読みます。VER が 0 か全 1 なら「読めない」とします（guest の見え方）。
  - **firmware framebuffer**: boot handoff の `pcat.framebuffer`（既存 legacy driver と同じ入口）から、aperture 内なら GGTT page 範囲を求めます。
  - **pipe**: 電源の判定は、well の **STATE bit**（どの要求元でも実際の電源状態を表す）で行います。init_hw 前の driver の記録（hw_enabled = -1、BIOS 要求の引継ぎ前）は使いません。電源がない pipe は `not_readable` と記録し、register は読まず、inactive と数えます（正本の readout と同じ結論）。電源がある pipe は TRANSCONF、DDI_FUNC、PIPESRC、PLANE_CTL／SURF／STRIDE／SIZE を読みます。pipe A の domain が有効なら DPLL、DDI_BUF、PP、BLC も読みます。
- 判定（`parity_native_decide`、純関数）は、次の順で STOP します:
  1. active な pipe がある（N1 の引継ぎ未移植）
  2. firmware の scanout と、driver が書く GGTT page（上端 1 MiB＋display 窓 32 MiB）が重なる
  3. native で VT-d unit が有効なのに読めない
  4. native で TES または PMR（EPM／PRS）が立っている（zedBSD に IOMMU driver はない）

  どれにも当たらなければ PROCEED です。STOP の場合は `res.outcome = BLOCKED, where = "native-precheck (before any display write)"` で teardown へ進み、display には何も書きません。

### 6.3 試験
- host `opregion-host-test`（**11/0**）: 対象機の実 OpRegion dump → 2.1、RVDA 相対 0x2000、RVDS 8704、物理 ASLS＋0x2000。Linux が読んだ VBT は RVDS byte で、header の宣言 size 8701 が収まり、BDB も内側にある。分岐として mailbox #4（ASLE_EXT あり／なし）、2.0 の物理 RVDA、2.1 で RVDA が OpRegion の内側を指す場合の警告、ASLE なし、署名不一致、長さ不足。
- host `native-decide-host-test`（**8/0**）: 何もない native → PROCEED。**GOP が pipe A を点灯（surf 0、fb は GGTT page 0 から 2025 page）→ active を理由に STOP、重なりなし**。重なり → STOP。TES → STOP。PMR → STOP。VT-d 不明 → STOP。guest の見え方 → PROCEED。VT-d なし → PROCEED。
- VM 実機（LCD-D の build）: `N0 … hypervisor=1 | ASLS=0 | GFXVTBAR=0xfed90001 view=guest VER=0 readable=0 | fb 0x80000000（aperture 外）| pipe A..D not_readable（STATE off）| decision: PROCEED` の後、LCD-D は従来どおり PASS しました。

### 6.4 native の最初の起動で予想される到達点と、残っている課題
- **予想**: pipe A が active（GOP）のため、N0 で「書込み前に STOP」します。これは「未対応条件で、正確な箇所で止まった」到達点で、LCD の陰性結果ではありません。log には、OpRegion から得た VBT が pin と一致するか、VT-d の実際の状態、pipe A の実際の register 値（surface、format、stride）が残るはずです。**これが N1 の設計入力になります**。
- **N0 が防いだ既存の危険**（調査で判明）: 現在の P5 の `sanitize` は、active な pipe の DPLL を「使われていない」として切ります（pipe_mask の readout が未実装）。N0 がその前で止めるため、この経路には入りません。
- **N1 で必要なもの**: active な pipe の readout（`intel_modeset_readout_hw_state` の plane／DPLL／link 部分、`initial_plane_config`）、`intel_crtc_disable_noatomic` を中心とする正本の停止経路、DPLL の pipe_mask の readout、firmware framebuffer の GGTT／stolen の扱い。停止後は既存の通常初期化 → LCD-D へつなぎます。
- **OpRegion**: `intel_opregion_register`（ASLE／SWSCI）は未移植です。現在の probe は ASLS≠0 で `BLOCKED where=intel_opregion_register` になるため、N1 の後にもう一つの壁として残ります（正本からの移植が必要）。VBT については、native では OpRegion（RVDA）を取得元にでき、明示 blob と同一であることを N0 が記録します。
- **log の経路**: 対象機に legacy serial はなく、kernel の log の出口は画面と 32 KiB の ring です（ring は `sysctl kern.msgbuf`→syslogd→`/var/log/messages` で USB へ残せます）。最初の native 起動は、**画面の写真と、userland まで上がった場合の `/var/log/messages` の回収**を前提にします。N0 は起動直後の数十行なので、ring に収まります。
- **GOP console**: kernel の text console は firmware の framebuffer（aperture 経由）へ書き続けます。N0 で止まる間は問題ありません。N1 で表示を引き継ぐ前に `kern_text_suspend()` で止める必要があります（記録のみ）。
- **DMA**: DMAR の platform opt-in があるため、firmware が PMR や translation を残しているかを N0 が native で実測します。残っていれば STOP です。

## 7. 試験

- host: lcd-modeset **123/0**（+8: J1〜J3）、lcd 56/0、dp 72/0、**opregion 11/0（新）、native-decide 8/0（新）**、check_generated は一致。
- GPU-free ktest: **536/0**（+1: TLB-POLL）。意図的な fault の log 12 行は、すべて `backend=MODEL test=… expected_fault=1` で区間の内側にありました。
- VM 実機: LCD-D（新しい順序＋evasion probe＋N0）PASS を 3 回。
- 回帰（`sweep_e120.sh`、最終 source）: **13/13 PASS** — EU-REPEAT 5/5、DRAW、R1 12/12、TEX、T3 9/9、BL 4/4、TEX＋明示 VBT、AUX＋SCANOUT、LCD-B、LCD-R 3/3、LCD-G、LCD-C 4/4、LCD-D（描画 8/8、flip 7/7＋probe 1、buffer をまたぐ比較 4/4、evasion REACHED、TLB timeouts 0）。runner はすべて probe=COMPLETE cleanup=1。log 全体で TLB timeout の行はすべて `expected_fault=1`（MODEL）で、`backend=HW` の timeout は 0 件でした。

## 8. 成果物
- 報告（本書）: `expert-reports/report-e120-native-prep.md`
- review diff（E-119 固定版との差分）: `increment-results/e120-review-changes.patch`。累積 patch: `e97-e120-changes.patch`
- 固定: `increment-results/e119-freeze.md`
- 実機 log: `increment-results/e120-run-parity-hw-lcdd-*.log`（NOT-REACHED の回、REACHED の回、N0 付きの回）、sweep の log `e120-run-parity-hw-*.log`
- 写真: `increment-results/e120-photos/`
- 作業 script: `handover/tools/lcd-e120/`（round51〜57 ほか）、`sweep_e120.sh`
- 新しい host 試験: `plan/ws031/tests/opregion-host-test.c`、`native-decide-host-test.c`（と run script）

## 9. 次の手順の提案
1. **native 第一枠（事前採取）**: 現在の build（どの試験 flag でも N0 は共通）を USB から native で起動し、N0 の記録を写真と `/var/log/messages` で回収します。予想は STOP（active pipe）です。
2. その記録をもとに **N1**（active な pipe の readout と正本の停止経路）を移植し、VM では「active な状態から始める」model 試験で確認します。
3. `intel_opregion_register` を移植します。
4. **native 第二枠**: N0 → N1 → 通常初期化 → LCD-D（少数回）。
5. 並行して、同じ context を再利用する renderer を進めます（未着手）。
