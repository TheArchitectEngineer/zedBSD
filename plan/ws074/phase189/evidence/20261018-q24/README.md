# q24 pinned IDL correction

疎checkoutに無いinterfaces/*.idlをpinned Git原blobからgenerated専用GETで配信し、5種類/2 ID計6資源の原hash・query・MIME・header metadata不在と全rawを検証。独立wire/本物404/未配信Git資源nonPASS/metadata拒否を含む15 controls通過。全1508を235.8秒で再測定、全whole/status不変、IDL2以外1506の全callback/completion objectは完全同一（旧1491を含む）。IDL2のfetch errorは解消し、native JS compilerがyield*を未実装のため公式WebIDL2 parser実行に失敗する正しい残差へ到達。 raw harness494 PASS/958 FAIL/4 harness_error/26 no_completion/24 no_output/2 multiple、callback11540 PASS/20643 FAIL/14 NOTRUN。known native prerequisite holdback1は保持し確認済1481/3805（38.922470%）、未attempt275。native C/固定WPT/期待値/分母不変、static844/scripted105/crash39/DOM/C/Acidは同一q21 pairの証拠を再利用。 q24両scoped attempt cleared、q23元uncleared履歴は保持。whole p189/WS/p100/target fullは未達、shared projection/GitHub pending、push無し。次はp254 native mark producer、p255 observerでpositive通知を検証するまでheld PASSを確認済へ加算しない。yield*は次のVM候補としてp189に残す。

## Evidence / limitations

Source WIP26980308b、approval fb571a291、q23 correction8b98b77ba。summary.json/exact-callback-proof.json/source-proof.jsonと全raw archiveは原本、generated HTML/JS/IDL/stdio binary/provenance、全report/checkpointを保持。全tar memberをbyte read-back済み、WPT tracked clean。fixed2025 FileHandlerのload_headers/get_headers/guess_content_typeとconstantsを読んでIDLはapplication/octet-stream、隣接__dir__/resourceのheaders/sub.headers不在をGit treeで確認。未対応metadataがあると配信成功を偽らずunsupported inputとする。原Gitを読むだけでcheckoutやGit metadataを変更しない。

classificationは既存5 pureを含むOutcomeTests全6を誤って指定し、font fixtureのTemporaryDirectory cleanupも走った。既存script内の削除を避けるsession規則に反したため記録し、以後は5つのmethodを明示指定する。再実行は不要。15 generated controlsはmkdtempで全入力を保持。archive最初のvalidationはSyntaxErrorがstderrにあると仮定して停止、実際はstdoutのため両channelの原consoleを確認するよう訂正し再実行。製品/WPT/成功条件を変えていない。

Python専用full standard無し、Guardrail/既存tool/FS規則を直接適用。C源を変更していないので新host/target/画素回帰は不要。p100全標準適合とtarget full link/runtime、p189未対応275/API残差、whole100%は未達。
