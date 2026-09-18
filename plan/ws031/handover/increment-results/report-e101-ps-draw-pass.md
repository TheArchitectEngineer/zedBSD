# WS031 E-100／E-101 報告: C1 反復 6/6 PASS、修正済み命令列による PS 描画も PASS

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

指示 7-A の二つを順に実施し、どちらも PASS でした。

1. **E-100**：C1 の反復、同一 context の次 request、新しい context。1 起動で 6 回すべて PASS。
2. **E-101**：修正済み命令列による PS 描画の独立試験。**PS が実行され、render target の 1024 画素すべてが期待色、request も完了**しました。

「PIPELINE_SELECT の誤りが big-bang 期の PS ハングの唯一原因だった」とは書きません。今回の成功は、後述の複数の条件が同時に違う状態でのものです。

## 1. E-100：C1 の反復（1 起動）

| round | context | seqno | HWSP 生値 | marker 4 種 | 読み戻し | park |
|---|---|---|---|---|---|---|
| 初回 | A（新規） | 2 | 2 | 一致 | 一致 | 完了 |
| 1〜3 | A（同じ context、同じ timeline） | 4、6、8 | 4、6、8 | 一致 | 一致 | 完了 |
| 4〜5 | B（新規 context、新規 timeline） | 2、4 | 2、4 | 一致 | 一致 | 完了 |

- image は vmunix `5fe21213…`、GPU なしで 373 checks / 0 failures。reset なしで teardown まで正常です。
- 提出 bytes は E-99 の artifact と byte 単位で一致します（`cmp` で照合）。
- E-99 の残件だった「PASS 経路でも HWSP の生値を記録する行」を追加しました。
- 最初に合格しなかった round で停止し、ハング後は追加提出をしない作りです。

## 2. E-101：PS 描画の独立試験

### 構成

- big-bang の draw fixture（命令列、surface／dynamic state、実コンパイラ製の const-colour PS、RECTLIST 頂点）を、**同じ関数から GPU VA だけで生成**する薄い wrapper を用意しました。変更は E-98 の PIPELINE_SELECT 定数だけです。
- legacy 初期化側の処置は移していません：WA の MMIO 直書き、ring への追加命令、scratch page への EOT 敷き詰め、clflush、統計の SRM request。parity では正本の WA 表、MOCS、context 初期化がその役割を担います。
- 専用のビルドフラグで、EU 試験とは排他です。**C1 を先に流さない独立起動**で、C1 と同じ request 形（新規 context、default_state 継承、execbuf 形）を使います。
- 提出 object から PIPELINE_SELECT を読んで固定参照語で検査し、合わなければ提出しません。GPU なしの検査を 3 件追加しました（最終語、旧語の拒否、state page の要所）。GPU なしで **376 checks / 0 failures**。

### 実機結果（1 起動 1 提出）

image: vmunix `4d89ae7e…` / hdd-image `3be87128…`、flags `-DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_DRAW_TEST=1`。条件は E-99／E-100 と同一です。

```
DRAW-TEST fixture: batch_dwords=353 mocs=6 pdp0_matches_top=1 | pipeline_select: rc=0 3d=1@6 gpgpu=0 bad=0
EU-TEST record(completed): rq seqno expected=2 hwsp_observed=2 request_seqno_reached=1 | last_csb=00008000:03ff8000
DRAW-TEST PASS: completed=1 parked=1 timed_out=0 wedged=0 polls=17
  | before=a5a50001 middraw=c5c50003 after=d7a3f00d ps_marker=c0ffee01
  | pixels match=1024/1024 first=ffff0000 mid=ffff0000 last=ffff0000 expected=ffff0000
attach end: STOPPED (i915_driver_probe complete) / teardown 正常・reset なし / ktest 376/0
```

- CS の marker 3 種、PS 自身が A64 store で書く marker、RT 32×32 の全画素（不透明赤）がそろいました。
- 提出 bytes は batch 1412 bytes（sha256 `d41d1413…`）、state page 4096 bytes（sha256 `fe546190…`）として保存しました。

### 評価

確認できたのは「累積修正版の parity 経路で、3D パイプラインの PS dispatch、RT 書込み、request 完了が成立した」ことです。WS031 の発端だった PS 描画ハング（E-7〜E-13）と同じ fixture ですが、今回は次の点が同時に違います。

| 違い | big-bang 期（ハング） | 今回（PASS） |
|---|---|---|
| PIPELINE_SELECT | 0x61041310（別命令のヘッダを持つ不正語） | 0x69041310 |
| 初期化 | legacy big-bang | Linux-parity（P0〜P7） |
| legacy 側の処置 | MMIO 直書き WA、ring 追加命令、EOT 敷き詰め等 | なし |
| request 形 | 独自 | execbuf 形 |

legacy 初期化と修正語の組合せは試していません。混在させない方針のため、試す予定もありません。0x6104 語が実ハードウェア上で何を起こしていたかも未確認のままです。big-bang 期の切り分け結論（E-7〜E-30）は「不正な語を含む命令列の上での観測」として保持し、原因の除外判断には使いません。

## 3. 提出物（`plan/ws031/handover/increment-results/`）

| 種類 | ファイル |
|---|---|
| 差分（base 2bf790a4） | `e97-e101-changes.patch`（旧 `e97-e100-changes.patch` を置換、新規 `draw_fixture.h` を含む） |
| draw の提出 bytes | `e101-draw-batch.{hex,bin}`、`e101-draw-state.bin`、`e101-draw-manifest.txt` |
| 実行ログ | `e100-run-parity-hw-eu.log`、`e101-run-parity-hw-draw.log` |
| ツール | `tools/eu_artifact.py`（draw 用の抽出を追加） |

台帳は E-100、E-101。引き継ぎ README を更新済みです。

## 4. 次の候補（ご指示待ち）

1. 描画の反復、同一 context の次 draw、新しい context（E-100 と同じ形）。
2. C1 と draw の混在順（compute と 3D の切替を同一 context／別 context で）。
3. その先は parity の残作業（GEM／DRM object model の要部分、runtime PM、device 公開）。

git commit / push はしていません。HAL インタフェースは不変です。
