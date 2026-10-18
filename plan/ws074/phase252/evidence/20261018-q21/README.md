# q21: flex-basis grammar・computed・content base

## Terminal outcome

flex-basis grammar/computed/content baseをnative修正。固定3 IDは27/27 callback PASS、2 whole/7 callback改善（parsing3は5、shorthandは2）。全1491 harnessは493 PASS（+2）・11538 PASS/20219 FAIL/14 NOTRUN、old whole/callback後退・欠損・追加・順序変更0。全static1561は844 PASS/717 FAIL（+5）・old839後退0、画素変更は改善5 IDだけ。5件の旧/新16 capturesを無改変pinned comparatorでも全FAIL→PASS確認。scripted413は105 PASS/283 FAIL/5 unsupported/20 missing・805 captures全RGB不変/388 edge primary一致、strict48は39 PASS/9 missing維持。確認済み1481/3805（38.922470%）、genuine native whole改善7。未attempt harness292と環境/API/native残差は分母保持。 host scoped partial cleared、p252 wholeの受け入れは今回のhost partialだけで認定せずuncleared。replaced/一般intrinsic/縦書き・negative inner base等の既存別未実装面とtarget full link/runtimeも保持。p240/p241/p189/p190/WS全体やp100を先取りclearしない。指定calc canonical順序の既存差は未修正。shared projection/GitHub pending、push無し。

Source implementation063d87c951ce940f59b61750f1ef046ef9dfe8ff、final whitespace70e40cb7a0f1b30fb5fc52555e2fef91621883d7。final host/target/native39とELF runtime全allocated sections一致proofでwhitespace後の意味を再検証。独立Chrome39/DOM36/CSSOM12+12/ratio40/host・target4 warning0/変更5関数full C・project formatter/style0、Acid3 score/inventory100・pixel0/Acid2差873維持。全harness raw/gzip/checkpoint・全scripted/crash rawのtar memberをread-back、fixedID/manifest/WPT tracked clean/source hashes一致。staticは全report/pixel metrics保持・全件画像未保存の既存制限を開示し、新改善5件は全旧新rawを保持。sandbox EPERMの無効setupは別記。Python full専用standard無し、既存tool/Guardrailを直接適用。次候補p189 HTTP停止待機の短縮→同1491全callback同値確認、続いて未attempt292/環境5/native残差の有限選定へ。

## Independent/initial checkpoint

Native source WIP063d87c95。contentをautoから区別、negative literal/none/normal拒否、absolute/pure-percent calc range、非置換horizontal row/columnでmain指定/minmax/cacheを一時分離する測定を実装。公開ABI2/Wayland/runner/WPT期待値不変。

確認済み: Chrome39行=native、DOM36/36、CSSOM12/12・mutation12/12、既存ratio40/40。固定parsing3は27 callback全PASS（5改善）。全static1561=844 PASS/717 FAIL、旧839後退0・5改善。新5件は旧/新native16 capturesと無改変pinned comparatorでも全FAIL→PASSを確認（five-static-gains.json/tar）。scripted413は105 PASS/283 FAIL/5環境未対応/20 readiness missing・805 captures全RGB不変、全388 edgeが無改変pinned comparator一致。strict48=39 PASS/9 readiness missing維持、Acid3 score/inventory100・pixel0/Acid2既知差873維持。当初のcheckpoint時点では全1491 terminal待ちでq20の1474/3805を維持していた。terminal outcomeは上記。

C全文5関数/checked-in .clang-format19（ColumnLimit0と定義行形式）・generated style0、full-file legacy count不増、host/target4ソースwarning0。最後の2条件改行は非空白bytes不変・production target flex.o全bytes一致（format-only-proof）、測定pairのCの意味とruntime code不変でglobalを再利用。before/final C bytes・source hashを保持。canonical指定calcの既存順序差はpreflight.out/Chrome logへ保存、grammar/computed/layoutと切り分け未修正を明記。

全scripted raw/PPM/PNG/console/source/checkpoint・crash rawをtarに保存し全member byte read-back。static既存toolは全1561のpixel metrics/report/checkpointを保持するが全件の個別画像は保存しない制限があり、新5件は別途全rawを保存した。harness raw/gzip/checkpoint/terminal比較も最終保存・照合済み。最初のsandbox socket EPERM実行は無効なsetup evidenceとして別保持、正式nativeのPASSに混ぜない。native runtime/target full/p100/full WS・p189/190未対応・shared projection/GitHubはpending、push無し。
