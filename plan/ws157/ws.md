<!-- awesome-plan project=zedbsd record=ws157 -->

# WS157: Keiland の app: 写真の管理

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-07 q831 P2: p001 の既定案で p002・p003 を実装。p001 はユーザーの確認待ち）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: q831（2026-10-07、P2）
Resume point: p003 の T1 の結果（`apps.photos.*`）。ユーザーが p001 の既定案を変えたら p002・p003 を直す。
<!-- awesome-plan-current:end -->

## 単一目標

Keiland の標準 app として、写真を集めて整理し、見る app を作る。

## ユーザーの指示（2026-10-05、原文）

「Keilandアプリとして、写真管理ソフト、音楽プレイヤー、動画プレイヤーを追加します。それぞれWSがなければ立ててください。写真管理ソフトは要件の検討からスタートですね。」

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws157-p001](phase001/phase.md) | 要件の検討（下の観点）。既定案 P1〜P9 を記録、ユーザーの確認待ち | planning | — |
| [ws157-p002](phase002/phase.md) | library（`~/Pictures`・EXIF の日付・album・お気に入りと回転の保存）と host の試験 | in-progress（host 済み、T1 待ち） | p001（既定案） |
| [ws157-p003](phase003/phase.md) | Photos の app（grid・全面・slideshow・回転・お気に入り・App Home・AAT） | in-progress（T1 待ち） | p002 |

## p001 の観点（要件の検討）

- 何を管理するか: library（取り込み先の folder を見張るか、指定の folder を読むだけか）、album・日付・場所（EXIF の GPS）・人（顔の認識は範囲か）・お気に入り。
- 見る: 一覧（日付の timeline、grid）、拡大・slideshow、動画（WS122）も同じ library に入れるか。
- 編集: 回転・切り抜き・明るさなど簡単な補正の範囲。元の file を書き換えるか（非破壊の編集）。
- 取り込み: camera・SD card・USB（PnP の通知 WS132）からの取り込み。
- 形式: JPEG・PNG・HEIC・RAW など（今の画像の decoder の独自実装（libjpeg-compat・libpng-compat）との関係、HEIC の codec の license）。
- 共有: クラウドストレージ（WS146・WS147）との関係。
- 既存の Image Viewer（WS128 の imageview）との役割の分け方。
- 他の app（Apple Photos・Google Photos・Shotwell・digiKam）の調べ。模倣の範囲に注意（Files の Tags の件と同じく、特定の製品の固有の UI の写しは避ける）。
