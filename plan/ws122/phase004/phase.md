<!-- awesome-plan project=zedbsd record=ws122-p004 -->

# ws122-p004: 独自の container と libavcodec の dlopen の add-in で player を完成させる

Status: cleared（2026-10-05 Q1: T1-191 PASS（QEMU、dlopen の libavcodec（major 63）で再生・seek、libavcodec が無い時の文）。実機は UAT）。以前: in-progress（実装と host の試験は済み・T1 の試験待ち）
Disposition: normal
Parent: [WS122](../ws.md)
Queue: q765（Q1、2026-10-05 夕、P2 g15）
依存: p003（mediafile）

## 範囲（2026-10-05 夕のユーザーの決定）

「GPUデコードはあとで、libavcodecをdlopenしてソフトウェアデコードにする仕様で、まず完成させます。GPUデコードが別WSで完成したら、それをVulkan Video Extensionで利用します。」→ 独自の container（p003）＋ dlopen の libavcodec の software decode で player を完成させる。libavcodec が無ければ再生できない旨を出す。dlopen の field offset の license: 公開の header の構造体の layout だけを使い code を写さない前提（Q1 の伝達）。Q1 の注意: AVPacket・AVFrame の先頭の field の layout は major ごとに確かめ、知らない major なら断る。

## 実装（2026-10-05 夕）

- **player は FFmpeg に link しない**（`platform/amd64/vmunix.mk` の videoplayer の rule から FFmpeg の include・link・stage の依存を外した。NEEDED は libvulkan・libwayland-client・libkeiland・libtruetype・libc だけ）。demux は `userland/desktop/mediafile`（p003）を player に link。
- **add-in**（`userland/desktop/videoplayer/codec.c`）: `dlopen` で `libavcodec.so.<major>`・`libavutil`・`libswscale` を開き、`dlsym` で関数を取る（宣言は自分で書く）。知っている版の組は FFmpeg 9（63・61・10、image の package）と FFmpeg 7（61・59・8、Debian 13）だけで、`avcodec_version()` の major が soname と違う、または知らない版なら断る（`VP_CODEC_VERSION`）。無ければ `VP_CODEC_MISSING`。
  - 構造体は AVPacket（buf・pts・dts・data・size・stream_index・flags）と AVFrame（data[8]・linesize[8]・extended_data・width・height・nb_samples・format）の先頭だけ（`codec-layout.h`）。AVCodecContext の field は使わず、設定は option の名前（`threads`・`ar`・`ch_layout`）、codec は名前（`avcodec_find_decoder_by_name`: h264・hevc・libdav1d/av1・vp9・vp8・mpeg4・aac・opus・mp3float/mp3）、pixel・sample の形式は名前（`av_get_pix_fmt("bgra")`・`av_get_sample_fmt_name`）。enum の値を写さない。
  - extradata を渡さないので、設定を stream に入れる（`bitstream.c`）: H.264・H.265 は avcC・hvcC から Annex B にして key frame の前に parameter sets、AAC は AudioSpecificConfig から ADTS の header、MPEG-4 Part 2 は VOS/VOL を key frame の前に。
  - 絵の時刻は送った packet の時刻の小さい順（AVFrame の pts を読まない）、音の時刻は最初の packet の時刻と標本の数から。音は自前で 16-bit stereo・audiod の rate に（線形の補間、最初の 2 channel）。
  - 絵の縦横比は正方の画素とする（sample aspect ratio は AVFrame の奥の field なので読まない。制限）。
- **window**（`main.c`）: 開けない時の文を画面の中央に（「Playing video needs FFmpeg's libavcodec, which is not installed.」「This version of libavcodec is not supported.」「The video's format is not supported.」）。log `NOTICE problem= text=`・`CODEC load error= major= reason=`・`OPEN ... container=`。

## 検証（2026-10-05 夕）

- layout: `plan/ws122/tests/host-layout.c` を Debian の FFmpeg 7（libavcodec-dev、major 61）と image の FFmpeg 9.0.2 の staged header（major 63）の両方で compile して PASS（AVPacket・AVFrame の各 field の offset が一致、AV_NUM_DATA_POINTERS=8、AV_PKT_FLAG_KEY=1）。試験だけが FFmpeg の header を使う。
- host の decode: `sh plan/ws122/tests/run-host-codec.sh`（ASan・UBSan、host の FFmpeg 7 を dlopen）PASS: sample.mp4（mpeg4・aac、500 絵・20.03 s の音・seek で 10.0 s）、host の ffmpeg で作った H.264+AAC の MP4 と MKV（B frame あり、100 絵・順の乱れ 0）、H.265+AAC の MP4、VP9+Opus の WebM。各々 BGRA への縮小で絵が描かれ、音は長さの 9 割以上、seek の後の最初の絵は中央の手前の key frame。
- `run-host-mediafile.sh` PASS（p003 の回帰）。
- build: zedBSD の `bin/videoplayer`（`config-amd64-p002.mk`）warning 0、NEEDED に FFmpeg 無し。style-check: codec.c・bitstream.c・codec-layout.h 0、media.c・main.c 0。
- guest の試験 `plan/ws122/tests/videoplayer-p004.sh` を作った（p002 の試験に加え major 63 の読み込み、container=mp4、libavcodec を退けた時の文と PNG）。
- 未実施: QEMU（T1）、実機。host の試験の作成に host の Debian へ `libavcodec-dev`・`libavutil-dev`・`libswscale-dev`（header の確かめ）と `ffmpeg`（試料の作成）を apt で入れた。
