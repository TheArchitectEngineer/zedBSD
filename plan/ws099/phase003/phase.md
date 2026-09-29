<!-- awesome-plan project=zedbsd record=ws099p003 -->

# ws099-p003: 試験の固定の待ちを、compositor の READY を待つ形にする（BUG-115）

Phase ID: `ws099-p003`
Parent: [WS099](../ws.md)
Status: cleared（2026-09-30、サブエージェント、worktree `wt/ws035`。QEMU の Venus）
Phase disposition: normal
Bug: [BUG-115](../../bugs/BUG-115.md)
Queue: なし（2026-09-30 main の割り当て「p003: 固定の待ちの試験を socket か `ZWL READY` を待つ形に。既存の `plan/ws035/tests/` は互換を保つ」）

## 原因（p001 の切り分け）

`plan/ws035/tests/zdesktop-p*.sh` の多くは、compositor（`/bin/wayland`）を起こして `sleep 3`〜`sleep 5` の後に client を起こす。起動
（`ZWL READY`、socket の作成）がその秒数より遅いと、client は socket の無いうちに接続に失敗する（BUG-115: 古い image の起動 6 秒超で
`WLSHM FAILED setup errno=5`）。

## 変更

`plan/ws035/tests/` の 42 本・43 箇所の `/bin/wayland … > LOG 2>&1 </dev/null & sleep N` を、
`… & for w in 1 2 … 30; do grep -q ZWL.READY LOG 2>/dev/null && break; sleep 0.5; done; sleep 1` にした（python の一括の置き換え、
diff は各 1 行）。

- その試験が compositor の出力を書く log（多くは `/tmp/zdesktop.log`）に `ZWL READY` が出るのを待つ（上限 15 秒）。READY は起動に成功すれば
  どの mode でも出る（`main.c`）。socket の名前が試験ごとに違っても同じ形で効く。
- `$` と引用符を使わない形にしたので、guest に渡す文字列が単引用符でも二重引用符でも、変数の中（p013・p105 の `start_desktop=`・`start=`）でも
  同じに働く（最初の版は `$i` を使い、二重引用符の試験では host で展開されて待ちが効かなくなる危険があったので改めた）。
- 互換: 引数・出力・判定は変えていない。READY が 15 秒出なければ今までどおり先へ進む（失敗の仕方は同じ）。READY の後の 1 秒は、
  以前の待ち（起動 約 3 秒 + 残り）で窓の mode に入っていた余裕を保つため。
- 変えていない: 他の WS の試験（`plan/tools/titlebar/`・`plan/tools/files/`・`plan/ws079/tests/`・`plan/ws089/tests/` など）にも同じ固定の待ちがある。
  WS099 の新しい試験（`plan/ws099/tests/`）は socket を待つ形。

## 確かめ

- 文法: 変えた 42 本を `sh -n` で確かめた（0 件）。
- BUG-115 の条件（main の古い image の複写 `build/ws099-bug115-old.img`）: `zdesktop-p072.sh` が **PASS**（変更前は `WLSHM FAILED run=a setup errno=5` で FAIL）。
- 回帰（今の image `build/ws099-criteria.img`、`plan/ws099/tests/criteria.sh … C9`、新しい guest で 1 本ずつ）: p052・p053・p072・p076・p126・
  p128・p134・p137・p138 の 9 本が全て PASS（`build/ws099-p003/results.txt`）。
- 未実施: 変えた 42 本のうち C9 の一覧に無い 33 本の実行（固定の待ちの置き換えだけで、手順は変えていない）。実機。

## Resume point

2026-09-30: cleared。BUG-115 は main が resolved にしてよい（ticket と Bug Board は main の担当）。
