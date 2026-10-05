<!-- awesome-plan project=zedbsd record=ws122-p003 -->

# ws122-p003: 独自の container の読み込み（MP4・Matroska/WebM の demux）

Status: in-progress（2026-10-05、P2。設計）
Disposition: normal
Parent: [WS122](../ws.md)
Queue: Q1（ベータ2 の割り当て、P2 の 2 番目）
依存: p002（今の player）

## 範囲

ws.md の段 2 の前半: FFmpeg を使わずに file を読み、track と packet（圧縮されたままの frame、時刻つき）を取り出す library。decode はこの Phase に入らない（下の「段 2 の残りと決めが要る点」）。

- **形式**: MP4・MOV（ISO BMFF。`moov` の sample table: `stsd`・`stts`・`ctts`・`stsc`・`stsz`/`stz2`・`stco`/`co64`・`stss`、`elst` の始まりのずれ。fragmented MP4（`moof`）は後）と Matroska・WebM（EBML: `Segment`・`Info`・`Tracks`・`Cluster` の `SimpleBlock`・`BlockGroup`、`Cues` でのシーク、lacing）。
- **codec の識別**（decode の道を選ぶための名前と codec の private data）: H.264（`avc1`/`avc3`・`avcC`、`V_MPEG4/ISO/AVC`）、H.265（`hvc1`/`hev1`・`hvcC`、`V_MPEGH/ISO/HEVC`）、AV1（`av01`・`av1C`、`V_AV1`）、VP9（`vp09`、`V_VP9`）、MPEG-4 Part 2（`mp4v`）、AAC（`mp4a` の `esds`、`A_AAC`）、Opus（`Opus`・`dOps`、`A_OPUS`）。他は「知らない codec」として track だけ返す。
- **API**（`userland/desktop/mediafile/mediafile.h`、接頭辞 `mf_`）: `mf_open(path, &file)`、`mf_track_count`・`mf_track`（種類、codec、時間の単位、幅・高さ、標本化率・channel、private data）、`mf_read(file, &packet)`（file の中の順で次の packet: track、pts・dts（µs）、keyframe、data）、`mf_seek(file, time_us)`（その時刻以前の keyframe へ）、`mf_close`。file の読みは `pread`、memory は file の大きさに比例しない（index は track の sample の数に比例）。
- **置き場所**: 当面は player の program に link する object（`userland/desktop/mediafile/*.c`）。共有の library にするのは browser（WS121）が使う時（sysroot の header の追加が要るため、今は避ける）。
- **入らない**: decode、player の切り替え（p004 以降）、fragmented MP4、MPEG-TS・AVI・Ogg。

## 試験

- host: `plan/ws122/tests/host-mediafile.c` と python の生成器で、既知の sample（大きさ・offset・時刻・keyframe）を持つ MP4 と MKV を作り、読み戻して一致を見る（壊れた box・EBML の長さ・大きすぎる値の拒否も）。今の試料 `sample.mp4`（mpeg4 25 fps 500 frame・AAC 48 kHz）の track と packet の数・時刻の範囲・mdat の範囲の内側。
- guest（T1）: 後の Phase で player を切り替えた時。

## 段 2 の残りと決めが要る点（Q1 経由でユーザーへ）

1. **libavcodec 無しの decode は WS083 が要る**: 「独自の container ＋ 動画の再生の支援（GPU の decode）だけ」で再生するには、Vulkan Video（[WS083](../../ws083/ws.md)、planning、P1 の列の 12 番目、実機）が要る。QEMU（Venus）では GPU の decode が使えない見込みで、それまで libavcodec 無しでは絵が出ない。
2. **音**: GPU の decode は動画だけ。libavcodec 無しで音を出すには、AAC の decoder を自分で書くか（大きい）、Opus なら BSD の libopus を外部 package にするか。それまで、libavcodec 無しの時は音無しで再生するか。
3. **add-in の宣言**（段 3）: header を使わずに `dlsym` で呼ぶには、AVFrame・AVPacket・AVCodecContext のうち使う field の位置を自分で書く（AVFrame の `data`・`linesize`・`width`・`height`・`format` は先頭にあり major 版の中で動かない）。field の位置という事実の記述で、FFmpeg の header の写しではない、と考えるが、license の判断はユーザーに確かめたい。extradata は H.264 を Annex B に、AAC を ADTS にして packet の中で渡せば、AVCodecContext の field を書かずに済む見込み。
