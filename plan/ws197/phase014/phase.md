<!-- awesome-plan project=zedbsd record=ws197-p014 -->

# ws197-p014: Phone の「＋」の添付の選択（写真の popup と file chooser、libkeiland の UI）

Parent: [WS197](../ws.md)
Status: planned
Phase disposition: normal
Queue: q926（[P1 の lane](../../agents/P1/queue.md)）

## 由来・承認

2026-10-11 ユーザー:「Phone Appはメディア管理機能を使ってメディアを添付できるようにしたいですが、まだ未実装です。添付を追加する「＋」ボタンを押すと、写真選択ボタンとファイル選択ボタンがでて、写真選択ならlibkeilandのメディア管理機能で問い合わせて、写真・動画一覧をポップアップで表示して選択する形です。ファイル選択はfile chooserです。」「MMSの添付ファイル送信、画像送信、動画送信、メディア選択ポップアップ(libkeilandのUI機能として実装)」

## 今（2026-10-11 Q1 の照合）

「＋」と「Attach File...」は `KL_FILE_CHOOSER_MEDIA` の file chooser を 1 つ開くだけ（`phone/main.c`、`libkeiland/ui/chooser.c` が `kl_system_media_list` の一覧を file の一覧の形で出す）。選んだ物を下書きに置く仕組み（`phone/media.c`）・DnD・下書きのサムネイルはある。

## 範囲と条件

- 「＋」→ 2 つの button（写真を選ぶ／ファイルを選ぶ）。写真 → libkeiland の UI の新しい部品（media picker の popup: `kl_system_media_list` の写真・動画をサムネイルの格子で出し、複数選択して返す）。ファイル → 普通の file chooser（OPEN）。
- media picker は libkeiland の公開 API（keiland.h、KL_VERSION を上げる）。Phone 以外の app も使える形。OS の依存は持たない（Guardrail の「app と設定」）。
- host 試験（picker の model・選択の返し）、named build warning 0。GUI の確認は T1（QEMU）とユーザーの UAT。
