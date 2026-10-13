<!-- awesome-plan project=zedbsd record=ws178 -->

# WS178: OpenGL を Desktop へ、GLX を X11 server（xserver）へ

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」。旧: incomplete）
Primary Milestone: MG006
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-07）「make menuconfigの X11 -> OpenGL and GLX ですが、Desktop -> OpenGL に移動して、GLXは Desktop -> X11 server for the compositorのライブラリの1つに統合しましょう。ベータ2の範囲にして、優先度は低くていいです。」同日のユーザーの決定「完全に分ける」（libGL.so は GL だけ、glX* は xserver の package の libGLX.so だけ）。

## 結果（2026-10-13 完了）

- p001: tree `userland/x11/libGL/` → `userland/desktop/libGL/`（package `libgl`、label「OpenGL」、menu Desktop）。GLX は `userland/desktop/xserver/libGLX/`（xserver の package が `/lib/libGLX.so` を持ち、REQUIRE に desktop/libGL）。GL と GLX の間は私的な口 `userland/desktop/libGL/zgl-glx.h`。exports は libGL.so が gl* と `zgl_glx_attach`、libGLX.so が glX* と `zglx_swap_step`。
- link: `platform/amd64/vmunix.mk` の libGL.so・libGLX.so の規則、zgears・glxtest は `-l:libGL.so -l:libGLX.so`、REQUIRE は `desktop/libGL desktop/xserver`。文書は `include/libc/GL/glx.h` の注、`docs/reference/compatibility-profile.md`、`userland/desktop/xserver/libGLX/README.md`（外から移植する X の GL の program は `-lGLX` を足す）。
- 確認: build warning 0、`llvm-nm`・`llvm-readelf` で symbol と NEEDED、QEMU の T1-340 で x11 の回帰の p005 PASS。

## 制限・移管

- x11-p004 の段 3（docked の glxtest を bar の close で閉じても終わらない）は WS178 の前の main でも同じ（T1-341）で、BUG-273（後に BUG-245 の duplicate）へ移した。
- `plan/tools/menuconfig-target-host-test.py` の platform の縛りの検査の FAIL は WS178 と無関係の既存。全文規約の見直しはベータ3。実機は未実施。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws178-p001 | 分割と移動（package・tree・link・menu・config）、build と menuconfig の試験、T1 で zgears・glxtest の回帰 | cleared（2026-10-08、T1-340。x11-p004 の段 3 は BUG-273） |

Phase の directory は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
