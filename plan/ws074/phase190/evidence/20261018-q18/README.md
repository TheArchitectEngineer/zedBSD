# q18 first-ready client: uncleared / XHTML producer prerequisite

公開SDKだけを使う試験clientを実装。最初のready成功callで撮影し、500ms後の未来timerを先取りしない（lateはbudget32・赤、TestRendered解除はbudget64・緑、delay100ms解除はbudget132・緑）。callbackは状態保存/console出力だけ、同view再入なし。font3/単一nonce・URL・readyState/除去済みprobe ackと外側成功callを要求。main失敗/重複/zero-loop/deadlineは非PASS。probeは原scriptの前に除去し、その後のnative getElementsByTagName('script').length=1でoriginal scriptだけが見える。

native必須10対照は**9/10**。plain/late/TestRendered/delay/query/child/throwはPASS、永久waitはexpected readiness_missing、zero-loopはexpected bounded timeout。**XHTML positiveはreadyなしでFAIL**のまま保持した。first runの8/10中plain failureはcontrolが既存未実装Document.scriptsを読んだためで、標準の既存getElementsByTagNameへ修正し2回目は9/10。XHTML criterionをskip/expected failureには変えていない。[全raw/console/PPM/PNG](native-controls.tar.gz)と[最終summary](native-summary.json)、各logを保持。全tar memberを元bytesとread-back照合。

[独立XHTML原例](xhtml-inline.xht)はadapterを含まない。Chromiumはinline→DOMContentLoaded→loadの3行、nativeはexit0/console0行。[native](xhtml-inline.native.json)・[Chrome](xhtml-inline.chrome.out)。page_load_xhtmlがvalidated XMLをadoptした後すぐpage_fire_loadし、script実行を接続していないことをsourceで確認。既存p196はscript-free partialのみcleared、whole script/lifecycle未達を元から保持しているため、その必要native outputを新Queue候補とする。child XMLには既存inline backendがあり再利用設計を検討する。

新C sourceはC全文/browser component全文・clang-format19/生成style0・手動ownership/error/bounds/SDK境界を確認。host warning0・target single-source warning0、ELF NEEDEDはlibbrowser/libcのみ、RUNPATH=$ORIGIN。初回compileはfontフィールド名誤りを指摘し、公開headerのsans/monoへ修正した履歴logを保持。新client source/instance hashesは[source proof](source-proof.json)。engine/app/public SDKは無変更、libbrowserはq17とbyte同一。Python専用full standardは無いため既存tool/Guardrailを直接適用。

q18-i01はrequired XHTML positive未達でuncleared、i02は必要output不足で未着手uncleared。Python transport helperはi01 controls用だけで、pixel graph/full runner/main/actual413は未実装・未実行。正式PASS増分0、確認済み1369/3805はq17 sourceのまま。旧global1491/static1561/crash48はengine不変のため再測定しない。p196 native XHTML inline/lifecycle→p190同じ10対照再検証→scripted413を次の依存順とする。shared projection/target full/p100はpending。
