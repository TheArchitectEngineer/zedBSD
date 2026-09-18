# WS031 E-104 報告: T3（texture 更新・binding 切替・context 再利用）9/9 PASS、リファクタ開始条件がそろいました

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

T3 を 1 起動 9 提出で実行し、**9/9 PASS** でした。host 側の独立検証も 9/9 です。ご提示のリファクタ開始条件 6 項目がすべて実機の根拠つきでそろったので、ここで機能追加を止めます。

現行の execlists、参照起動条件、10 ms タイマー、HAL 非変更は維持しています。git commit / push はしていません。

## 1. T3 の構成

- texture B（VA 0x100405000）の surface state を surface heap の +192 に追加しました（A は +128 のまま）。**binding の切替は binding table entry 1 の 1 dword だけ**で、batch と state 頁の他の bytes は変わりません。GPU なしの検査で「差は 1 dword」を確認しています。
- batch は 1 本を全 step で使い回します（texture を名指ししないため）。E-103 で合格した提出 batch と同一 bytes です。
- 順序は毎回「前 request の完了と park を確認 → CPU が texture を更新（ある場合）→ state 頁を書き直し → RT を期待色と違う値で初期化 → 提出」です。GPU 実行中の CPU 書換えは試していません。
- テスト画像は 3 種で、いずれも位置識別・非対称です。

## 2. 実機結果

image: vmunix `f9731b24…` / hdd-image `d2b6599d…`、flags `-DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_T3_TEST=1`。GPU なしで 383 checks / 0 failures を確認してから実機へ渡しました。

| step | context | bind | CPU 更新 | 期待画像 | 結果 | 確認したこと |
|---|---|---|---|---|---|---|
| 1 | A（新規） | A | — | 画像 0 | 1024/1024 | T2 と同じ描画 |
| 2 | A | A | A := 画像 1 | 画像 1 | 1024/1024 | 同じ object の内容更新を次の draw が読む |
| 3 | A | B | — | 画像 0 | 1024/1024 | binding 切替（A は画像 1 のまま） |
| 4 | A | A | — | 画像 1 | 1024/1024 | 切替を戻す。古い binding を使い続けない |
| 5 | A | A | — | 画像 1 | 1024/1024 | 同一 context の再描画 |
| 6 | B（新規） | A | — | 画像 1 | 1024/1024 | 新規 context で同じ fixture |
| 7 | B | B | — | 画像 0 | 1024/1024 | 新規 context での binding 切替 |
| 8 | B | B | B := 画像 2 | 画像 2 | 1024/1024 | B の内容更新 |
| 9 | A | B | — | 画像 2 | 1024/1024 | 別 context が走った後の旧 context |

```
T3 PASS: steps=9/9 passed=9 wedged=0 polls=123 | gt irq: user=18 ctx_switch=36 error=0
attach end: STOPPED (i915_driver_probe complete) / teardown 正常・reset なし / ktest 383/0 / runner-result probe=COMPLETE cleanup=1
```

- 全 step で CS marker 3 種、PS marker、HWSP 生値、park が成立しました。初期値のまま残った画素は 0、texture の変更は 0 bytes、guard の破損も 0 bytes です。
- host 側の独立検証：各 step の RT の hash を、host で計算した期待画像の hash と照合して 9/9 でした。texture A／B の hash から保持している画像も host で同定し、bind された texture と期待画像の対応を確認しました。
- 最終 step の実 bytes は保存済みで、RT は host の期待画像と 1024/1024 一致です（画像添付）。

## 3. リファクタ開始条件の充足

| 条件 | 根拠 |
|---|---|
| C1 回帰が通る | E-99、E-100（6/6）、E-102 の C1 3 回 |
| 単色 PS 回帰が通る | E-101、E-102 の draw 9 回 |
| compute↔3D 切替が通る | E-102（同一 context 内と別 context 間、両方向） |
| テクスチャの初回描画が通る | E-103 |
| 内容更新・binding 切替・context 再利用が通る | E-104 |
| 正常終了と資源回収が成立する | 各回 teardown 正常、reset なし、ktest 完走、cleanup=1 |

回帰基準を [regression-baseline.md](plan/ws031/regression-baseline.md) にまとめました。試験モードとビルドフラグ、合格行、pin した入力の hash、artifact の場所、道具、リファクタで壊しやすい箇所のメモです。

**一点、正直に書いておくこと**：最終ツリーで実機実行したのは T3 だけです。C1 単独、単色 draw 単独、R1、テクスチャ初回の実機 PASS は各増分時点のツリーでのものです。その後に入った変更は、request builder の batch VA 引数、draw builder の texture 引数、object 台帳の拡張です。提出 bytes の同一性は GPU なしの pin で保証し、同じ request 経路は R1 と T3 が実機で通っています。それでも、リファクタ着手前に最終ツリーで全モードを実機で 1 巡して基準を確定するのが望ましいと考えます。

## 4. 次の段（第 1 段：出典・表示・生成物の整理）に向けた入力

- [provenance-ledger.md](plan/ws031/provenance-ledger.md)：出典台帳（E-103 で新設）。
- [license-inventory.md](plan/ws031/license-inventory.md)：i915 配下 152 ファイルの header の事実を自動抽出した棚卸しです。判断は含みません。

| ディレクトリ | ファイル数 | SPDX 行あり |
|---|---|---|
| `i915/`（legacy 本体と fixture） | 13 | 12（Zlib 11、生成物の MIT 1。今回新設の `draw_fixture.h` は表示なし） |
| `i915/linux/` | 6 | 6（MIT 5、Zlib 1） |
| `i915/vk/` | 26 | 23（Zlib。`vk/linux/` の .inc 3 本は表示なし） |
| `i915/parity/` | 107 | **0** |

parity 配下は SPDX 行も copyright 行もありません。ここは表示を整える前に、方針の決定が必要です。

## 5. ご判断をお願いしたい点

1. **回帰 1 巡の解除**：最終ツリーで C1、単色 draw、R1、テクスチャ初回を実機で各 1 起動（計 4 起動）実行して基準を確定してよいか。あわせて、リファクタ中に受入済みの回帰モードを再実行するための恒常的な解除をいただけるか。
2. **parity 配下の表示方針**：Linux の MIT ファイルを元に移植した関数を含むファイルが多数あります。ファイル単位で「元の Intel の copyright と MIT 表示を保持＋プロジェクトの表示を併記」とするのか、プロジェクトの Zlib 表示に第三者表示を別ファイルでまとめるのか。関数単位の区分（コピー／改変／独立実装）の監査をどの粒度で行うかも含めて、方針をいただければ第 1 段に着手します。copyright 名義の記入は私の判断では行いません。
3. **bilinear の小試験**：リファクタ前に入れるか、後に回すか。

## 6. 提出物（`plan/ws031/handover/increment-results/`）

`e97-e104-changes.patch`（base 2bf790a4）、`e104-run-parity-hw-t3.log`、`e104-t3-last-*`（最終 step の batch、state、texture、RT、manifest、画像）。ツールは `tools/eu_artifact.py`（T3 の検証を追加）、`tools/build_verify.sh`、`tools/hw_run.sh`、`tools/license_inventory.py`。台帳は E-104。
