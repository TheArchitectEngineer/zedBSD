<!-- awesome-plan project=zedbsd record=ws120-p009 -->

# ws120-p009: Music の app

Status: in-progress
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

## 確かめ

- host の PNG、zedBSD の build の warning 0、style-check 0。
- QEMU: T1（未依頼）。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS120 の行。
