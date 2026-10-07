<!-- awesome-plan project=zedbsd record=ws120-p009 -->

# ws120-p009: Music の app

Status: cleared（2026-10-07 Q1 の判定: T1-315 の AAT music.play（HDA 付き、PLAY・POSITION 2026・ENDED・FILE）と playing の PNG（Tone A の cover・再生の bar）を Q1 が目視。音は耳で聞いていない）（旧: uncleared（2026-10-07 Q1 の判定: T1-301 で play が `MUSIC AUDIO error=13`（QEMU の guest に sound device が無い）で止まる。open-from-home は pass。P2 が直す）（旧: test-wait（T1-301）））
Disposition: normal
Parent: [WS120](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p008](../phase008/phase.md)

## 範囲（正常系）

- `userland/desktop/music/play.c`: 再生の thread。mediafile で音の track を開き、videoplayer の `codec.c`（libavcodec の dlopen の add-in）で decode、`audio.c`（audiod の client）へ書く。再生・一時停止・seek・曲の終わりで次の曲（album の続き）。位置は audiod の読みの位置。
- `view.c`: 左に album の一覧（cover・題・artist、先頭に「All Songs」）、右に選んだ album の曲（番号・題・artist・長さ、再生中の印）、上に検索、下に再生の bar（cover・題・artist・前・再生/一時停止・次・位置の slider・時間）。libavcodec が無ければその旨。外部の service の欄・icon・名前は作らない（D-SVC）。
- `main.c`: kl_app の loop、menu（Play/Pause・Next・Previous・Quit）、key（Space・←→・Ctrl+Q）、引数の file を開いて再生。log `MUSIC …`（tests が読む）。
- build: `Makefile`（package `music`）、`platform/amd64/vmunix.mk` の link の規則と filter-out の一覧。Files の `audio/mp4` → Music（`userland/desktop/files/apps.c`）、App Home の「Music」（`userland/desktop/wayland/apps.conf`）。
- 試験: host の view の PNG（`plan/ws120/tests/run-host-music.sh`）、QEMU は T1（AAT の `apps.music.*`）。

## 実装（2026-10-07、P2）

- `music/play.c`・`play.h`（再生の thread、mediafile と videoplayer の codec.c・audio.c）、`cover.c`（JPEG・PNG の cover を 160 px の正方形に）、`view.c`、`main.c`、`Makefile`。
- `platform/amd64/vmunix.mk`: `music` の link の規則（png・z・jpeg・gif の compat）と filter-out の一覧。
- Files: `userland/desktop/files/apps.c` の built-in に `audio/mp4` → Music。App Home: `userland/desktop/wayland/apps.conf` に Music、`icons.c`・`icons.h` に音符の picture（`GLASS_ICON_APP_MUSIC`、名前 `music`、app ID `music`）。
- AAT: `plan/tools/aat/scenarios/aatlib.py`（APPS・PROGRAMS に Music）、`helpers_apps.py`（open-from-home）、`helpers_music.py`（`apps.music.play`）、`tests/scenarios/apps/music/{open-from-home,play}.md`。
- 試験の image: `plan/ws120/tests/config-amd64-music.mk`（AAT の image＋music。AAT の image は beta2 の config で libavcodec・audiod 入り）。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws120/tests/run-host-music.sh` → PASS 10（cover の JPEG・PNG の decode、album の選択、Play、Next、再生・一時停止の button、1 回の click は選ぶだけ・double click で再生、Space、All Songs）。PNG: all・album・playing・glass・search・no-codec・empty（`build/review/ws120/`）。
- host: ffmpeg で作った m4a（AAC、cover 付き）を tags.c が読む（題・artist・album・番号・8000 ms・cover 857 byte・音だけ）。
- zedBSD: `make -j16 ZEDBSD_CONFIG=plan/ws120/tests/config-amd64-music.mk BUILD=build/ws120-zed build/ws120-zed/bin/music build/ws120-zed/bin/files build/ws120-zed/bin/wayland` warning 0。
- style-check 0（music の全 file）。check-scenarios PASS。
- QEMU: T1（AAT の `apps.music.open-from-home`・`apps.music.play`）、未実施。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS120 の行。

## T1-301 の直し（2026-10-07 q834 P2）

- 原因は試験の環境: AAT の QEMU の guest に音の device が無く、audiod が使えない（`MUSIC AUDIO error=13` は zedBSD の ENODEV）。app の扱い（音が無い時は「No sound」を出して落ちない）は今のまま。
- `plan/tools/aat/scenarios/helpers_music.py` の注記と check の文に、QEMU を `--qemu-extra '-audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0'` で起動することを書いた。`tests/scenarios/apps/music/play.md` の準備にも同じ。
- 再試験は T1 に（`apps.music.play`、HDA 付きの QEMU）。
