<!-- awesome-plan project=zedbsd record=ws120-p008 -->

# ws120-p008: m4a の metadata と `~/Music` の collection

Status: cleared（2026-10-07 Q1 の判定: T1-315 の AAT music.play（HDA 付き、PLAY・POSITION 2026・ENDED・FILE）と playing の PNG（Tone A の cover・再生の bar）を Q1 が目視。音は耳で聞いていない）（旧: test-wait（T1-301））
Disposition: normal
Parent: [WS120](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p001](../phase001/phase.md)（2026-10-07 の決定と設計）

## 範囲（正常系）

`userland/desktop/music/`（新規）の `music.h`・`tags.c`・`library.c`。

- `tags.c`: m4a（MP4）の top level の box を pread で辿り、`moov` を読む。`mvhd` の長さ、`trak/mdia/hdlr` の種類（音・映像）、`udta/meta/ilst` の `©nam`・`©ART`・`aART`・`©alb`・`trkn`・`covr`（JPEG・PNG の bytes）。meta は full box と QuickTime の形の両方。
- `library.c`: folder を深さ 4 まで見て `.m4a` と、映像の無い `.mp4` を集める。album（`aART` か `©ART` と `©alb`）にまとめ、album は題の順、曲は album・track の番号・題の順。題が無ければ file の名前、artist・album が無ければ「Unknown Artist」「Unknown Album」。cover は album の最初の物だけ持つ。検索（題・artist・album の部分一致、大小を区別しない）、album の中の次・前、引数の file を足す。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws120/tests/run-host-music-library.sh` → PASS 24（`make-m4a.py` が MP4 の box を組んだ file: tags の題・artist・album・番号・長さ（mvhd v0・v1）・cover・音と映像の track、QuickTime の形の meta、64 bit の size の mdat が moov の前、MP4 でない file は EINVAL。collection: 6 曲 4 album、album・番号・題の順、album の artist、cover は album に 1 つ、題の無い file は file の名前、深さ 4 まで・隠し folder を見ない、映像の有る .mp4 を入れない、album と検索の一覧、次・前、外の file を足す・同じ file・映像だけの file は ENOTSUP）。ASan・UBSan。
- style-check 0（music.h・tags.c・library.c）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS120 の行。
