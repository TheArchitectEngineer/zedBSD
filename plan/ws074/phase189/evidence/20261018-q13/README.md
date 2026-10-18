# q13 / plain HTTP variant部分 cleared

2026-10-18、q12 fa7488df5 frozen browser/libbrowserで測定。classifierとrunner validationはURL pathからsource/HTML/HTTPS/worker/driver条件を判定し、capture URL/selected identityにはquery/fragmentを保持する。結果判定/REPORT_SCRIPT/WPT/engineは変更していない。

全固定3,805分類を照合し旧candidate1,455維持/追加36のみ、新1,491。HTTPS45、driver等169、worker/substitution33、generated45はtestharness未対応292として分母内に保持。全36 ID/hash/checkpoint/raw JSON=gzip同値、欠損/重複0。whole34 FAIL/2 multiple_completions、callback34 FAIL/14 NOTRUN、PASS増分0。既存plain PASS/FAIL/harness_error/no_completion/no_output各1はstatus/名前/順序/各callback statusをq12と照合し同値。native query roundtripでencoded `a%26b`・query・fragment・hrefの保持を確認。

multiple-completionsの2件はnavigation-api/focus-reset/autofocusの2variant。promise_setupのinitial autofocus expected button/got bodyエラーで各7 NOTRUN、同じharness error completionを2回取得。両方をrawに保持し、都合のよい通知だけを選ばずnonPASSとする。native autofocus/navigationの実装を補ったとはしない。

Python syntax/diff check通過。Python専用全文規約は未登録のため既存styleを維持。engine/source pair不変なので再build/Acid/全plain再測定はしない。変更source/binary hashをproof.jsonへ保存。q13 partial cleared、p189 whole1,783/全3,805 PASS100%は未達。reftest413/crash48とp248等も残る。共有Master/Queue/GitHub投影pending、push無し。次はcomputed fit-content boundの有限修正候補。
