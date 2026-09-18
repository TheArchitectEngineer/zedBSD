# WS031 E-99 報告: PIPELINE_SELECT 修正版で C1 を 1 回実行 → PASS

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

**累積修正版の parity 経路で、対象 C1 の EU 実行・書込み・request 完了を確認しました。** READY／EU／DONE／CS の 4 marker がすべて期待値で、request は HWSP の seqno 到達と CSB の context complete の両方で完了、reset なしで teardown まで正常です。

「PIPELINE_SELECT だけが全期間・全症状の唯一原因だった」とは書きません。PDE、表の公開処理、default_state 継承、execbuf 形 request を保持した条件での成功です。

## 1. 再試験前の確認（三項目）

| 項目 | 実施 |
|---|---|
| A. 最終命令語 | batch を提出 object に書いた後、その object から読んで検査する関数を追加。固定参照語 0x69041310 が 1 個、0x69041312 が 1 個、順序 3D→GPGPU、0x6104 ヘッダや想定外の 0x6904 語が 0 個。不成立なら提出しない。実機ログ `rc=0 3d=1@6 gpgpu=1@261 bad=0` |
| B. 生産側の残り | PIPELINE_SELECT の生成元は `vk/linux/3dstate-gen12.inc` のマクロ（big-bang `selftest.c` 4 箇所、`vk/pipe.c` 1 箇所が利用）と parity `eu_test.c` のマクロの 2 定義のみ。どちらも 0x6904。0x6104 の一括置換はしていません。直書きや生成済み配列の PIPELINE_SELECT はありません |
| C. 独立試験 | `.inc` に固定値との `_Static_assert` を 2 本。ktest `EU-PIPESEL` を 2 件追加：emitter 出力が固定参照語と一致／bit 27 を戻した旧語 0x61041310・0x61041312 を入れた batch は拒否される |

検査を足したため EU 有効の clean build をやり直し、GPU なしで **373 checks / 0 failures** を確認してから同一 image を実機へ渡しました。

## 2. 実機結果

image: vmunix `96274bf3…` / hdd-image `1a5f7878…` / BOOTX64 `57f8eab6…`、flags `-DCONFIG_DRIVER_PCI_I915_PARITY=1 -DPARITY_EU_TEST=1`。条件は E-98 と同じ（4 GiB／4 vCPU／host-phys-bits-limit=39、execlists、parity 単独・attach 先行、driver_register 後、forcewake 全保持、2 s、10 ms tick、HAL 不変、batch VA 0x100401000、診断 copy あり）。通常起動 P0〜P7 → 今回の起動で default_state 生成 → 新規 context → C1 を 1 回。

```
EU-TEST fixture: batch_hash=5dfb47d3c10b0560 fixture_hash=444e3a7a4e9c1abd batch_dwords=322 pdp0_matches_top=1
EU-TEST pipeline_select (read from the submitted object): rc=0 3d=1@6 gpgpu=1@261 bad=0
EU-TEST PASS: rc=0 submitted=1 completed=1 parked=1 timed_out=0 wedged=0 polls=16
  | ready=c0ffee10 eu=c0ffee02 done=c0ffee20 cs=c0ffee30 idd_rb_ok=1 kernel_rb_ok=1
  | rq seqno=2 krq seqno=4 | gt irq: user=2 ctx_switch=4 error=0
attach end: STOPPED (i915_driver_probe complete) err=0 / teardown 正常 / ktest 373/0 / runner-result probe=COMPLETE cleanup=1
```

- request 完了は「HWSP の seqno が 2 に到達」かつ「CSB で当該 context が complete」の両成立で判定しています。続く kernel context への park request（seqno 4）も完了しました。
- PASS 経路では HWSP の生値を 1 行に出していません（HANG 経路の記録行のみ）。試験済み binary と作業ツリーを一致させるため今回は未変更で、次の増分で足します。
- MCR 読み戻しは E-98 と同値です（SAMPLER_MODE 0x3020）。ページ表 walk も E-98 と同形です。

## 3. E-98 提出 batch（HANG）との全差分

```
dword   6  byte 0x0018  old=61041310 new=69041310 xor=08000000
dword 261  byte 0x0414  old=61041312 new=69041312 xor=08000000
```

これ以外の差はありません。shared page 側（IDD＋kernel）の hash も E-98 と同値です。

## 4. 記述の是正と過去判断の撤回

- **E-98 の説明の是正**：「GPGPU への切替が一度も発行されず、3D 既定のまま投入していた」は不正確でした。正しくは「PIPELINE_SELECT 予定位置に、別の命令識別部（Gen12LP では 3 dword の GPGPU_CSR_BASE_ADDRESS のヘッダ）を持ち、長さ・予約 bit も通常と一致しない不正な語が存在した。正しいパイプライン切替は保証できず、実際のパイプライン状態と副作用は未確認」です。
- **撤回**：E-25 由来の「同一入力が Linux で成功、zedBSD で失敗したので batch／dispatch state は原因から除外」。比較していたのは「Linux 基盤＋正しい命令列」対「zedBSD 基盤＋異なる命令列」でした。C0 完走も GPGPU 初期化が全て正しい証明ではありません。
- **保持**：Linux 陽性結果（E-23／E-25）、当時の zedBSD ハング観測、DMC・request・context 保存・SRM・ページ表等の個別実測、E-98 の PDE／公開処理／walk／MCR 採取。
- **PS 描画**：3D を選ぶ位置にも同じ不正語が入っていたため、今回の修正が効く可能性と別の不具合が残る可能性の両方があります。独立した試験として確認します。

## 5. 同一性の管理

- `e99-c1-artifact.md`：artifact ID `zedbsd-parity-c1-e99`、生成元、対象世代、VA、batch／kernel／IDD の byte 数と SHA-256、Linux replay との意図的な差（batch VA、診断 copy 44 個）、実機結果。

| object | bytes | SHA-256 |
|---|---|---|
| batch | 1288 | `c6437cf230c5541a70969ba6660ad1c06ed95bcc117e206be3c7126d5daf4f50` |
| kernel | 144 | `4faa29cd303b66cc02c36ff18018bc86c8a4b4db30c0a3d293900b90da988398` |
| IDD | 32 | `8438f3232c8b994da024199ce0bba23f2f97479152d6bf5aae8e43624e8312eb` |

- `tools/eu_artifact.py`：ログから提出 bytes を抽出（hex、生 binary、SHA-256）し、2 つの batch を全 dword で比較（index、byte offset、旧値、新値、XOR）します。
- 試験側の期待値は固定語で、実装マクロからは生成しません。
- 未実施：Linux 側の L-REF／L-ZED-EXACT（今回は分岐 A のため不要）、Gen12.0 定義による C1 全命令の意味層検査（次の増分の候補）。

## 6. 提出物（`plan/ws031/handover/increment-results/`）

`e97-e99-changes.patch`、`e99-c1-artifact.md`、`e99-c1-batch.{hex,bin}`、`e99-c1-{idd,kernel}.bin`、`e99-c1-manifest.txt`、`e98-batch-as-submitted.hex`（修正前）、`e99-run-parity-hw-eu.log`。台帳は E-99。

## 7. 次（指示 7-A、いずれも実機試験のため解除待ち）

1. 少数の反復、同一 context の次 request、新しい context での C1。
2. その後、修正済み命令列による PS 描画を独立した試験として。legacy と parity の初期化は混在させません。

git commit / push はしていません。HAL インタフェースは不変です。
