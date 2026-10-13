# q1015-i01: Interop 2025 textdecoration 全16 testharness ID

2026-10-14 00:08–00:11 JST。ユーザーの Interop 2025 100% までの有限 Queue 事前承認に基づく最大90分の試行。固定 [manifest](../../../phase173/evidence/20261013-q1012/manifest.json) から `textdecoration` の testharness 16 IDを全選定し、既存 `run-interop2025-harness.py` で単一 HTTP origin、virtual settle 5秒、process timeout 20秒で実行した。固定WPTは `/tmp/zedbsd-interop2025-wpt`、browserは `build/ws074-host/plain/browser`。各IDのraw callback、completion、stdout、browser hashは [report.json](report.json) と `test-01.json`–`test-16.json` に保存。

結果: **16件中4 PASS / 12 FAIL**、harness error・timeout・無出力0。subtestは47 PASS / 132 FAIL、計179。選定領域の残り未実行0だが、この領域の PASS 100% は未達。他の18領域や公式全3,805件の率に外挿しない。

主な共通原因候補: computed 系 `text-decoration` / `text-decoration-line` は WPT の `computed-testcommon.js` が呼ぶ `CSS.supports` に対して browser 側に `CSS` global がなく、全32 subtest が `CSS is not defined` で失敗。`text-decoration-color`、`-style`、`-skip-ink` の valid/computed 系は CSSOM の property設定値または computed style が空で、実装不足が疑われる。shorthand・複合line値にも失敗がある。invalid 系4ファイルはPASS。これは固定WPTの期待と現在のbrowser実装の差であり、runnerのcallback欠落ではない。browser原因の最小再現と修正は後続Queueで行う。

確認: manifestと実行reportで16 ID・重複0、全16 completionあり。WPT、browser source、`master.md` は変更なし。Acid3はbrowser source無変更のため再計測省略。通常sandboxで localhost bind が `Operation not permitted` となったため、同じコマンドを承認済み elevated 実行で計測。未達は p189 の全1,783件のcoverage、p173の全3,805件baseline、全件 PASS 100%。
