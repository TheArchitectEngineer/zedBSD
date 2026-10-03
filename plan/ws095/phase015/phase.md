<!-- awesome-plan project=zedbsd record=ws095-p015 -->
# ws095-p015: 利用者の辞書の保存を入力の無い 3 分の後と終了の時に（BUG-143）

Status: uncleared（q652-i01、P2 generation7。T1-046 で SIGTERM の後の保存が FAIL）
Disposition: normal
Parent: [WS095](../ws.md)
Bug: [BUG-143](../../bugs/BUG-143.md)
Queue: q652 / q652-i01（承認: 2026-10-04 user「P1、P2はBUG-143, 053, 052, 162, 103, 129, 033, 157, 139を修正します。直す順序は任せます。」、方針は同日 user「BUG-143は、辞書の保存は遅延させ、入力が3分以上ないときにファイル保存、みたいな運用にして回避しましょう。」）

## 範囲

1. 確定のたびの利用者の辞書の保存（ws095-p014 で writer thread に移した `ja_user_save_later`、全体の書き出し・fsync・rename）をやめる。
2. 入力（IME に届く key）が 3 分無かった時と、IME の終了の時に保存する。保存は IME の event loop を止めない（writer thread）。
3. Text Editor の入力の 500 ms の遅れが IME と無関係なら、原因を読んで報告し、直せるなら直す。

## 設計と実装

- `engine.h`: engine の ops に `save`（学んだ物を file へ、key の経路の外で）を足した。direct の engine は何もしない。
- `ja-engine.c`・`ja.h`: 確定で学んだ時は `ja_core.user_unsaved` を立てるだけ。`ja_core_save` は未保存の時だけ `ja_user_save_later`（writer thread）に渡す。
  `ja_core_close` は未保存なら `ja_user_save_later` に渡し、`ja_user_free` が thread の最後の書き込みを待つ（同期の `ja_user_save` を使わないのは、
  thread に待っている古い text が新しい file の上に書かれないため）。
- `program.h`・`method.c`: `PROGRAM_SAVE_IDLE_MS`（180000）。engine に key を渡すたびに `save_ms` を 3 分後へ動かす。`program_save_timeout`・`program_save_due`
  （期限が来たら全 engine の `save`、`save_ms` を 0 に）。
- `main.c`: poll の timeout は key の repeat と保存の早い方（`main_earlier`）。SIGTERM・SIGHUP・SIGINT は loop を終わらせる印（`main_stopping`）にし、
  終了の経路（engine の destroy）で未保存の分を書く。接続の終わり（zdesktop の終了）も同じ経路。
- 直接入力（direct）の間は key が IME に来ないので、IME から見て入力の無い時間に数える（日本語で学んだ後に直接入力で打ち続けても 3 分で保存）。

## 検証

- host: `sh plan/ws095/tests/host-engine.sh build/p2-q652/host-engine` → **220 passed, 0 failed**（ASan・UBSan。新しい `test_engine_save` 12 項目:
  確定で file を書かない、未保存の印、save で writer へ、2 度目の save は何もしない、save で書かれた内容、save しないで閉じた時に close が書く）。
- build（zedBSD amd64）: `make -j16 ZEDBSD_CONFIG=plan/ws095/tests/config-amd64-ime.mk BUILD=build/p2-q652 build/p2-q652/bin/keiland-ime` → exit 0、warning 0。
- build（Linux）: `make -j16 keiland-linux KEILAND_LINUX_BUILD=build/p2-q652/linux` → exit 0、warning 0。
- QEMU: T2 に依頼。`plan/ws095/tests/latency-bug143.sh` に step 4 を足した（確定の直後は辞書が無い、190 s の無入力の後に 1 行以上、もう 1 つ学んで
  keiland-ime に SIGTERM の後に行が増える）。step 1〜3 は前と同じ（Text Editor の数字は T1-024 の 337/416 ms と比べる）。未実施。
- 実機（5330）: 未実施。

## Text Editor の遅れ（範囲 3）

直接入力の key は IME を通らない（zdesktop の `zwl_ime_key_grab` は direct で 0、ws095-p014 の読みと同じ）。IME の保存は直接入力の遅れの原因ではない。
Text Editor に固有の重い所を探した: key のたびに `te_draw` が窓の全体を CPU で描き直し、その大半が窓とほぼ同じ大きさの card の角丸
（`te_canvas_round`）だった。画素ごとに clip の判定・角の判定（double）・`over` の合成をしていた（gprof: `over` 56%、`te_canvas_round` 21%、
`clip_inside` 15%）。

直し（`userland/desktop/textedit/canvas.c` だけ）: clip の中の行と列だけを回し、角の無い行は角の判定なしに `round_full` で合成し、直前と同じ画素は
同じ結果を使う（`over` は純粋な関数なので画素は同じ）。

host の計測（scratchpad の bench、-O2、`te_draw` を 1 key ごと、50 回の平均/最大）。出力の画素の hash（通常・glass・clip を狭めた 3 つの角丸）は前後で一致:

| 窓 | 前 | 後 |
| --- | --- | --- |
| 900x680 | 7.94 / 10.49 ms | 1.90 / 4.49 ms |
| 1920x1080 | 24.93 / 26.82 ms | 3.75 / 6.53 ms |
| 2560x1600 | 48.81 / 55.30 ms | 6.39 / 7.68 ms |

- `sh plan/tools/textedit/host-core.sh build/p2-q652/host-core` → 53/53。textedit の zedBSD の build（`config-amd64-ime-textedit.mk`）と Linux の build は warning 0。
- 結論: Text Editor の key ごとの CPU の描画は host で約 1/6 になった。ただし 500 ms を説明する量ではない（host で 25 ms）。QEMU の T1-024 では Text Editor と
  Terminal の差は小さかった。残りの候補は実機（5330、i915）の present の経路（`keiui_present_frame` の 8 MB の memcpy を host から見える memory へ、
  `vkWaitForFences` の同期の待ち）で、実機の計測が要る。ここは未特定のまま BUG-143 に残す。

## 結果（Q1、2026-10-04、T1-046、QEMU Venus KVM、main 09179c0 の image）

uncleared。ok: `ZWL IME bypass lines: 0 -> 0`、確定の直後は辞書の file が無い、190 s の後に 1 件。FAIL（2 回とも同じ）: `dictionary after SIGTERM: 1`・`written at the end: FAIL`。SIGTERM の後の辞書は `かんじ /感じ/漢字/` だけで わたし の行が無い。SIGTERM の後も ps に keiland-ime が残る（pid 44・175、/bin/wayland の子）。LATENCY（ms、参考）: textedit-direct 312/440・306/516、terminal-direct 300/309・305/313、japanese 290〜417。証拠 worktrees/t1/build/t1-046/・t1-046-retry/。再開: P2 が SIGTERM で終わらない理由（signal の扱い・試験の送り先の pid）を調べて直し、T1 に再依頼。
