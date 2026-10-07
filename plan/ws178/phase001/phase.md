<!-- awesome-plan project=zedbsd record=ws178-p001 -->

# ws178-p001: libGL を OpenGL（Desktop）と GLX（xserver）に分ける

Status: planned（2026-10-07 Q1 が作成）
Disposition: normal
Parent: [WS178](../ws.md)

## 範囲

ws.md の「目標」のとおり。libGL.so の soname・export は変えない。

## 確認

- build（warning 0）、`make -s list-user-programs` で OpenGL が desktop、GLX が xserver の一部、`plan/tools/menuconfig-target-host-test.py`。
- T1: zgears・glxtest（X11 の image）の回帰。
