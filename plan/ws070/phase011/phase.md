<!-- awesome-plan project=zedbsd record=ws070p011 -->

# ws070-p011: TABS の presentation

Phase ID: `ws070-p011`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27、サブエージェント。まず一通り動かす方針で、残りの端の場合は下の「残り」）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが計画・実行）
依存: p010（CONTROLS）

## 範囲

[titlebar-design.md](../titlebar-design.md) §6・§7・§9: TABS mode の窓の Presentation（浮いたタイトルバーの題名の後ろ、docked では
システムバーの Application Zone）に tab の strip を並べ、pointer で操作する。mode の切替が 1 つの commit で変わることを試す。

## 実装（2026-09-27）

- `userland/base/zdesktop/titlebar-shell.c`: `shell_draw_tabs`・`shell_tabs_layout`・`shell_draw_tab`・`shell_act_tabs`・`shell_log_strip`。
  - tab は pill: active は白い面、他は淡い面、pointer の下は明るく。attention は title の前の accent の点。× は closable で active か
    pointer の下のとき（hit も描いたときだけ）。「+」は `new_tab_button` のとき tab の後ろ。
  - 縮退: 全部が望む幅（title + padding + × + 点、96〜200）で入ればそのまま → 入らなければ同じ幅に縮める（96 まで、title は「…」で
    切れる）→ それでも入らなければ両端の矢印で scroll（入るだけの tab、commit ごとに一度 active を見える所へ、矢印は 1 tab ずつ）、
    見えない tab は「…」の popup の行（選ぶと `tab_activated`）。
  - press は tab・×・＋・矢印が取り、release で `tab_activated`・`tab_close_requested`・`new_tab_requested`。strip の空きは今まで通り
    窓の drag と double click。
  - CONTROLS から外れた窓の検索欄の編集は終える。
  - log: `ZWL TITLEBAR strip client= surface= where= id= x= y= width= height= shown= flags= close=`、`... button=left|right|new|overflow`、
    `ZWL TITLEBAR strip scroll client= surface= first=`。
- `titlebar.h`: model に presentation の `tab_first`・`tab_seen`。
- `userland/base/tests/titlebar-probe/main.c`: TABS の窓が editor のように振る舞う（選んだ tab を active に、閉じた tab を消して隣を
  active に、「+」で "Untitled N"）。`--tabs=N`、`--switch=S`（controls と tabs の両方の model を持ち、S 秒ごとに 1 つの transaction で
  mode だけを変える）。待ちの loop を `wl_display_prepare_read`・`read_events` の形に（前の `wl_display_dispatch` の形は `--seconds` が
  過ぎても終わらなかった: 既存の probe の不具合、ここで直した）。

## 検証（amd64、QEMU の Venus だけ、2026-09-27）

- 新 `plan/ws070/tests/titlebar-p011.sh` PASS: strip（main.c active・×、日本語.txt の点、+）、README.md の click（active に）、
  日本語.txt の click と ×（close、main.c が active に）、+（Untitled 4 が active）、docked の strip と click、戻す、6 tab の窓で縮む
  （全部見え、矢印なし）、14 tab の窓で scroll（矢印、shown=0、…）、右の矢印（first=1）、… の popup から Document 14（tab_activated）、
  `--switch=2` の mode の切替（commit は毎回 controls=8 tabs=3 の全体、途中の状態の commit なし）、ERROR なし。
- 回帰（最も影響がある所だけ）: titlebar-p010（CONTROLS）PASS、files-p017（zdesktop-files の CONTROLS の docking）PASS。
- 規約: 変えた file（titlebar-shell.c・titlebar.h・titlebar-probe）の style-check 0。
- 実機（i915）: 未実施。boot test はユーザーの指示（2026-09-27）で Phase ごとには行わない。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws070-shots/p011-20260927-venus-{tabs,after,docked,narrow,scroll,strip-overflow,
switch-controls,switch-tabs}.png`。

## 残り（後の補強の Phase へ）

- docked の bar（白い地）で active でない tab の淡い面がほとんど見えない。地に合わせた色が要る。
- 縮めた tab の title の切れ方（6 tab の窓で "Docu…"）: 縮める前に題名（窓の名前）を短くする等、配分の見直し。
- dock・restore の animation の途中の strip（§10）の確認、日本語の title の幅の確認（描けているのは画面で見た）。
- keyboard（F10 で「…」は既存の経路、tab の keyboard 操作は無い）、tab の drag での並べ替え（Future Work）、touch の大きさ。
- scroll の矢印の長押し、wheel での scroll。
