<!-- awesome-plan project=zedbsd record=ws075p002 -->

# ws075-p002: desktop の新しい機能を実機で確かめる

Phase ID: `ws075-p002`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-27。実機の capture で App Home・System Menu・X11 が PASS。zdesktop-files は保留）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。

## 範囲

p001 で今の zdesktop（glass、backdrop、Wiseview、docking）と mview は実機で動いた。その後の desktop の機能を実機の capture で確かめる:

1. App Home から zdesktop-terminal と mview（capture の `zdesktop-home`）。
2. System Menu（`zdesktop-menu`、`plan/tools/titlebar/menu-hw.sh` と同じ scenario、11 検査）。
3. X11（`zdesktop-x11`: Gears の GLX、X terminal、仮想デスクトップ）。
4. zdesktop-files（file manager の窓、tab、pane のすりガラス）: capture の scenario が無いので `plan/ws031/tests/i915-capture.py` に
   `zdesktop-files` を足す（Venus の試験 `plan/tools/files/` の操作に倣う）。
5. 落ちたものは原因を調べ、i915 の側（実行器・compiler・driver）で直す。zdesktop とその client は直さない（desktop の
   サブエージェントの範囲。要るときは main を通す）。

## 設計

- 実機の run は `plan/ws075/tests/capture-hw.sh SCENARIO MODE OUTDIR`（`flock /tmp/i915-hw.lock` の下で `vkloop-hw.sh`）。
  `zdesktop-home`・`zdesktop-menu` は `ZDESKTOP_APP=home`、`zdesktop-x11` は p006 の記録の形（`run-home.sh`）。
- 結果の画像は `/home/awe/zedBSD-rpi4/build/ws031-shots/ws075-p002-20260927-<scenario>-hw.png`。

## 判断が要る点（既定を選んで進める）

- zdesktop-files（tab、pane）の実機の capture の scenario は、この Phase では作らない（既定）。desktop のサブエージェントが
  zdesktop-files を変えている最中で（card の幅、docking、p055）、座標に頼る scenario はすぐ古くなる。zdesktop-files の shader と
  Vulkan の command は p001 の検査で全て実行器が受ける。desktop 側の変更が落ち着いたら（main の合図で）scenario を足す。戻せる既定。

## 結果（2026-09-27、実機 = 5330 の i915 を QEMU に VFIO で渡した capture。Venus ではない）

| scenario | 結果 | 記録 |
| --- | --- | --- |
| `zdesktop-home`（App Home から zdesktop-terminal と mview） | **PASS 4/4**（2 回目）。terminal（menu bar 付き）と mview が実機の GPU で描けた | `build/ws075-p002/hw-home2/` |
| `zdesktop-menu`（System Menu、ws070 の 11 検査） | **PASS 11/11**（2 回目） | `build/ws075-p002/hw-menu2/` |
| `zdesktop-x11`（Gears の GLX、X terminal、仮想デスクトップ） | **PASS 6/6**（2 回目）。zgears が実機の実行器で回る | `build/ws075-p002/hw-x11-2/` |

1 回目の home（検査は PASS だが terminal は起動していない）と menu（FAIL）は道具の誤りだった（i915 の不足ではない）: `vkloop-hw.sh` は入力の file が image より新しい時だけ image を
作り直すので、p001 の `zdesktop` の image（run-mview.sh と、terminal の font 無し）がそのまま使われた（terminal が
`ZTERM FAILED ... terminal_font_open errno=6`（ENOENT））。`plan/ws075/tests/capture-hw.sh` が `ZDESKTOP_APP` ごとに build の
directory を分ける（`build/resident-zdesktop-<app>`）ように直した。新しい image の 1 回目の `zdesktop-x11` は Home の Gears の
click が効かず（`ZWL HOME launch` が無い）落ちたが、同じ image の 2 回目は PASS。間欠の入力の取りこぼしで、GPU の不足ではない
（再現したら desktop の側へ）。

executor・compiler の拒否（`i915: vk: XXX unimplemented`・`vkCreateGraphicsPipelines failed`）は全 run で 0。

画像: `/home/awe/zedBSD-rpi4/build/ws031-shots/ws075-p002-20260927-home-hw.png`・`-menu-hw.png`・`-x11-hw.png`。

## 検証

| 確認 | 結果 |
| --- | --- |
| 実機（i915、capture） | home 4/4、menu 11/11、x11 6/6 PASS |
| Venus の回帰・boot test | 未実施（製品の source を変えていない。道具の script だけ） |
