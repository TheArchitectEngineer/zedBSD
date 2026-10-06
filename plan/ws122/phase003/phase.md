<!-- awesome-plan project=zedbsd record=ws122-p003 -->

# ws122-p003: 独自の container の読み込み（MP4・Matroska/WebM の demux）

Status: test-wait（2026-10-07 q831 P2: 実装と host の試験は済み、p004 が player に接続し T1-191 PASS（mp4 の container で再生・seek）。cleared の判定は Q1）
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

## 実装（2026-10-05、P2）

- `userland/desktop/mediafile/`: `mediafile.h`（公開の API）、`mediafile-private.h`（reader の間の共有）、`mediafile.c`（open、先頭の byte で形式を見分ける、`pread` の helper、時刻の換算）、`mp4.c`（ISO BMFF）、`mkv.c`（Matroska・WebM）。
  - MP4: `moov` を丸ごと読み（上限 64 MiB）、track ごとに sample の一覧（offset・大きさ・dts・cts・sync）を作る。packet は file の中の offset の順。edit list の最初の edit で pts をずらす（空の edit の遅れも）。`stz2` は 4・8・16 bit、`co64` も読む。seek は最初の video track の、時刻以前の最後の sync sample。他の track はその dts 以前の最後の sample から。
  - Matroska: EBML header の DocType（matroska・webm）、Segment の Info・Tracks・SeekHead・Cues（cluster の後ろの cues は SeekHead から）。cluster を順に読み、SimpleBlock と BlockGroup（ReferenceBlock があれば keyframe でない）。Xiph・fixed・EBML の lacing は 1 frame を 1 packet に。知らない track の block は飛ばす。dts は pts と同じ（Matroska は pts だけ持つ）。seek は cues、無ければ cluster の時刻を順に読む。seek の後、video track は keyframe まで packet を捨てる。
  - 壊れた file: box・element の大きさを親と file の大きさで確かめ、拒む（EINVAL）。index の sample の数の上限は 1,600 万、packet の大きさの上限は 64 MiB、private data は 1 MiB、cue は 100 万。
  - 置き場所: 当面は player に link する object（共有の library にすると sysroot の header が要る）。この Phase では player にはまだつないでいない（p004 で decode の道と一緒に）。
- 確かめ（host）:
  - `sh plan/ws122/tests/run-host-mediafile.sh`（ASan・UBSan）: `make-media.py` が作った 6 つの case（mp4-narrow・mp4-wide・mkv-cues・mkv-scan、壊れた bad-cut・bad-junk）で、track・全 packet（track・pts・dts・keyframe・大きさ・byte の和）・seek の後の 3 packet が期待と一致。試料 `sample.mp4`（mpeg4 と AAC、20 秒）で track 2 つ、video 500・audio 939 packet、10 秒への seek で keyframe の pts=10000000。`host-mediafile: PASS`。
  - 変異の試験（手で 1 回、host、ASan・UBSan・leak）: 5 つの file の切り詰めと byte の書き換え 640 通りで、crash・ASan・UBSan の報告 0。
  - zedBSD の target の clang（amd64 の sysroot）で 3 つの file が warning 0 で compile できる。style-check は違反 0。
- 未実施: QEMU（player につないだ後の p004 で）。fragmented MP4、MPEG-TS・Ogg・AVI。

## 2026-10-07 q831 P2: 記録

- 成果は p004（cleared、T1-191: `container=mp4`、dlopen の libavcodec で再生・seek）と WS120 の Music（m4a の音の track）が使っている。fragmented MP4・MPEG-TS・AVI・Ogg は範囲外のまま（[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS122 の行）。
