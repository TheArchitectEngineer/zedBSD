<!-- awesome-plan project=zedbsd record=ws122-p002 -->

# ws122-p002: 簡単な動画 player（開く・再生・一時停止・停止・シーク）

Status: in-progress（2026-10-05、P2。実装と build まで済み、QEMU の試験は T1 に依頼、判定は Q1）
Disposition: normal
Parent: [WS122](../ws.md)
Queue: Q1 の指示（2026-10-05、ベータ1 の RC 10/13 までに簡単な player、ユーザーの決定）
依存: [p001](../phase001/phase.md)（`multimedia/libavcodec` の package）

## 範囲

- libkeiland の `kl_app` で書く player `videoplayer`（`userland/desktop/videoplayer/`）。FFmpeg の library（p001 の package、LGPL）で demux・decode し、絵を libkeiland の canvas に、音を audiod に出す。
- できること: 開く（引数の file、または File > Open の file chooser）、再生・一時停止、停止（先頭で一時停止）、10 秒の前後のシーク（key と menu）、bar の slider でのシーク、全画面。
- 範囲の外: GPU の decode（WS083、ベータ2）、独自の container の読み込みと dlopen の add-in（ws.md の段 2・3）、字幕・音量・playlist、App Home・Files への登録（下の「残り」）。

## 設計

| file | 役 |
| --- | --- |
| `videoplayer.h` | `struct vp_audio`（audiod の stream）、`struct vp_media`（読み込みの thread、絵の ring 8 枚、時計、シークの要求）、関数の宣言 |
| `audio.c` | audiod の client（`userland/base/audiod/protocol.h`）。`/run/audiod.sock` に HELLO・STREAM_CREATE（48 kHz・S16・stereo、buffer 0.5 s、period 1/50 s）、SCM_RIGHTS で受けた共有の ring に書き、START・STOP・FLUSH。書く前に REQUEST・UNDERRUN の event を読み捨てる（audiod は送れない client を切る）。 |
| `media.c` | 読み込みの thread: `avformat_open_input`・`av_find_best_stream`・decoder（thread 数は FFmpeg に任せる）・`swr` で音を S16 stereo に。絵は時刻つきで ring に置き、満ちたら待つ。音は audiod の ring の空きに合わせて書く。シークは `avformat_seek_file`・decoder と音の flush・その時刻より前の絵と音を捨て、時計をそこに据え直す。時計は音のある時は audiod の読み位置、無い時は monotonic。 |
| `main.c` | window「Video Player」（app_id `videoplayer`、960x600、Vulkan の present）、menu（File: Open・Close・Quit、Playback: Play/Pause・Stop・Back 10 s・Forward 10 s、View: Full Screen）、key（Space・Left・Right・F・Esc・Ctrl+O/W/Q）、時刻の来た絵を `sws` で BGRA にして縦横比を保って canvas に、下の bar（再生 button・時刻・slider、slider のシークは 150 ms ごとに間引く、pointer が動いて 3 s 出る）。 |

- build: `platform/amd64/vmunix.mk` の `DYNAMIC_VIDEOPLAYER_OBJS`（libavcodec の stage の header を `-isystem`、stage の後に build）と link（`-l:libavformat.so.63 -l:libavcodec.so.63 -l:libswscale.so.10 -l:libswresample.so.7 -l:libavutil.so.61`、Vulkan の NEEDED の確かめ）。package `desktop/videoplayer`（amd64、requires に `multimedia/libavcodec`）。
- **audiod の client は player に内蔵した**（Q1 の指示で可）。他の app（Settings の音の試し、将来の browser の音）でも同じ client が要るので、**後で共有の library（例: `libaudiod-client`）に移す候補**。移す時は `audio.c` の `vp_audio_*` をそのまま API にできる形にしてある（socket・serial・shm の ring・event の読み捨て）。Future Work への登録は Q1 に依頼する。
- 試験のための log（標準 error、`VIDEOPLAYER ` で始まる）: `READY`、`AUDIO error=`、`OPEN path= width= height= duration_ms= video= audio=`、`OPENED path= error=`、`PLAY shown= time_ms=`、`PAUSE shown= time_ms=`、`STOP`、`SEEK to_ms=`、`SEEK done to_ms=`、`FRAMES shown= time_ms=`（1 枚目と 100 枚ごと）、`END reached`、`ENDED shown=`、`DONE reason=`。option: `--width`・`--height`・`--timeout-s` と file。

## 試験

- 試料 `plan/ws122/tests/sample.mp4`: 20 s、320x240 の MPEG-4 Part 2（Simple Profile）25 fps と AAC LC 48 kHz stereo（440 Hz）、375352 byte。host で build した FFmpeg 9.0.2 の `testsrc`・`sine` で作った（`ffmpeg -f lavfi -i testsrc=size=320x240:rate=25:duration=20 -f lavfi -i sine=frequency=440:sample_rate=48000:duration=20 -c:v mpeg4 -q:v 24 -g 50 -c:a aac -b:a 32k -ac 2 -movflags +faststart`）。H.264 は使わない（試料の encoder が要らず、decode の経路は FFmpeg の中で同じ）。
- image: `plan/ws122/tests/config-amd64-p002.mk`（Files の image ＋ `libavcodec videoplayer`）を `FILES_CONFIG=plan/ws122/tests/config-amd64-p002.mk FILES_EXTRA='--file /usr/share/videoplayer-tests/sample.mp4=plan/ws122/tests/sample.mp4' plan/tools/files/build-files-image.sh BUILD`。
- guest: `plan/ws122/tests/videoplayer-guest.sh start IMAGE`（zdesktop-guest.sh の Venus の guest に ICH9 HDA と QEMU の wav の録音 `build/ws122-run/sound.wav` を足した物）。
- 試験: `plan/ws122/tests/videoplayer-p002.sh`。開く・再生（FRAMES 1・100）、Space の一時停止と再生（その間に絵も時計も進まない）、Right のシーク、終わり（END・ENDED）、終わりからの Left、Ctrl+Q、zdesktop の ERROR 無し、録音の peak が 1000 を超える（音が card に届いた）。PNG `playing.png`・`seek.png`。

## 確認

- build: `make ZEDBSD_CONFIG=plan/ws129/tests/config-amd64-release-noclang.mk BUILD=build/p2-ci build/p2-ci/bin/videoplayer` exit 0、warning 0。NEEDED は libav の 5 つ・libkeiland 系・libc。`plan/tools/style-check.py userland/desktop/videoplayer/*` 違反 0。
- 未実施: QEMU（T1 に依頼）、実機。release・CI の config に `libavcodec videoplayer` を足すのは Q1（p002 の後、Q1 の予定）。CI の apt の `nasm`（p001 から Q1 に依頼済み）。

## 残り

- App Home の一覧（`userland/desktop/wayland/apps.conf`、無い program の行は出ない）と Files の「開く」（`userland/desktop/files/apps.c` の built-in に `video/*`）への登録は他の WS の source なので Q1 に提案する。
- audiod の client の共有の library への移動（上）。
