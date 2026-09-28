<!-- awesome-plan project=zedbsd record=ws075p017 -->

# ws075-p017: BUG-058（App Home の zgears が最初の frame の前に終わる）の再試験

Phase ID: `ws075-p017`
Parent: [WS075](../ws.md)
Status: uncleared（2026-09-28。6 回の起動で再現せず。原因は未特定のまま、[BUG-058](../../bugs/BUG-058.md) は tracking）
Phase disposition: normal
承認: 2026-09-28 main の依頼（WS075 の i915 の subagent、項目 3「時間が残れば」）。

## 範囲

実機（5330）の i915 で App Home から Gears を起動し、BUG-058（`ZGEARS START` の後、最初の frame の前に黙って終わる）の再現を試み、
再現すれば gdbstub で原因を探す。

## 結果（実機 = 5330 の VFIO passthrough の QEMU guest、2026-09-28）

[ws075-p016](../phase016/phase.md) の H4 の run（`build/h4-hold2`: main の kernel + ws075-p015 の timer の較正の修正 + p016 の hold、
HDMI 1920x1280 の session）で、App Home (22,16) → Gears (599,617) を 6 回（毎回 title bar の × (1233,413) で閉じる）。起動の約 9 秒後の
scanout の live の buffer で窓（660..1260 × 443..923）の赤・緑・青の画素を 4 画素おきに数えた:

| 起動 | 赤 | 緑 | 青 |
| --- | --- | --- | --- |
| 1（9 秒・19 秒の 2 枚） | 3860・3832 | 883・907 | 766・756 |
| 2〜6 | 3861・3845・3828・3854・3851 | 885・891・903・895・869 | 774・761・762・758・763 |

6 回とも 3 つの歯車が描かれた（画面 `build/ws075-shots/ws075-bug058-gears-from-app-home.png`）。kernel の log に zgears の pipeline
（vs 7568 byte）の compile がある。**再現せず**（6 回）。setup の段の印と SIGSEGV の handler の追加（ticket の再調査の条件）は、再現が
無いので行っていない。

## 残り

- BUG-058 は tracking のまま（再発したとき）。旧の 1 回は ws069-p010（capture 表示、今と違う kernel）で、6 回の無事は直ったことの証明ではない。
- zgears の stdout（session.log）は guest の disk から読んでいない（H4 の stop が読む `/run/user/0/session.log` は tmpfs で disk に無い）。
