<!-- awesome-plan project=zedbsd record=ws121-p005 -->

# ws121-p005: JS の HTMLMediaElement と media の event

Status: cleared（2026-10-07 Q1 の判定: T1-315 の AAT browser.video（`MEDIA play position_ms=0`・`pause position_ms=5083`）と playing の PNG で絵と操作の帯（一時停止の印・進みの bar）を Q1 が目視。音は QEMU では聞いていない）（旧: in-progress（2026-10-07 q831 P2））
Disposition: normal
Parent: [WS121](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p004](../phase004/phase.md)

## 範囲（正常系）

- `libbrowser/bind/media.c`（新規）: `HTMLMediaElement`（`paused`・`ended`・`currentTime`（seek）・`duration`（知る前は NaN）・`readyState`・`muted`（属性の反映）・`play()`（すぐ resolve する Promise）・`pause()`・`load()`（何もしない）・`canPlayType()`（MP4・WebM・Matroska の video/audio は "maybe"）・`HAVE_*`）、`HTMLVideoElement`（`videoWidth`・`videoHeight`）、`HTMLAudioElement`。`<video>`・`<audio>` の object の prototype（`bind/node.c`）、interface の表（`bind/internal.h`・`window.c`）。
- `bind_host.media`（`bind/bind.h`）: 状態の問い・play・pause・seek。page の `page_media_host`（`page/media.c`）が答え、page がまだ見つけていない element の play() はその時に始める。
- event（`page/media.c` の `media_events`）: 開いた時 `durationchange`・`loadedmetadata`・`loadeddata`・`canplay`、`play`・`playing`、`pause`、終わりで `timeupdate`・`pause`・`ended`（paused に戻る）、再生中 0.25 秒ごとの `timeupdate`、失敗で `error`。handler の属性（`onended` など）を `bind/handler.c` の表に。音だけの再生中も view の timeout を 250 ms に。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws121/tests/run-host-video.sh` → PASS 9（p004 の 4 に加え、script の page: `canPlayType` が "maybe" と ""、開く前 paused・readyState 0、`loadedmetadata` で 320x240・20 s、16 s へ seek して `play()` の Promise が resolve、`playing` で paused false、`ended` で ended true・paused true・currentTime 20）。
- zedBSD: libbrowser.so・browser の build warning 0。style-check: bind/media.c・page/media.c・node.c・handler.c・bind.h・internal.h・page.h 0（window.c・script.c は既存の件数のまま）。
