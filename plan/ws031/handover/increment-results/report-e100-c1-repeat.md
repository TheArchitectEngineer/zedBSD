# WS031 E-100 報告: C1 の反復・同一 context の次 request・新しい context → 6/6 PASS

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

指示 7-A の確認を 1 起動で実行し、**6 回すべて PASS** でした。E-99 と合わせて、累積修正版の parity 経路の C1 は 2 起動・計 7 回すべて完了しています。parity 経路の EU 陽性基準が、コード・起動条件・提出 bytes・ページ表・結果の一組でそろいました。

「PIPELINE_SELECT が全期間の唯一原因」とは引き続き書きません。

## 1. 試験の構成

| round | context | timeline seqno | 内容 |
|---|---|---|---|
| 初回 | A（新規、default_state 継承） | 2 | E-99 と同じ C1 |
| 1〜3 | A（同じ context、同じ timeline） | 4、6、8 | HW が保存した image からの復帰を含む次 request |
| 4〜5 | B（新規 context、新規 timeline） | 2、4 | 新しい context での初回と次 request |

- VM、batch object、shared page は共通です。各 round の前に marker 4 種と読み戻し領域を初期化します。
- 各 round の前に、提出 object の PIPELINE_SELECT 検査と batch hash を再確認します。
- 最初に合格しなかった round で停止し、ハング後は追加提出をしない作りです（今回は該当なし）。
- 変更は試験 harness だけです。request 組立て、park、記録、ハング処理を関数に抽出し、初回と反復で同じコードを使います。初期化経路と HAL は不変です。

## 2. 実機結果

image: vmunix `5fe21213…` / hdd-image `b079b326…` / BOOTX64 `57f8eab6…`、flags `-DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_EU_TEST=1`。EU 有効 clean build を GPU なしで 373 checks / 0 failures まで確認してから実機へ渡しました。条件は E-99 と同一です。

```
EU-TEST record(completed): rq seqno expected=2 hwsp_observed=2 initial_breadcrumb_seen=1 request_seqno_reached=1 | lrca=fffb9119 | last_csb=00008000:03ff8000
EU-TEST PASS: completed=1 parked=1 polls=17 | ready=c0ffee10 eu=c0ffee02 done=c0ffee20 cs=c0ffee30 idd_rb_ok=1 kernel_rb_ok=1
EU-REPEAT round=1 ctx=A lrca=fffb9119 seqno=4 hwsp_observed=4 pass=1 polls=7 | ring 0x0e0→0x1c0
EU-REPEAT round=2 ctx=A lrca=fffb9119 seqno=6 hwsp_observed=6 pass=1 polls=7 | ring 0x1c0→0x2a0
EU-REPEAT round=3 ctx=A lrca=fffb9119 seqno=8 hwsp_observed=8 pass=1 polls=7 | ring 0x2a0→0x380
EU-REPEAT round=4 ctx=B lrca=fffcb119 seqno=2 hwsp_observed=2 pass=1 polls=9 | ring 0x000→0x0e0
EU-REPEAT round=5 ctx=B lrca=fffcb119 seqno=4 hwsp_observed=4 pass=1 polls=7 | ring 0x0e0→0x1c0
EU-REPEAT PASS: rounds=5 passed=5 wedged=0 | gt irq: user=12 ctx_switch=24 error=0
attach end: STOPPED (i915_driver_probe complete) / teardown 正常・reset なし / ktest 373/0 / runner-result probe=COMPLETE
```

- 全 round で marker 4 種が期待値、IDD と kernel の PPGTT 読み戻しが一致、HWSP の生値が当該 request の seqno と一致、park も完了しました。
- E-99 の残件だった「PASS 経路でも HWSP の生値を記録する行」を追加済みです（上の `record(completed)`）。
- 提出 bytes は E-99 の artifact `zedbsd-parity-c1-e99` と byte 単位で一致します（ログから抽出した batch／kernel／IDD を `cmp` で照合）。

## 3. 提出物（`plan/ws031/handover/increment-results/`）

`e97-e100-changes.patch`（旧 `e97-e99-changes.patch` を置換）、`e100-run-parity-hw-eu.log`。台帳は E-100。引き継ぎ README §2.5 を更新済みです。

## 4. 次

修正済み命令列による PS 描画の独立試験です。parity 経路には 3D 描画 batch を投入する手段がまだありません（big-bang 側の draw batch は legacy 初期化の上にあり、混在させない方針）。C1 と同じ request 形で draw 用の試験 harness を parity 側に用意し、GPU なしで最終語を固定参照と照合したうえで、実機 1 回は解除をいただいてから実行します。

git commit / push はしていません。HAL インタフェースは不変です。
