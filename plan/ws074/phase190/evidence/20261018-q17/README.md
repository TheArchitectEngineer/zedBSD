# q17 strict crash actual48（終端global gateは別途記録）

q15/q16で前提未達だった公式待機を、native post-font pumpとordinary attribute observerを備える同じfinal frozen pairで実行。判定条件を変更せず固定48 IDすべてを実行し、**39 pass / 9 readiness_missing**。exit0/image単独ではPASSへ変換しない。9件は全てexit0だが公式completeなし。signal/timeoutは0。

[compressed raw report](crash-report.json.gz)と[checkpoint](progress.jsonl.gz)は全48 recordを同順に保持。[画像index](image-index.json)は各caseのPPM・PNG・RGB hashを保持。原PPM/個別stdout/stderr/JSONとcheckpointはreport記載のretained build directoryに残し、全48のPNGをlossless変換してRGB一致をread-backした。original WPT sourceはpinned blob byte一致を毎case確認し、bootstrap insertion位置・original/instrumented hash・nonce/URL・公式wait script blob hashはraw reportに保持。WPTファイル自体は編集しない。

[最終pair/source hash](source-proof.json)、[全15対照](controls.log)。source f1b83fa8f（engine）、new fixtureの後続GC圧力追加は挙動判定を変えず独立Chrome期待値を再確認。engine library 4ac8b0037013c43b80f84703ca51deb803d8a509e2652ea1403a2141cb40502a、browser 25a66385edb7811c04e5e04fddd6bf3060ef2883f26d2781adbecf9d87debc02。

## 9件の残り

* Anchor composited scrolling 002/003/004/005: inline moduleにtop-level awaitがありmodule evaluationがEINVAL。unsupportedをPASS扱いしない。p216のmodule evaluation面が残る。
* inline-grid-try-fallbacks: animationendでtest-waitを外すがnative CSS animation lifecycle未対応。p248 native animationの残り。
* document-element-detached / list-style-position-style-change / root-element-display-none-during-transition: startViewTransitionの呼出しでTypeError。native ViewTransition API/lifecycleが残る。
* iframe-transition-destroyed-document: style要素load/errorからのhandler起動を含む経路が未到達。無consoleを成功とせず、stylesheetイベント・子Document transitionの独立native調査が必要。

これはp190のstrict48 coverage partialであり、未対応static413やWSの100%を達成したとはしない。global1491/static1561の同一final pair比較が終わるまでq17 terminal clearanceと全体確認済み総数は未確定。共有Master/Queue/GitHub投影はpending、pushなし。
