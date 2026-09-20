# WS031 増分 E-123（続き）報告: レビュー対応、EDID 再読み出し、二画面（別内容・同一バッファ）

2026-09-20 / zedBSD Linux-parity (i915, Linux v6.8.12 正本) / Dell Latitude 5330 (ADL-P)

前回の報告（report-e123-hdmi.md）へのレビューを受けて、指摘事項を実装し、そのうえで二画面へ進んだ。

## 1. レビュー指摘への対応

### 1.1 model 試験中の実 IRQ（最優先）
**確認した構造**: 実機の入口は「SDEIIR を読んで ack した**あと**に、HPD の処理だけを飛ばす」形だった。つまり指摘の表の 2 番目（接続変更を消費して捨てる）に当たる。

**対処**: 実インスタンスがその起動で一度でも動いたら、model インスタンスの開始を拒否する。HPD の model 試験は GPU なし構成でのみ実行し、実機起動では理由を記録して実行しない（実機 run の ktest は 582 checks、GPU なし run は 612 checks）。デバイスの hotplug 状態を model と共有しない、という指摘どおりの分離。

終了時の work 同期は正本の intel_hpd_cancel_work をそのまま使っている（hotplug work、dig-port work、poll init work、reenable work の 4 つを同期してから、キューを壊す）。

### 1.2 EDID 未取得と connected の出所
**訂正**: 前報告の HPD 実機 PASS は **slice (a) のビルド**（EDID 未移植、live status で接続扱い）での結果だった。slice (b) では正本どおり、EDID が読めなければ disconnected になる。実際、EDID 試験では live=1 でも disconnected と報告している。

**記録の改善**: HPD-EVENT の記録に、採用した status に加えて **live status、EDID の戻り値、有効ブロック数、digital かどうか、epoch** を併記するようにした。

### 1.3 EDID が読めない件の表現
「移植ではなく環境要因」→ **「参照 Linux でも同じ環境で EDID 取得に失敗した。環境または共通の開始条件に依存する可能性が高いが、原因は未確定」**に改めた。台帳も同じ表現にしてある。

### 1.4 VBT の HDMI level shift
**指摘のとおり実装が誤っていた**。既存パーサ（vbt/parity_vbt.h の `hdmi_level_shift`）から port B の値を読み、正本の `intel_ddi_hdmi_level()` に渡すようにした。**実機の値は 0**（有効な index）で、以前の「既定 entry へ固定」とは別の入力から選ばれている。値が無い場合（< 0）のみ、正本と同じく table の既定 entry に落ちる。

### 1.5 GMBUS
転送全体を正本と同じ共有 mutex（display.gmbus.mutex）で保護し、hotplug worker と試験スレッドが同時に触れないようにした。NAK・timeout・ACK は別々の値として返る（GMBUS2 の状態確認で有限に終わる）。jiffies は 10 ms tick に統一してあり、変換（msecs_to_jiffies）も同じ周期を使っている。

storm 後の polling への縮退と再有効化は、model 試験でのみ確認しており、**実機では未確認**（polling 自体は計数のみ）。

## 2. EDID の再読み出し（工程 1）

点灯中に同じ DDC からもう一度読んだ。**結果は変わらず**（status disconnected、reads 2 fails 2 rc -5）。TMDS を出しても DDC は応答しない。表示状態はこの結果で変更していない（固定モードのまま）。log: `increment-results/e123-run-parity-hw-hdmib-edid.log`。

## 3. 二画面（工程 2）

### 3.1 実装
- **DPLL**: デバイス全体の pool（2 個）にし、正本の `intel_find_shared_dpll`（hw state が一致すれば共有、なければ空き）と参照カウント（`intel_reference_shared_dpll` / `intel_unreference_shared_dpll`）を生成して使う。固定割り当てはやめた。pipe の参照返却とデバイス再作成時の初期化も入れた。
- **DBUF / MBUS**: 状態をデバイス全体に移し、各画面は「この構成で点灯する pipe の集合」（`also_active_pipes`）で DDB を計算する。A+B では MBUS 非結合になる。**適応**: 1 つの atomic commit ではなく直列 commit（計算そのものは正本の check が出す状態と同じ）。
- **modeset object を 2 面化**。pipe ごとの状態・event・scanout は分け、PLL・CDCLK・DBUF・電源・ロックは同じ実体。

### 3.2 実機結果（別内容、必須項目）— **PASS**
内蔵 LCD 1920×1080 パターン 110（pipe A / **DPLL0**）と外部 HDMI 1280×720 パターン 111（pipe B / **DPLL1**）を同時表示。利用者が両画面を目視確認（F110 / F111）。20 秒の観測で frame counter A 1262 / B 1247。MBUS 非結合。
**外部だけ停止しても内蔵は継続**（A 1268→1298、PLL・power domain・buffer を保持）。その後内蔵も停止し、両 buffer を解放。log: `e123-run-parity-hw-dual.log`。

### 3.3 実機結果（同一バッファ、追加目標）— **PASS**
1 つの buffer（1920×1080、pitch 7680）を両 pipe が読む。外部には**同じ行の左上 1280×720**が出る。これは**部分表示**であり、縮小ミラーではない（縮小には pipe scaler が必要で未使用）。
buffer の寿命を利用者数で管理するよう拡張し、外部停止後は users 2→1 で保持、その状態の unpin は拒否（rc -17）、内蔵停止後に users 0 となって解放。log: `e123-run-parity-hw-dual-shared.log`。

### 3.4 途中で起きた重大な不具合（記録）
1. **結線**: 2 面化後、生成コードが参照するグローバル（DDI 呼び出し側の encoder など）が「最後に prepare した画面」を指したままで、内蔵の停止処理が外部側の encoder に対して走った。結果、pipe と PLL は止まるのに DDI IO / AUX の電源参照が返らない（実機で観測: crtc_active=0、PLL off、io=30 aux=54）。→ 画面選択と各 entry point で結び直す。
2. **ホスト巻き込み**: 上記で停止できないまま VM が終了すると、表示が DMA を続けたまま IOMMU の unmap に入り、**ホストが `vfio_iommu_type1_detach_group` で soft lockup → panic**（2 回、電源長押しで復旧）。→ 終了処理の最後に「まだ表示している pipe / DDI を確実に止める」LAST-RESORT を追加（**適応**、参照の経路ではない安全網）。起動スクリプトにも `timeout --kill-after` を追加。

## 4. 回帰

最終ソースで 17 モードの sweep を 1 回実行中（従来の 14 + HDMI-B + DUAL + DUAL-SHARED）。結果は台帳に記録する。host 試験は lcd 56/0、lcd-modeset 123/0、生成物の再現性チェックも通過。GPU なしの ktest は 612/0。

## 5. 次

N1（ファームウェアが点けた画面の引き継ぎ）へ進む。今回できた複数出力・共有資源の管理（DPLL pool、DBUF のデバイス状態、buffer の利用者数、LAST-RESORT 停止）を、その readout と停止に接続する。
