# q19 validated XHTML classic inline producer / terminal

最上位strict XMLをparse/project/adoptした後、loadより前にHTML namespace・exact lowercaseのclassic inline/CDATAを既存native backendへ接続した。全候補snapshotとsaved currentScriptをcallback前にrootし、connected/started/template/detachedを再確認する。currentScript設定/復元、self-removalと4000 allocations、後続snapshot削除をnativeで検証。data/foreign/case/templateを誤実行しない。parser streaming timing・外部/module/document.write/full XML scriptingは未実装のまま、whole p196はclearしない。

[独立fixture](lifecycle.xht)・[Chromium期待5行](lifecycle.expected)・[native5行](lifecycle.out)がexact一致。snapshot削除安全性は[別native例](snapshot-safety.xht)の[first-safe/last-safe](snapshot-safety.out)で確認（validated full-tree partialの安全性でありparser streaming準拠を主張しない）。q18 adapter無しXML3行もnativeから得た。p190元10対照が通り、追加capture境界/実pixel graph/fuzzy/参照graphを合わせ[22対照](../../../phase190/evidence/20261018-q19-i02/runner-controls-final.log)を通過。

新sourceはpage/page.cだけ、source/tool WIP1c6991441。[source/pair proof](source-proof.json)。host build warning0/target single-source warning0、C全文/browser component全文・clang-format19（変更関数/ColumnLimit0・multiline signature保持）/手動ownership・error/bounds/ABI/Wayland境界を確認。[generated3関数style0](generated-style.log)、[full-file legacy候補は1減/追加0](style-comparison.json)。full-source conformanceはWS最終p100へ残す。DOM35/35、XML model249/249・XML DOM54/54。host-frame-loadは必須2 URL無しの誤起動exit2であり検証に数えず、childのpublic native controlを確認した。target全体/link/runtimeは共有toolchain制約でskip/pending。

[Acid](acid-report.json)はscore/inventory100・画素差0、Acid2差873を維持。全1491 harnessは491 PASS・callback11531 PASS/20226 FAIL/14 NOTRUNを維持、whole/callback後退・追加・欠損・順序変更0。全static1561は839 PASS/722 FAIL・全reference pixel metrics unchanged。strict48は39 PASS/9 readiness_missingを全ID対応で維持。scripted413は全native attempt/568 capturesで82 PASS・73 pixel FAIL・242 unsupported_environment・16 readiness_missing。確認済み1451/3805（38.134034%）、新規計測PASS82増。harness未attempt292、環境/API/worker等の未達は分母に保持。

[全harness比較](harness-comparison.json)・[全1491 raw](harness-raw.tar.gz)・[static比較](static-comparison.json)・[static report](static-report.json.gz)/[progress](static-progress.jsonl.gz)・[strict48 raw](strict48-raw.tar.gz)・[入力proof](input-proof.json)を保持。全report/checkpoint/ID/hashとtar元bytes read-back一致、source/pair/WPT cleanを再確認。scripted413のsource・all PPMS/PNGS/console/binary console・graph/fuzzyとpinned比較の詳細は[p190 evidence](../../../phase190/evidence/20261018-q19-i03/README.md)。

q19-i01 producer・i02 consumer・i03 measurementは各scoped partial cleared。p196/p190 whole・WS whole、native API/環境・target full/p100・shared projectionは残す。q18 uncleared履歴を後日成功で書換えない。push無し。
