<!-- awesome-plan project=zedbsd record=ws121-p006 -->

# ws121-p006: 音と controls の帯

Status: in-progress（2026-10-07 q831 P2）
Disposition: normal
Parent: [WS121](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p005](../phase005/phase.md)

## 範囲（正常系）

- 音: muted でない `<video>`・`<audio>` の engine は `MEDIA_SOUND` で audiod に stream を作り、音を書く（libmedia の engine、p002）。muted の物は stream を作らない（音だけの file は無音で時計が進む）。自動再生は muted だけ（U2）。
- controls（`controls` 属性の `<video>`）: video の絵の下に帯（暗い地、左に再生か一時停止の印、時間の track と再生した部分）を絵の上に描く（`page/media.c` の `media_controls`、状態か位置が変わると `media_engine_redraw` で絵を描き直して重ねる）。click（`page/input.c` の `page_click_control` から `page_media_click`）: 帯の印か帯の上の所で再生・一時停止、track の上でその位置へ seek。
- log: libmedia の行を標準 error に `BROWSER MEDIA …`（試験が読む）。
- AAT: [apps.browser.video](../../../tests/scenarios/apps/browser/video.md)（helper `plan/tools/aat/scenarios/helpers_browser_media.py`）。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws121/tests/run-host-video.sh` → PASS 12（p004・p005 に加え、controls の page: 開いて一時停止の帯、click で `play`、1.5 秒後の click で `pause`、帯の暗い地）。PNG `build/review/ws121/host-video-controls-{paused,played}.png`。
- zedBSD: libmedia.so・libbrowser.so・browser の build warning 0。style-check 0（page/media.c・input.c・engine.c）。check-scenarios PASS。
- QEMU: T1（未依頼）。音そのものは実機（耳）。
