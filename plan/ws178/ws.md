<!-- awesome-plan project=zedbsd record=ws178 -->

# WS178: OpenGL を Desktop へ、GLX を X11 server（xserver）へ

Status: planned（2026-10-07 追加、ベータ2、優先度は低い）
Master: [master](../master.md)
Primary Milestone: MG006

## 由来

ユーザー（2026-10-07）:「make menuconfigの X11 -> OpenGL and GLX ですが、Desktop -> OpenGL に移動して、GLXは Desktop -> X11 server for the compositorのライブラリの1つに統合しましょう。ベータ2の範囲にして、優先度は低くていいです。」

## 目標

- 今の package `libgl`（label「OpenGL and GLX」、menu の X11、`userland/x11/libGL/`）を分ける。
  - OpenGL（libGL の GL の部分: gl3.c・fixed.c・immediate.c・shaders）は menu の Desktop の「OpenGL」へ（tree も `userland/desktop/` の下へ移す）。
  - GLX（glx.c）は Desktop の「X11 server for the compositor」（`xserver`、`userland/desktop/xserver/`）の library の 1 つにする（xserver を選ぶと GLX も入る）。
- glxtest・zgears など libGL を要る X11 の program の依存（REQUIRE）と link を新しい置き場所に合わせる。libGL.so の soname・export（exports.map）は変えない（既存の program の ABI を保つ）。
- vmunix.mk（libGL の link の規則）・config の program の一覧・menuconfig の host 試験を追従する。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 分割と移動（package・tree・link・menu・config）、build と menuconfig の試験、T1 で zgears・glxtest の回帰 | planned | — |
