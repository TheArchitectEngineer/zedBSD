# WS031 E-92 報告: P6-c 完成 — parity 経路で初めて GPU が命令を実行

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本）/ 実機: ADL-P 8086:46a8 rev 0x0c（VFIO）

## 結論

`__engines_record_defaults` が**実機の 5 エンジンすべてで成功**しました。これが parity 経路での最初の GPU 実行です（承認③の範囲：ctx WA の LRI と breadcrumb だけの null request、および engine park 時の kernel context への切替。batch buffer なし・描画なし）。

```
P6c record_defaults: rc=0 polls=9 timed_out=0 wedged=0 | gt irq during: user=9 ctx_switch=10 error=0
P6c rcs0  defaults: state=PARKED rq_seqno=2 krq_seqno=1 hwsp_seqno=1 | submits=2 promotes=2 completes=2 errors=0
P6c rcs0  default_state: 65536 bytes | reg[1]=11081019 RING_HEAD=188 RING_TAIL=188 RPCS=80041000
P6c bcs0  default_state: 16384 bytes | RING_HEAD=f8 RING_TAIL=f8
P6c vcs0 / vcs2 / vecs0 default_state: 16384 bytes | RING_HEAD=78 RING_TAIL=78
attach end: BLOCKED where=__engines_verify_workarounds   (次段の設計上の停止点)
```

- 全エンジンが約 0.5 ms（9 回のポーリング）で 2 件の request を完了し、CSB は promote/complete が 2 組ずつ、**エラー 0**。
- P4 で入れた GT 割込み経路が CS の user / context-switch 割込みを受けて ack しています（error 割込み 0）。
- **HW が保存した RING_HEAD/TAIL が、GPU-free テストで語単位に予測した値と一致**しました（RCS 98 dw = 0x188、BCS 62 dw = 0xf8、VCS/VECS 30 dw = 0x78）。投入した命令列が設計どおりに実行・完了したことの直接の証拠です。
- 採取した image（engine->default_state）は、HW が LRI ヘッダ `0x11081019` を同じ形で書き戻し、render の RPCS=`0x80041000` を保持しています。

## P6-c の構成

正本の依存鎖（context 生成 → LRC → ring → request → ELSQ 投入 → CSB → idle 待ち → park 切替）を 6 単位に分け、各単位を GPU-free テストで固めてから実機に進めました。

| 単位 | 内容 | GPU-free |
|---|---|---|
| c0 | GT object、GGTT 上端窓、kernel ppgtt、gt scratch | 284/0 |
| c1 | status page、ELSQ レジスタ、CSB ポインタ、execlists 有効化 | 292/0 |
| c2 | LRC image・ring・descriptor・INDIRECT_CTX / PER_CTX_BB | 307/0 |
| c3 | ring 命令生成、ELSQ 投入、CSB 処理 | 322/0 |
| c4a | `intel_engines_init` + `intel_gt_resume`（正本順、投入なし）| 331/0（実機も確認）|
| c4b | `__engines_record_defaults` | 338/0（実機成功）|

## 途中で見つけて直したもの

1. **媒体 forcewake ドメインの欠落**: VCS0/VCS2/VECS0 のレジスタが「always-on」扱いで検査されていませんでした。ドメインを追加し、P6 は正本どおり全ドメイン保持に。
2. **forcewake 域表を正本から機械生成**: 手作業の表は他にも欠け（0x9xxx の RENDER/always-on 分割、MSG_IDLE、PWRGT_DOMAIN_STATUS など）があったため、`__gen12_fw_ranges` 43 項目を抽出する形に置換。表示・PCODE 帯（0x40000–0x1bffff）は always-on のままで、P2〜P5 には影響しません。
3. **`reset_csb_pointers` の `ring_set_paused(0)` 抜け**: 正本は冒頭で status page の PREEMPT 語を 0 に戻します。これが無いと、reset.prepare が 1 にした値が残り、**全 request の末尾の preempt busywait が永久に待つ**ところでした（実機投入前に発見）。
4. **P6-b の順序是正**: `intel_gt_resume` の正本順に組み直し、抜けていた **`rc6_sanitize`（初期化中は RC6 と render power gating を切る）** を追加。
5. **カーネル像の物理 8 MiB 上限**: 像は物理 2 MiB に置かれ、総計が約 6.29 MB を超えると UEFI が配置に失敗します（`Allocate kernel: EFI_NOT_FOUND`）。未使用だった適応層の DMA マッピング表を 256→32 に縮めて 6.09 MB に戻しました。

## 判断をお願いしたい点

**カーネル像の 8 MiB 上限（上記 5）**は、今回は適応層の未使用領域を削って回避しましたが、残り約 200 KB で、次の verify_workarounds と P7 で再び当たる可能性があります。恒久策は カーネルの配置（ロード先や再配置）側の変更で、HAL・ブートローダに触れるため私の判断範囲外と考えています。今のまま縮小で凌ぐか、配置側の対応を別途進めるか、方針をご指示ください。

## 正本で確定した事実（抜粋）

- gen12 に renderstate は無い／record request に init breadcrumb も無い。
- LRC オフセット表は 6.8.12 と 7.1 でバイト一致（機械生成）。
- gen11+ の GGTT 窓は UC 写像でフラッシュレジスタは書かない（big-bang は書いていた＝差分）。
- ADL-P は全 5 エンジンで AUX table invalidate（MI_SEMAPHORE_WAIT ポーリング）を要する。
- Wa_22011802037 が ADL-P に該当。`gt_sanitize(force)` は評価順により必ず全エンジン HW リセットを行う。
- record_defaults は 1 エンジンあたり 2 投入で、2 回目（kernel context への切替）完了時に初めて record context の image が書き戻される。

## 次

**`__engines_verify_workarounds`**（判断②で常時診断として承認済み）。kernel context 上で各エンジンの WA レジスタを **SRM でメモリへストアする request** を投入し、GPU から見た値を照合します。今回の null request と違い、レジスタ値を GPU に読ませる実行なので、着手前に一言いただければ進めます。

git commit / push はしていません。HAL インタフェースは不変です。
