<!-- awesome-plan project=zedbsd record=ws197-p014 -->

# ws197-p014: Phone の「＋」の添付の選択（写真の popup と file chooser、libkeiland の UI）

Parent: [WS197](../ws.md)
Status: in-progress（実装・host 試験・build 済み、GUI は T1 待ち）
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

## 実装（2026-10-11 P1、q926-i01）

- libkeiland: `kl_media_picker_open`・`kl_media_picker_destroy`、`struct kl_media_item`・`kl_media_picker_options`・`kl_media_picker_listener`（keiland.h、KL_VERSION 85、exports.map は exports.py で生成）。`ui/picker.c`（window・glass・sheet、file chooser と同じ寿命）、`ui/picker-model.c`（snapshot の読み、撮影日の新しい順、選択と上限と番号、答え、サムネイルの thread と共有 cache の読み書き）、`ui/picker-view.c`（card・grid・選択の番号の円・動画の印・Cancel と Add・Esc／Enter／矢印／Space）。Makefile・Makefile.linux・Makefile.freebsd に 3 file。
- Phone: 「＋」→ composer の上の card に「Photos & Videos」「File…」（外の click・Esc で閉じる）。Photos & Videos → picker（上限は下書きの空き、サムネイルは `ph_decode`＋`ph_thumbnail` を picker の thread で）。File… と menu の「Attach File...」→ 普通の file chooser（OPEN、filter「Photos and Videos」「All Files」、写真・動画でない file は「Only photos and videos can be attached.」）。menu に「Attach Photo or Video...」。
- A4（keiland-os-boundary）: `plan/tools/keiland-os-boundary/app-allow.tsv` に Phone の viewer の posix_spawn の行（Q1 の許可、ecbed1686）。check.sh PASS。

### 確認

| コマンド | 結果 |
| --- | --- |
| `make -j16 BUILD=build/ws197-p014 ZEDBSD_CONFIG=config/current-uat.mk … build/ws197-p014/{dynamic/libkeiland.so,bin/phone,bin/wayland,bin/photos}` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws197-p014-linux …/lib/libkeiland.so …/bin/mediastorage` | rc 0、warning 0（gcc） |
| `sh plan/ws197/tests/picker-model-host-test.sh`（新、ASan/UBSan） | PASS ×3: 並び、album の行を除く、選択の番号と上限と詰め、答えの順と動画の判定、答えの後は不変、thread のサムネイル（写真 2 つで decoder 2 回、動画は icon）、cache の書き込み、2 つ目の picker は cache から（decoder 0 回、色が一致）、単一選択の移動、double click、cancel、上限超え EINVAL、壊れた snapshot 4 種 |
| `sh plan/ws157/tests/run-host-mediastorage.sh <tmp> build/ws197-p014-linux` | 全 PASS（media selection の既存の試験を含む） |
| `python3 plan/tools/style-check.py`（新 4 file と試験） | 0 |
| `exports.py --check`・`git diff --check`・`keiland-os-boundary/check.sh` | PASS |

未実施: FreeBSD の build（host が無い）、QEMU の GUI（T1 に依頼）: Phone の「＋」→ 2 button、Photos & Videos で grid にサムネイル（cache 無しで decode、2 回目は cache）、複数選択の番号、Add で下書きに並ぶ、Cancel、File… で file chooser、写真でない file の notice。
