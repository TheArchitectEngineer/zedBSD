# q21 checkpoint: flex-basis grammar・computed・content base

Native source WIP063d87c95。contentをautoから区別、negative literal/none/normal拒否、absolute/pure-percent calc range、非置換horizontal row/columnでmain指定/minmax/cacheを一時分離する測定を実装。公開ABI2/Wayland/runner/WPT期待値不変。

確認済み: Chrome39行=native、DOM36/36、CSSOM12/12・mutation12/12、既存ratio40/40。固定parsing3は27 callback全PASS（5改善）。全static1561=844 PASS/717 FAIL、旧839後退0・5改善。新5件は旧/新native16 capturesと無改変pinned comparatorでも全FAIL→PASSを確認（five-static-gains.json/tar）。scripted413は105 PASS/283 FAIL/5環境未対応/20 readiness missing・805 captures全RGB不変、全388 edgeが無改変pinned comparator一致。strict48=39 PASS/9 readiness missing維持、Acid3 score/inventory100・pixel0/Acid2既知差873維持。全1491 harness terminal gateは進行中、whole/partial clearanceは未判定、latest verified totalはq20の1474/3805。

C全文5関数/checked-in .clang-format19（ColumnLimit0と定義行形式）・generated style0、full-file legacy count不増、host/target4ソースwarning0。最後の2条件改行は非空白bytes不変・production target flex.o全bytes一致（format-only-proof）、測定pairのCの意味とruntime code不変でglobalを再利用。before/final C bytes・source hashを保持。canonical指定calcの既存順序差はpreflight.out/Chrome logへ保存、grammar/computed/layoutと切り分け未修正を明記。

全scripted raw/PPM/PNG/console/source/checkpoint・crash rawをtarに保存し全member byte read-back。static既存toolは全1561のpixel metrics/report/checkpointを保持するが全件の個別画像は保存しない制限があり、新5件は別途全rawを保存した。harness raw/gzip/checkpoint/terminal比較は進行中。最初のsandbox socket EPERM実行は無効なsetup evidenceとして別保持、正式nativeのPASSに混ぜない。native runtime/target full/p100/full WS・p189/190未対応・shared projection/GitHubはpending、push無し。
