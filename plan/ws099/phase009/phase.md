<!-- awesome-plan project=zedbsd record=ws099p009 -->

# ws099-p009: greeter の「Shutting down...」「Restarting...」の表示

Phase ID: `ws099-p009`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Bug: [BUG-119](../../bugs/BUG-119.md)（表示の側。電源断そのものは kernel の担当）
Queue: なし（2026-09-30 main の割り当て「p009: p008 の案のとおり、表示を出した frame の後に POWER を送る。c1-boot-shutdown.sh の Shut Down の最後の絵が
『Shutting down…』になることを撮る。kernel の電源断の後に『QEMU が終わる』判定も働くよう試験を用意し、今は警告だけ」）

## 変更（`userland/desktop/wayland/greeter.c`）

- Shut Down・Restart の押下（`greeter_power`）は、すぐには送らず状態を「電源を切る途中」（`greeter_powering`）にし、押した時の frame の数と時刻を記録する
  （log `ZWL GREETER powering=poweroff frame=N`）。
- 描画（`zwl_greeter_draw`）: その間は card の中身（avatar・名前・password の欄）と Restart・Shut Down の button を描かず、card に
  「Shutting down...」（Restart は「Restarting...」）と、8 つの点の輪の spinner（1.2 秒で 1 周、明るい点が回る）を描く（`greeter_draw_power`）。
  時計・日付・Kei の印はそのまま。
- tick（`zwl_greeter_tick`）: 毎回描き直し（spinner）、押してから frame が 2 つ進んだか `GREETER_POWER_MS`（1 秒）が過ぎたら `POWER` を sessiond に送る
  （`greeter_power_send`、log `ZWL GREETER power=poweroff frames=N ms=M`）。Venus が最後の絵を保つので、電源が切れるまで「Shutting down...」が残る。
- 押した後の pointer・key は受けない。
- 規約: `style-check.py greeter.c` 0、`git diff --check` 0。build: desktop の warning 0。

## 試験（`plan/ws099/tests/c1-boot-shutdown.sh` を広げた）

- Shut Down の前に greeter を撮り（`greeter.png`）、40 秒の後の最後の絵と比べる: card の中（470,350〜810,540）と power の button の角
  （1010,730〜1270,790）の画素のうち `C1_CHANGED_PERCENT`（10%）以上が変わっていれば「Shutting down... の絵」（C1-shutdown-picture）。
- log の順番（powering の後に power）: 押して 2 秒後に greeter の log を読もうとするが、その頃には sshd が止まっていて読めない（WARN）。最後に保たれた
  絵が「Shutting down...」であることが、表示の後に要求が行った証拠になる。
- QEMU が終わるか（ACPI の電源断、BUG-119）: `C1_REQUIRE_QEMU_EXIT=0`（既定）では WARN、`=1` で FAIL。kernel の電源断が入ったら 1 にする。

| image | 最後の絵と greeter の差（card・button） | C1-shutdown-picture | QEMU | C1 |
| --- | --- | --- | --- | --- |
| 変更前 `build/ws099-p007-after.img` | 0%・0%（greeter のまま） | FAIL | 終わらない（WARN） | FAIL |
| 変更後 `build/ws099-p009-after.img` | 14%・11% | PASS | 終わらない（WARN） | PASS |

起動と Shut Down の黒・文字の console は、どちらも 0 のまま（C1-boot・C1-shutdown PASS）。
画面: `build/ws099-shots/c1-p009-p009-after/shutdown/seen-080.png`（「Shutting down...」と spinner）、前は `c1-p009-p007-after/`。

回帰（`criteria.sh build/ws099-p009-after.img … C9`）: 10 本のうち 9 本 PASS、p076 が 1 回 FAIL（左の辺の resize の drag が 416 で止まり、
最小幅の 200 まで届かなかった。greeter.c 以外の compositor は p007 と同じで、p076 は greeter を使わない）。同じ image で p076 を 2 回走らせ直し、
2 回とも PASS（`build/ws099-p009-p076/results.txt`）。pointer の注入の一度きりの不安定さと見る（再現すれば Bug Board へ）。

## Resume point

2026-09-30: cleared。kernel の電源断（BUG-119、High の担当）が入ったら `C1_REQUIRE_QEMU_EXIT=1` で c1-boot-shutdown.sh を走らせる。
