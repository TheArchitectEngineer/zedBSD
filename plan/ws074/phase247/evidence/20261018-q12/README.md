# q12 / zero glyph own acceptance cleared

2026-10-18、source WIP fa7488df5。text_select_fontでlogical zeroをpositive rasterizer requestと分離し、text_glyphはzero advance/empty inkを返す。positive/fractional既存選択・cache lifetime・公開browser ABIは不変。runnerはi01でraw JSONをgzip後も保持する。

## Verification

- Native zero font56/56、既存text20/20。regular/bold/mono/fallback/missing glyph、metrics/character widths、positive cached glyphを確認。DOM14条件は3/14→14/14でChromium一致、全DOM30/30、CSSOM/mutation各12/12、p185 strut PASS。
- 100×100 software paintでzero黒pixel15→0、positive黒pixel305維持/前後差0。PNG/fixture/比較JSONを同梱。
- 固定testharness既存適格1,455 IDのbefore/after全件、raw JSON/gzip同値・checkpoint/reportを全照合。両方489 PASS/918 FAIL/3 harness_error/21 no_completion/24 no_output。callback11,534 PASS/20,189 FAILを維持。whole/callback後退0、欠落/追加/名前順変化0。eligible hash `d9b36bd0c947a35529eeb11e46b4d2038053cc0b8786a2354be87536e563dae2`。
- 固定static reftest全適格1,561 IDは両方839 PASS/722 FAIL、旧PASS後退0、reference画素差countの変化0。固定Interop whole PASS増分は0。補助font-size-zero 3件の0→2 PASSは固定3,805分母外。
- host全build/target exact clang font.c compile warning0、変更関数C全文review/format19・自動候補0（full file既存候補1はunchanged）。versions/commands/hashを同梱。Acid3 score100/inventory100/画素差0、Acid2差873不変。

Before pairはq11 fa4ac8002の固定browser/libbrowser、afterは同じdirectoryへfreezeした新pair。各長期測定中にpairを上書きしていない。通常XDG_DATA_HOMEをphase別build/dataへ設定、settle5,000ms/process20s/固定font、各coordinator最大3,900s・Queue90分内に終了。全rawはbuildで非圧縮/圧縮を保持し、repoには可逆なgzip版を保存。

## Outcome and remaining work

p247のlogical zero/advance/ink基準を満たし、Phase/q12-i02 cleared。i01 retentionもcleared、q12 finished。補助zero-1の後続p y51.20/ref67.20差はzero-height生成lineをempty扱いする既存margin-collapseの残りで、p249計画へ。font-size0のglyphは既にink無し、lineとblockの余白をこの修正で偽装しない。

全testharness1,783中328未対応、reftest1,974中413未対応、crash48の正式wait/load/paint確認、full target link/runtime・p100全WS source規約関門等は未達。固定全3,805 IDの100%ではない。q11 animation24 callback未達/p248も維持する。共有Master/Queue/GitHub publish/close・再利用fixtureのTools/AAT登録/移設はQ1投影pending。push無し。次はp189 plain URL variant36の有限Queue候補。
