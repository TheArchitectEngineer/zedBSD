# q11 / intrinsic track partial uncleared

2026-10-18、source WIP fa4ac8002。maximum keywordをparse/cascadeで保持し、computed fallbackのminmax/keywordをserialize。implicit cycleを共通track-size parserへ統一。horizontal plain min-contentの非置換single-span contributionとintrinsic非stretchを修正。既存native floorを維持しcomputed_maximumで元の上限を保存。初期scope/改訂はPhaseとscope-initial.mdに保持。

- 独立22は6/22→22/22でChromium153一致。隣接inline-blockでfont-size0の空白幅依存を除き、参照結果の同一性も再確認。元の空白付きprobeは残す。DOM29/29、CSSOM/mutation各12/12。
- 公式Grid全502、same ID/hash/callback名/重複順/checkpoint/rawを照合、欠落/追加/順序変更0。whole248 PASS/254 FAILを維持、callback6,421→6,471（74改善/24後退）。全遷移をafter/summary.jsonへ保存。
- 静的Grid529は227→228 PASS、旧PASS後退0。新PASSはtable-with-infinite-max-intrinsic-width。
- host全buildとtarget exact clang4source compile warning0。変更関数full/manual review/clang-format19、関数内自動候補0、file候補341→330。Acid3 score100/inventory100/画素差0、Acid2差873不変。

24後退はcolumns/rows各composition/interpolationの4 IDだけ。beforeはfit-contentとmax-contentを共にautoへ潰したため、未実装animationで動かないunderlyingと期待endpointが同じ文字列となりPASSしていた。afterはmax-contentを保持して相違を露出。独立probeはanimate/Animationが両pairともundefined、underlying/animatedはauto autoのまま、期待endpointだけbefore auto auto→after auto max-contentを示す。native animation/transitionを実装したとはしない。

old callback PASS後退0のcriterionは未達なのでq11-i01/Phaseはuncleared。既存false PASSを復活させるためにserializerを壊さない。known animation欠落はp248計画へリンクし、実装verified output後に新attemptで再照合する。userのnon-blocking Bug transfer判断はないのでclearしない。一般span/row/vertical/replaced、line names/fit bounds等もwhole残り。

runner auditでgzip後のraw JSON unlinkを発見した。この実行の全rawはgzipからbuild内へ同じbytesで復元し、gzipを含め何も削除しない。repo evidenceは可逆なgzip版を保持、次のQueueでrunner自動削除を外してから再使用する。test内容/期待値/固定manifestは未変更。

共有Master/Queue/GitHub投影pending、production link/runtimeとp100全source関門も未達。全3,805 IDの100%未達。次は独立p247 zero-fontとrunner retentionを有限scopeに選定する候補。
