# q27 native ErrorEvent prerequisite partial / cleared

native ErrorEventをprivate registryへ追加し、Event継承/subclass/readonly enumerable message・USVString filename・uint32 line/column・arbitrary error（default undefined）、辞書変換順/brand/GCを実装。実WindowのIDL onerror handlerへ5引数を渡しBoolean trueだけcancel、通常listener/element/generic Eventの従来semanticsを維持。独立native28条件、Chrome27一致＋一次IDL通りのfilename USVString1差を明示、非保守的stackのreentrant GC/retired child ownership・実Document/realm回収9条件を通過。

final frozen pairでharness1508のraw PASS494を維持、whole/callback/completionはq26と全件同値。初回の既存FAIL1 IDだけprocess timeoutとなったが、全他suite終了後に同じ20秒/settle5000で1件だけ再確認し8.902秒で完了、全4305 callbacks/whole FAIL/completionが旧結果と一致。初回raw/checkpointsとretryを両方保持し最終selectionを明記。static1561は844 PASS/717 FAILで全pixel metrics不変、scripted413は105 PASS/283 FAIL/5 unsupported/20 missing・805全RGB不変、strict48は39 PASS/9 missing不変。DOM39、CSSOM12+12、producer7/observer10/error9、host/target exact event/window warning0、Acid3 score/inventory100・pixel0、Acid2既存差873を維持。

確認済1481/3805（38.922470%）維持、raw1482、held1は未解除。q27 scoped itemだけcleared、whole p256はproduction reporter/source provenance/muted errors未達でuncleared、p255の公式harness negativeも未達を保持。legacy content attributeのnamed parameter生成、eval/Function/module/worker、external source/CORS metadata、full target link/runtime/p100/WS100%は達成と主張しない。次finite Queueで通常例外の通知経路を実装し、timer-onlyとObserverのofficial harness negative/positiveを検証してheldを再評価する。

## Evidence / standards

C全文§2に従いevent.cの44 required declarationsをtable前に残し14 helpersを正規blockへ移動。宣言multiset/残りtokenとdebug除去後host object全bytes同一を証明しfinal sourceでtarget再compile。原frozen source e2111e1b2とconformance source70a5aaf54を区別する。checker raw19 goto flagsは全文§14が許す単一shared cleanupへのforward jumpで、他0/新q27 scope未解消0。q26 environment_entries_snapshotの宣言位置の旧conformance不備はp255/history/p100へdated correction、機能出力は保持しenvironment.cは今回変更しない。

Approval64a5b1725。summary/4 suite comparison/source-proof/native-runs/versions/target-commands/primary-proofが索引。harness-all-rawは全1508＋reviewed retryの原reports/checkpoint/stdio・generated source/provider資源、scripted-all-native-capturesは805全captures/ready/header/画像、crash-all-rawは48全raw、acid-all-rawは27全assets、native-controlsは初回Proxy未実装停止・Chrome/native原console/ソース/command/失敗logを保持。全tar membersを順次byte read-back済み。固定WPT/runner/manifest/期待値/分母は不変。staticの原runnerは全metricsとfail2/最終画像だけを保存する制限があり、全原画像やp190 full graph/fuzzy oracleを認定しない。

static初回は通常sandboxのlocalhost socket拒否で停止、raw/logを保持して別folderへ通常の承認経路で再実行。host buildを完了確認前に重ねた中間出力は正式測定へ使わず、全終了後の単独build完了を確認してからfinal pairをfreezeした。final-format-proofの「改行修正」はbefore/after同一のno-opだったためPhaseのdated correctionとformat-equivalenceへ訂正。実際に変えた宣言順はprototype-order-proofで別に実証している。

Chrome153はfilenameのlone surrogateを保存するが、pinned html.idlとHTMLはUSVString。nativeは選定済み一次IDL通りrepairし、この1差を隠さない。Proxy constructorは未実装なのでbrand対照は別genuine Event/偽prototype/plain/nullのactual receiversを使用、Proxy対応は主張しない。legacy content attribute compilerのnamed parametersは今回のsource bounds外、IDL handler dispatchの確認と区別する。

[次Queue向けsource調査](../../source-provenance-investigation.md)。code metadata候補であり実装未選定。shared Master/Queue/cache/GitHub projectionはQ1所有でpending、push/削除/make check/QEMUは無し。

Event ws074-q27-main-header-conformance-followup-20261018: q28全文監査で、q27新host-error-events.cのpublic main定義前がone-line commentだったと確認（C全文§3はmulti-lineを要求）。q27 scoped i01のconformance0/clearanceをこの限定点によりunclearedへ訂正。元native28/GC9/全corpus/host/target/API出力とscore1481は保持し、原cleared記述を履歴へ残す。q28-i02の実装前scope supplementに基づきheader commentだけをmulti-line化し、全code tokens/string不変をpublic-main-header-proofへ保存。q28 final build/native9で修正版を再確認する。元q27を後日の修正に合わせて成功へ書換えず、follow-upとして扱う。
