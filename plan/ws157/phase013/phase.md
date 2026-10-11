<!-- awesome-plan project=zedbsd record=ws157-p013 -->

# ws157-p013: mediastorage の解析を keiland-preview の jail へ・大きな取り込み

Parent: [WS157](../ws.md)
Status: in-progress（q937 の続き、P1。実装・host 試験済み、QEMU は T1 へ）
Phase disposition: normal
Queue: q937（[P1 の lane](../../agents/P1/queue.md)）

## 由来

2026-10-11 ユーザー:「mediastorageがメディアファイルをパースするのは権限的に避けたいです。既存のjailがありますよね、サムネイルで使っている。あれの仕組みを使ってほしいです。できればサムネイルのCLIもそのまま流用するのがよさそう。」→ [p012](../phase012/phase.md) の J1〜J7 と #4 をユーザーが承認（「全部承認です。」[ws.md](../ws.md) の末尾）。

## 実装

| 所 | 変更 |
| --- | --- |
| `userland/desktop/preview/info.c`（新） | `keiland-preview --info`: 入力の先頭 4 MiB（`PREVIEW_INFO_MAX`）を pread で読み、種類（`kl_media_kind`）、PNG の IHDR・GIF の画面・JPEG の最初の frame header の寸法、JPEG の APP1 の EXIF の日時（`photos/exif.c` の `ph_exif_date`、ph_time の秒）を text で出す（`keiland-preview-info 1`・`kind=`・`width=`・`height=`・`taken=`）。decode はしない |
| `preview/preview.h`・`main.c`・`make.c`・`zedbsd/spawn.c`・`linux/spawn.c` | `request.info`、`--info` の引数（他の引数と同時は使い方の誤り）、`preview_make` が info を `preview_info` へ回す（FreeBSD の process の中の経路も同じ） |
| `preview/client.c`・`client.h` | `preview_info_read(fd, &info)`: 一時の出力（名前は消す）、子を `PREVIEW_LIMIT_MS`（5 秒）まで待つ、答えを信用せずに読む（最初の行の版、kind の名前、寸法 0〜1,000,000、日時は整数、知らない名前は無視、悪い答えは何も残さず EPROTO） |
| `photos/import.c` | 寸法と日時は `preview_info_read` の答え（種類が先頭の magic と同じ時だけ）、失敗なら寸法 0・取り込みの日（J4）。複写は `<library>/.incoming/import-XXXXXX` に書いて全部書けたら rename（殺されても Files/ に半端が残らない）、`ph_import_clean` が `.incoming` を空に |
| `mediastorage/main.c` | add・add-list の前（lock の中）に `ph_import_clean` |
| `libkeiland-backend/media/media.c` | add の時間の上限 = 120 秒 ＋ 名前の file の大きさ 8 MiB ごとに 1 秒、folder か 1 時間で頭打ち（`media_import_limit`、stat） |
| `libkeiland/system/system.c` | add の待ちは 3605 秒、他は今の 125 秒（p014 の非同期の API までのつなぎ） |
| Makefile | mediastorage（zedBSD・Linux・FreeBSD）から `dimensions.c` を外し preview の client と spawn を、zedBSD は `desktop/preview` を require。preview（zedBSD・Linux）に `info.c`・`photos/exif.c`。Files・Settings の FreeBSD（preview を process の中で使う）にも `info.c`・`exif.c` |

`mediastorage/dimensions.c`・`dimensions.h` はもう使わない（削除は Q1 に依頼）。SHA-256 は mediastorage のまま（J5）。

## 確認

| コマンド | 結果 |
| --- | --- |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk … bin/mediastorage bin/keiland-preview dynamic/libkeiland.so bin/files bin/settings bin/wayland` | rc 0、warning 0 |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-q937-linux all` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-preview-info.sh build/ws157-q937-linux`（新） | all PASS: Linux の preview（seccomp）で PNG 37×19、GIF 21×13、EXIF 付き JPEG 640×480・taken 1700000000、EXIF 無し JPEG（taken 空）、切れた JPEG（寸法 0）、SOF だけの JPEG、動画の先頭（kind=video）、雑音・空（kind=none）。reader: 良い答え、版・kind・寸法・日時の悪い答え（EPROTO、何も残さない）、失敗の status（EINVAL）、30 秒寝る子（5 秒で ETIMEDOUT）、本物の子 |
| `sh plan/ws157/tests/run-host-mediastorage.sh build/ws157-q937-host build/ws157-q937-linux`（preview の wrapper を足した） | 全 PASS（寸法・EXIF の日付は jail の答えから、`.incoming` の古い複写が次の取り込みで消え Files/ に `import-*` が無い） |
| `sh plan/ws157/tests/run-host-photos-db.sh … [LINUX_BUILD]`（同じ wrapper） | PASS |
| `plan/tools/style-check.py`（変えた C） | 新しい指摘 0 |

未実施: FreeBSD の build（FreeBSD の guest で）、QEMU（T1）。`plan/ws157/tests/run-host-photos.sh` は main の時点で既に 10 FAIL（並びの期待が古い、この変更の前から）で、cleared の Phase からだけ参照 → 削除の候補として Q1 へ。

## T1 への依頼（案）

image = current-uat（main に統合の後）。guest で `mediastorage add` に JPEG（EXIF の日時付き）・PNG・動画を入れ、`mediastorage list` の行の寸法と日付（EXIF の日の folder）を確かめる。`ps` で add の間に `keiland-preview` が sandbox で起きること（`--info`）。合格: 寸法と日付が入り、mediastorage の process が画像を開く以外に解析しない（source の確認で代える）。
