# q08-i02 primary Grid再検証 / uncleared

2026-10-18、source WIP d820c977aの固定final binaryで公式Grid502を4 shard全測定。q07全502とID/callback名/重複順/checkpoint/rawを照合し欠損/追加/順序変更0。whole247 PASS/255 FAILを維持、旧whole PASS後退0。しかしcallbackは9改善/10後退、PASS6,410→6,409。全遷移はsummary.json。

q07のminmax(auto,10px) callbackは回復した。後退10件はgrid-minimum-size-grid-items-025のminmax(auto,0px)で、preferred heightやmargin/padding/borderによりautomatic minimumが最大値を超える場合。固定行guardがこれを普通の固定長と同じに扱い、以前10pxだった2番目のitemを0pxにした。plain fixed lengthとexplicit minmax下限の種類は現在のcss_trackで失われている。

q08-i02はuncleared。q08-i01の独立7条件/静的529の証拠は保持するが、この新しい受け入れ範囲の不備によりi01のclearanceもinvalidateし、q08全体をunclearedで終了する。旧出来事は消さず、p244 current stateにこのfollow-upを記す。

次の有限Queueでminimumの種類をparse→cascade→layoutへ保持し、automatic/intrinsic minimumを含む場合は固定上限を維持できると確認したときだけguardを適用する。未解決の一般minimum-floor/均等分配を修復したとは扱わず、wholeの残りを保持する。Grid502と静的529を再検証し、10後退の回復とq07で得た値取得の改善を維持する。p245のno-box minimumはこの修正後の候補。

独立used-track14/14は維持。i01ではDOM27/27・host/target compile warning0・Acid3差0/Acid2差873を確認。新たなbuild/Acidは同じsourceの再測定なので不要。Master/共有Queue/GitHub投影pending、全3,805件100%未達。
