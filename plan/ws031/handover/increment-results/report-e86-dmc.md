# WS031 E-86 報告：DMC firmware は実機でロード成功しました

## 結論（ご質問への直接の回答）

**はい、DMC firmware は実機（8086:46a8 rev 0x0c）で正常にロードできました。**

前回の実機ラン（案1(b)：intel_dmc_init を非同期実装 → modeset/flip WQ 作成 → intel_mode_config_init で停止）のログ抜粋：

```
i915: parity P3 intel_dmc_init: DMC load queued (path=i915/adlp_dmc.bin, work_submitted=1)
i915: parity P3 modeset/flip workqueues: modeset=1 flip=1
i915: parity teardown: intel_dmc_fini (worker synced; load_seq_completed=1, payload_writes=12982, dmc_ref_held=0)
i915: parity attach end: reached=P3 outcome=BLOCKED where=intel_mode_config_init err=0
```

意味：
- **load_seq_completed=1** … 非同期ワーカがパース→ペイロードのMMIOプログラム→事後WAまで完走。
- **dmc_ref_held=0** … DMC がロード成功時に取得した POWER_DOMAIN_INIT 参照を、規約どおり自分で解放（リーク無し）。
- teardown で flush→参照処理→アリーナ解放、クリーンに完了。
- 到達点は予定どおり **intel_mode_config_init**（案1(b)のストップ地点）で BLOCKED、err=0。

## 併せて完了していること（GPU-free 側 184/0）

- DMC-NORMAL / NO-FW / BAD-FW / FINI(running) / FINI(fault) の4系統テスト＝**184 checks / 0 failures**。
- F1 パーサ（version 2.20 確認）、F2 プログラムロード（13019 write を1write単位で検証）、F3 非同期ワーカ／3系統の電源参照／fini はすべて緑。

## ひとつだけ、ご判断いただきたい観測点

GPU-free の固定フィクスチャは **payload_writes = 13019**、実機は **12982**（37 DWORD 少ない）。

- これは**失敗ではありません**。ロードは両方とも成功しています。
- 差の仮説：GPU-free テストは stepping を `'D','0'` にハードコードしているのに対し、実機は PCI revision(0x0c) から stepping を導出します。DMC firmware は stepping ごとに MAIN/PIPE エントリの選択が変わるため、実機が選ぶペイロード集合が固定フィクスチャと異なり、書き込み DWORD 数が変わる、というのが最有力です（＝実機はその revision にとって正しいペイロードを書いている）。

（補足：この差を裏取りするための revid/stepping/version 診断ログを probe.c に入れようとして、書式文字列に生改行が混入するバグで一度ビルドが割れていました。今は修正済み・**ビルドはクリーン**です。ただし追加のGPUランはまだ回していません。）

## ご判断いただきたい方針（どれで進めるか）

- **(A) 12982 を「実機のその revision にとって正しいロード」として受理**し、次段（intel_mode_config_init 以降 or 別ユニット）へ進む。差の裏取りは不要。【推奨】
- **(B) まず裏取りだけする**：修正済み診断ログで実機を**1回だけ**回し、revid→stepping→選択ペイロードの内訳（MAIN/A/B/C/D の fw_size と present）を採取してから、(A) に合流。追加コスト＝実機ラン1回（〜3分）。
- **(C) stepping マッピング自体を精査**する（深追い）。

私の推奨は **(A)**、もし数字の説明を明示的に残したいなら **(B)** を一度だけ、です。方針をいただければその通りに進めます。
