<!-- awesome-plan project=zedbsd record=ws071p017 -->

# ws071-p017: card を浮いたタイトルバーの幅に揃える・docking の確認

Phase ID: `ws071-p017`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

2026-09-27 ユーザー（p015・p057 の画面を見て）:「カードの間の隙間は意図的です。OKです。また、フローティングタイトルバーと幅を合わせましょう。
フローティングタイトルバーにナビゲーションを実装してから、これがスクリーン上部のメニューバーにドッキングできるか、試していない気がします。
これもまだならう実装してください。」

1. card の間の隙間は保つ。glass のとき、いちばん左の card の左端とタイトルバーの左端、いちばん右の card の右端とタイトルバーの右端を揃える
   （窓の本体の余白を無くす: 浮いたタイトルバーは zdesktop の `floating_title` で本体と同じ幅）。タイトルバーと card の縦の間を card の間と
   同じに: card の間を zdesktop の「タイトルバーと本体の間」と同じ 8 px にし、上の余白 0。docked（最大化）では画面の端から 8 px。
2. 最大化で zdesktop-files の CONTROLS（戻る・進む・Home・パンくず・検索・表示・preview・「…」）がシステムバーの Application Zone に
   docking して動き、戻すと浮いたタイトルバーに戻ることを、zdesktop-files 自身で（glass の card とタブ付きで）確かめる。

## 実装（2026-09-27）

- `ui.c`: `ui_layout` の余白と間を glass のとき 0（docked は 8）と 8（`UI_GLASS_GAP`、zdesktop の `ZWL_GLASS_GAP` と同じ値）に。
- `files.h`: `app->docked`。`main.c`: frame ごとに窓の maximized を `app->docked` に。
- docking は既に動いていた（ws070-p010 の CONTROLS の docked と ws071-p014 の Home の試験）。直す所は無かった。

## 検証（amd64 だけ、2026-09-27）

- host: `host-build.sh` 通る。画面 `build/ws071-p016-host/p017-glass.png`。
- guest（QEMU、Venus）: 新 `plan/ws071/tests/files-p017.sh` PASS: 浮いているとき card が窓の端まで（`card:0,0,212,640,16 card:220,0,780,640,16`）、
  タイトルバーの control が窓の幅の中、docked（GLASS dock）で control が where=docked、card が画面の端から 8 px（`card:8,8,…`）、docked の
  Back（Downloads → Documents）、docked の検索欄（focus、"report" で SEARCH done）、docked の「…」の menu から View > List（action 16）、
  戻す（GLASS undock）と where=floating と card が元の位置。画面 `build/ws071-p017/{floating,docked,docked-search,docked-overflow,restored}.png`。
- 回帰（同じ image）: files-regress（p002〜p008・p012〜p015・p009・p017）PASS（p005・p006・p013・p015 の座標と card の位置を新しい配置に
  直した）、zdesktop-p057 PASS、boot test PASS（`build/ws071-p017-boot/login.png`）。
- 規約: `ui.c` の style-check 0。
- 実機（i915）: 未実施。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws071-shots/p017-20260927-venus-{floating,docked,docked-search,docked-overflow,restored}.png`。
