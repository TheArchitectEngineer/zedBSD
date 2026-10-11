<!-- awesome-plan project=zedbsd record=ws157-p016 -->

# ws157-p016: 動画の寸法と撮影日時を mp4 の header から jail の `--info` で読む

Parent: [WS157](../ws.md)
Status: in-progress（P1。実装と host の試験・build まで、T1 の確かめ待ち）
Phase disposition: normal
Queue: P1 の lane（Q1 が 2026-10-11 に追加）

## 由来

T1-538 で、取り込んだ動画の寸法が 0、日付が取り込みの日になった。2026-10-11 ユーザー「OKです」（Q1 経由: 動画の寸法と撮影日時を mp4 の moov の mvhd・tkhd から jail（`keiland-preview --info`）の中で読む）。p013 の続き（mediastorage は media を解析しない、ユーザーの決定のまま）。

## 設計と実装

| 所 | 決め |
| --- | --- |
| MP4・QuickTime（`userland/desktop/preview/info.c`） | jail の中で、file の box を pread でたどる（先頭の 4 MiB の buffer ではなく、moov が mdat の後ろ、faststart でない file でも読める）。top-level の box → `moov` → `mvhd`（version 0・1 の creation time、1904 年からの秒）と各 `trak`（`tkhd` の幅・高さ 16.16、`mdia`→`hdlr` の handler が `vide` の track だけ）。64 bit の size（size 1）と、末尾まで（size 0）を扱う。box は親の中に収まらなければ止め、たどる box は 512 まで、辺は 65535 まで |
| 回転 | `tkhd` の matrix が 1/4 回転（a = 0、b ≠ 0）なら幅と高さを入れ替える（見える向きの寸法。縦に撮ったスマホの動画は 1080×1920） |
| 日時 | mvhd の creation time は UTC（MP4 の決まり）。0 と 1970 年より前は「不明」。出力に `zone=utc` の行を足し（日時がある時だけ）、呼び手（`photos/import.c`）が `import_local` で地方時の暦に直す（EXIF と同じく「カメラの暦」として持つ。jail は時間帯を知らないので、変換は jail の外） |
| AVI | `avih` の幅・高さ（先頭の buffer から）。日時は無い（IDIT は読まない） |
| client（`preview/client.c`・`client.h`） | `zone` の行を読む: `utc` だけ受け、他の値は EPROTO。`struct preview_info` に `utc`。古い client は知らない行を無視する（`zone` を足しても前の版と互換） |
| 出力の版 | `keiland-preview-info 1` のまま（足した行は任意） |

## 変えた file

- `userland/desktop/preview/info.c`、`userland/desktop/preview/client.c`・`client.h`、`userland/desktop/photos/import.c`
- 試験: `plan/ws157/tests/run-host-preview-info.sh`（MP4・AVI の例）、`plan/ws157/tests/host-preview-info.c`（`utc`・`zone` の例）

## 確かめ（2026-10-11 P1、host）

| コマンド | 結果 |
| --- | --- |
| `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-q937-linux -j16 all` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-preview-info.sh build/ws157-q937-linux` | all PASS。Linux の preview（seccomp）で: 5 MiB の mdat（64 bit の size）の後ろの moov、音の track が先の 1920×1080・taken 1700000000・`zone=utc`。version 1 の mvhd・tkhd で 1/4 回転 → 1080×1920・taken 1700086400。moov の途中で切れた file → 寸法 0。creation 0 → 640×480・taken 空・`zone` 無し。AVI 720×576。reader: `zone=utc` を受ける、`zone=jst` は EPROTO、前の例は全部そのまま |
| `make BUILD=build/ws157-q937 ZEDBSD_CONFIG=config/current-uat.mk -j16 …/bin/keiland-preview …/bin/mediastorage …/bin/photos …/bin/files …/bin/settings` | rc 0、warning 0 |
| `sh plan/ws157/tests/run-host-mediastorage.sh build/ws157-q937-host build/ws157-q937-linux` | rc 0、全 PASS |
| `python3 plan/tools/style-check.py`（info.c・client.c・import.c） | 指摘 0 |

## 未実施

- FreeBSD の build（preview を process の中で使う Files・Settings の FreeBSD の Makefile は info.c をもう含む、変更は info.c の中だけ）。
- T1（QEMU、current-uat の image）: guest で `mediastorage add` にスマホの mp4（縦と横）を入れ、`mediastorage list` の行の寸法と日付（撮影の日の folder、地方時）を確かめる。Photos の動画の card の日付の並び。合否: 寸法が 0 でなく、日付が取り込みの日でない。
- 実機のスマホの動画（ユーザーの UAT、T1-538 の続き）。

## 残り

- QuickTime の `com.apple.quicktime.creationdate`（時間帯の付いた地方時）は読まない（mvhd の UTC を地方時に直すので、撮った所と今の時間帯が違うと日がずれうる）。要れば後の Phase。
- WebM・MKV は `kl_media_kind` の動画に入っていない（範囲の外）。
