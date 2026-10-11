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

## 設計（2026-10-11 P1、q926-i01）

技術の選択は委ねられた範囲で P1 が決めた（変えたい時は Q1 経由で）。

| ID | 決め | 理由 | 他の案 |
| --- | --- | --- | --- |
| M1 | 「＋」は composer の中の小さな card（「＋」の上）に 2 つの button「Photos & Videos」「File…」を出す。外を押す・Esc・もう一度「＋」で閉じる。menu の「Attach File...」は File の chooser、新しい「Attach Photo or Video...」は picker | ユーザーの言う「2 つの button」。Phone の他の部分と同じく view の中に描けば window の追加が要らない | `kl_menu_popup` の 2 項目の context menu |
| M2 | picker は libkeiland の公開部品 `kl_media_picker_*`（KL_VERSION 85）。file chooser と同じ形: 自分の toplevel（親があれば sheet）、wl_shm に CPU で描く、答えは listener に 1 回、`wl_display_sync` の後 | 既存の chooser と同じ寿命と約束で、app の作りが同じになる | app の window の中の overlay（app ごとに描画の統合が要る） |
| M3 | 一覧は `kl_system_media_list` の snapshot（metadata だけ）。並びは撮影日の新しい順。kinds の option で写真だけ・動画だけ・両方。複数選択（click で選択を反転、上限 `limit`、既定 1 = 1 つ選んで Add、double click で即決） | picker は library の DB を読まない（Guardrail「app と設定」）。MMS は複数の添付を送る | 1 つだけ |
| M4 | サムネイルは共有の cache `$XDG_CACHE_HOME/keiland/photos/<id>.ppm`（Photos の形式: 160×160 の P6、回転なし）を読む。無い物は app が渡す任意の callback（`thumbnail(data, path, side, image)`）を picker の thread で呼び、できた物を同じ cache に書く（Photos も次から使える）。callback が無い・失敗した・動画は icon | libkeiland に JPEG・GIF の decoder を入れない（全 program の依存が増える）。Phone は Photos の decode.c を既に link している | libkeiland が libjpeg-compat 等を link |
| M5 | 種類（写真／動画）は picker の thread が file の先頭 16 byte を `kl_media_kind`（picture/media-kind.h）で読む。分かるまでは写真の icon | Phone の下書きと同じ分類 | 拡張子 |
| M6 | 答え: `KL_MEDIA_PICKER_CHOSEN` と `struct kl_media_item`（id・path・name・video）の配列（選んだ順）、または CANCELLED。配列は callback の間だけ有効 | file chooser の path と同じ寿命 | 配列を app に渡して app が解放 |

A4（keiland-os-boundary、`phone/main.c` の attachment の viewer の posix_spawn）: Settings・PDF viewer の先例（app-allow.tsv の「a program starting another; F-085」）と同じ扱いの行を足す案を Q1 に送る。
