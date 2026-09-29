<!-- awesome-plan project=zedbsd record=ws099p007 -->

# ws099-p007: client の cursor はその client の窓の上だけ（BUG-118）

Phase ID: `ws099-p007`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機と X terminal そのものは未実施）
Phase disposition: normal
Bug: [BUG-118](../../bugs/BUG-118.md)
Queue: なし（2026-09-30 main の割り当て「BUG-118 を p007 として。cursor は pointer の下の surface ごとに決め、X terminal の外では矢印。基準の C2 か C9 の試験に入れる」）

## 原因

cursor の状態（client の surface・隠す・shape）は server に 1 つだけで、keyboard の focus が変わる（`zwl_seat_focus` → `zwl_cursor_default`）まで
戻らなかった。zdesktop は pointer の enter を focus の窓にだけ送るので、X terminal（focus）が cursor を空にすると、pointer がデスクトップ・system bar・
title bar・他の窓の上に出ても隠れたままだった。

## 変更（`userland/desktop/wayland/`、IME の file と hook には触れていない）

- `zwl.h`: server に `cursor_client`（cursor の状態を決めた client、zdesktop 自身なら NULL）と `cursor_client_logged`（試験の log 用）。
- `seat.c` の `set_cursor`（wl_pointer.set_cursor）・`tablet.c` の `tool_set_cursor`・`cursor.c` の `device_set_shape`（cursor shape）が
  `cursor_client` を記録。`zwl_cursor_default` は NULL に戻す。`objects.c` の `zwl_client_destroy` は、その client が去るとき矢印に戻す（dangling を防ぐ）。
- `cursor.c` の `zwl_cursor_client_shown()`（新）: glass の look で、popup の grab 中か、pointer の下の窓の本体（`zwl_glass_body_at`）がその
  client のものなら 1、他（デスクトップ・system bar・title bar・他の client の窓）なら 0。変わったときに `ZWL CURSOR client=N shown=0|1` を log。
- `compose.c` の `compose_cursor`: 0 のときは client の cursor（隠す・surface・shape）を使わず、窓の縁の resize の矢印か zdesktop の矢印を描く。
- 規約: `style-check.py` compose.c・cursor.c・seat.c・tablet.c・objects.c 0、`git diff --check` 0。build: desktop の warning 0。

## 試験（`plan/ws099/tests/cursor-owner.sh`、C9 の一覧に追加）

zdesktop --glass 1280x800、`wlshm --hide-cursor`（pointer が入ると cursor を空にする）。pointer を窓の本体・デスクトップ・title bar・本体に置き、
log の `shown=` と、pointer を遠くに置いた画面との差（pointer の位置の 24x32 の箱で 24 を超えて違う画素の数）で矢印の有無を見る。
`criteria.sh` の `C9_TESTS` は、`plan/ws035/tests/zdesktop-NAME.sh` の名前に加えて path も受けるようにした。

| image | 本体（隠れる） | デスクトップ（矢印） | title bar（矢印） | 結果 |
| --- | --- | --- | --- | --- |
| 変更前 `build/ws099-p005-colors.img` | 隠れる | **矢印なし**（差 0） | **矢印なし**（差 0） | FAIL（BUG-118 を再現） |
| 変更後 `build/ws099-p007-after.img` | 隠れる（差 ≤ 20） | 矢印あり | 矢印あり | PASS（`shown=1 → 0 → 1`、focus の変更なし） |

回帰（`criteria.sh build/ws099-p007-after.img … C8 C9`、`build/ws099-p007-regress/results.txt`）: C8（p134）、C9 の 10 本（p052・p053・p072・p076・
p126・p128・p134・p137・p138・cursor-owner）が全て PASS。

画面: `build/ws099-shots/cursor-owner-p007-after/`（desktop・title・body・body-again・far、拡大 `zoom-desktop.png`）、変更前 `cursor-owner-p005-colors/`。

## 制限

- X terminal（xserver の client）そのものでは試していない（同じ wl_pointer.set_cursor の空で試した）。実機（5330）は未実施。
- client の popup（menu）の上では、grab が無ければ矢印になる（menu の上の矢印は差し支えない）。

## Resume point

2026-09-30: cleared。
