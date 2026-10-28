<!-- awesome-plan project=zedbsd record=ws157 -->

# WS157: Keiland の app: 写真の管理

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: 2026-10-13 ユーザー「WS157はベータ2に入れます。UATは終わっています。OKでした。」、p001・p004〜p010 cleared、p015・p017 は 2026-10-13 Q1 の判定（T1-584））
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/photos/db.c:9・import.c:9・thumbs.c:19 の comment が要件の D1〜D4 を仕様として参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Keiland の標準 app として、写真を集めて整理し、見る app を作る。

## ユーザーの決定

- 2026-10-05「Keilandアプリとして、写真管理ソフト、音楽プレイヤー、動画プレイヤーを追加します。それぞれWSがなければ立ててください。写真管理ソフトは要件の検討からスタートですね。」
- 2026-10-07: 要件をユーザーが示し、D2・D3・D5 をユーザーがクリックで選択（「月ごとの TSV＋album ごと」「複写、同じ…」、p001）。最初の既定案（p002・p003）は置き換え。
- 2026-10-07「…写真の続き…」（ベータ3 へ。後に取り消し）
- 2026-10-10: メディア管理の先行実装（Photos の DB の処理を CLI の所有にし compositor の API から使う、Phone の＋と DnD も同じ API）と、保存の形式の変更（`~/Pictures/Media/metadata.db` の版付き JSON、原本の複写は `Media/Files/YYYY/MM/dd/名前`）をユーザーが承認（原文は会話にあり記録に無い）。Photos の削除（確認の dialog 付きの完全な削除、元の source は保つ）。
- 2026-10-11「mediastorageがメディアファイルをパースするのは権限的に避けたいです。既存のjailがありますよね、サムネイルで使っている。あれの仕組みを使ってほしいです。できればサムネイルのCLIもそのまま流用するのがよさそう。」
- 2026-10-11「全部承認です。」（p012 の推し: mediastorage が keiland-preview `--info` を起こす、SHA-256 は mediastorage に残す、解析の失敗・時間切れでも取り込む、libkeiland の media の API を非同期に、取り込みの時間の上限と一時の名前、thumbnail の decode を jail へ）
- 2026-10-11 L1（16 MP を越える写真）: ユーザー「Q1の推しにします」→ libjpeg-compat の縮小 decode（p017）。動画の寸法と撮影日時を mp4 の header から jail の中で読む（p016）: ユーザー「OKです」
- 2026-10-13「WS157はベータ2に入れます。UATは終わっています。OKでした。」

## 結果

- Photos の app（取り込み・album・timeline・お気に入り・回転・削除）、`/bin/mediastorage` の CLI と compositor の media の API（JSON の metadata、背景の取り込みと削除の worker、Wayland の通知）、共通の chooser の directory の選択、keiland-preview `--info` の jail の中の解析（JPEG・PNG・mp4 の header）、Photos・Phone の thumbnail の decode の jail、libjpeg-compat の 1/2・1/4・1/8 の縮小 decode が main にある（p016 の a1d5efa46 は main の祖先）。
- 証拠: QEMU の T1-331（p004・p005）、T1-584（p015・p017: 72 MP・200 MP の JPEG の thumbnail・開く・drag の絵、keiland-preview の jail）。実機は 2026-10-13 のユーザーの UAT「OKでした」。

## 制限・移管

- Phone の picker の上の入力は T1 の道具で届かず未判定（T1-584）。
- p011〜p014・p016 の phase.md は in-progress のまま WS を閉じた（実装は main、T1 の個別の判定の記録は無い。UAT はユーザーが OK）。
- MMS の写真・動画の送受信は WS197。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws157-p001 | 要件と設計（D1〜D7） | cleared |
| ws157-p002 | 最初の既定案の library | uncleared・取り下げ（要件で置き換え） |
| ws157-p003 | 最初の既定案の app | uncleared・取り下げ（要件で置き換え） |
| ws157-p004 | library・db・取り込み（複写、重複は取り込まない） | cleared（T1-331） |
| ws157-p005 | app（取り込み・album の card・縮小画像の cache、AAT） | cleared（T1-331） |
| ws157-p006 | メディアの CLI・compositor の API・Photos の移行 | cleared |
| ws157-p007 | 規約の全文・build・回帰 | cleared |
| ws157-p008 | folder の選択・背景の import/list・大きな JPEG の表示の修正 | cleared |
| ws157-p009 | UAT の修正の全 source の規約の全文・host・build・main の統合 | cleared |
| ws157-p010 | Photos の削除・API・CLI・通知・背景の worker | cleared |
| ws157-p011 | 削除の source の規約の全文・検証・main | in-progress のまま WS を閉じた（4a8856cc5 は main の祖先、UAT は OK） |
| ws157-p012 | media の経路の review と小さな直し、jail 化の設計 | in-progress のまま WS を閉じた（設計はユーザーが承認、実装は p013・p014） |
| ws157-p013 | mediastorage の解析を keiland-preview `--info` の jail へ、取り込みの時間と一時の名前 | in-progress のまま WS を閉じた（実装は main、T1 の判定の記録なし） |
| ws157-p014 | libkeiland の media の API を非同期に | in-progress のまま WS を閉じた（実装は main、T1 の判定の記録なし） |
| ws157-p015 | Photos・Phone の thumbnail の decode を jail へ | phase.md は test-wait のまま。2026-10-13 Q1 の判定: T1-584 で cleared 相当 |
| ws157-p016 | 動画の寸法と撮影日時を mp4 の header から jail の中で読む | in-progress のまま WS を閉じた（a1d5efa46 は main の祖先、T1 の判定の記録なし） |
| ws157-p017 | libjpeg-compat の縮小 decode | phase.md は test-wait のまま。2026-10-13 Q1 の判定: T1-584 で cleared 相当 |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験・検証の記録（tests/ の verification-*.md など）は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws157/` にある。回帰に使う試験（`run-host-jpeg-scale.py`、`run-host-mediastorage.sh` 一式、AAT の `make-photos.py`）は plan/tools/ へ移す。Codex のセッションの Queue の記録は [codex-queue.md](codex-queue.md)。Queue の履歴は [plan/history](../history/index.md)。
