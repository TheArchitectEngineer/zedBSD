<!-- awesome-plan project=zedbsd record=ws157-p014 -->

# ws157-p014: libkeiland の media の API を非同期に・picker と chooser はすぐ窓を開く

Parent: [WS157](../ws.md)
Status: in-progress（q937 の続き、P1。実装・host 済み、QEMU は T1 へ）
Phase disposition: normal
Queue: q937（[P1 の lane](../../agents/P1/queue.md)）

## 由来

[p012](../phase012/phase.md) の review の #1（app の media の API が同期で、Photos の印の保存・Phone の MMS の保存・picker・chooser が窓の thread で最長 125 秒止まる）。2026-10-11 ユーザー「全部承認です。」（d: libkeiland の media の API を非同期に、picker・chooser はすぐ窓を開いて Loading…）。

## 実装

| 所 | 変更 |
| --- | --- |
| `include/keiland/keiland.h`（KL_VERSION 88）・`libkeiland/system/system.c`・`exports.map` | 待たない形: `kl_system_media_list_begin`・`_add_paths_begin`・`_remove_ids_begin`・`_apply_begin`（request の番号を返す、`KL_SYSTEM_MEDIA_PENDING_MAX` = 8 まで同時、越えると EBUSY）、答えは `KL_SYSTEM_CHANGED_MEDIA_DONE`（`kl_system_dispatch`）の後に `kl_system_media_take`（番号・errno・成功なら snapshot の fd）、`kl_system_media_cancel`（答えは捨てる）。内部は 1 つの slot から 8 つの pending の表へ。待つ形（list・add_paths・remove_ids・apply）は同じ表の上の「begin して自分の番号を待つ」、待ちの切れた request は cancel。protocol は変えない（request の番号は前から非同期） |
| `libkeiland/ui/picker.c`・`picker.h`・`picker-view.c` | 窓をすぐ開き、list は `_begin`、frame ごとに答えを見る（来るまで「Loading…」と frame を続ける）、来たら items を読み thumbnail の thread を始める。読めない library は窓の中に「The media library is unavailable」 |
| `libkeiland/ui/chooser.c`・`chooser.h`・`chooser-view.c` | media の mode も同じ（今は使う app が無い） |
| `photos/main.c` | 印・album の保存（`ph_marks`）は `_apply_begin`。送った photo・album の id を覚えて changed を消し、失敗なら印を付け直す。答えを待つ間の refresh と削除は答えの後に（`media_again`・`remove_after`）。窓を閉じる時は答えを数秒待ってから残りを待つ形で保存 |
| `phone/receive.c`・`phone.h` | `ph_receive_media` を `ph_import_prepare`（MIME を読み一時の原本を書いて hash）・`ph_import_begin`（`_add_paths_begin`）・`ph_import_finish`（snapshot から hash で path）・`ph_import_free` に分けた（待つ形も残す） |
| `phone/main.c` | 受けた MMS と送る MMS の media の取り込みは待たない: 窓の list に積み、8 つまで begin、答えで item（serial で探す）に media の path を入れる、失敗は notice（受けた物は同期の pass が走っていれば失敗にしてやり直す）。compositor が去ったら・窓を閉じる時は一時の原本を消す |

## 確認

| コマンド | 結果 |
| --- | --- |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk … bin/phone bin/photos dynamic/libkeiland.so bin/wayland` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-q937-linux all` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-mediastorage.sh build/ws157-q937-host build/ws157-q937-linux` | 全 PASS（wire: 2 つの list を待たずに出して両方の答え、3 つ目を cancel して何も残らない。receive: 待たない取り込みが待つ形と同じ path） |
| `sh plan/ws197/tests/picker-model-host-test.sh` | PASS |
| `python3 userland/desktop/libkeiland/exports.py --check`、`plan/tools/keiland-os-boundary/check.sh` | ok、PASS |
| `plan/tools/style-check.py`（変えた C） | 新しい指摘 0 |

未実施: QEMU（T1）: picker が初回もすぐ「Loading…」で開き一覧に変わる、Photos の印（favorite）を付けて閉じて開き直すと残る、Phone の MMS の受信で写真が出る（実機の Android は p008）。
