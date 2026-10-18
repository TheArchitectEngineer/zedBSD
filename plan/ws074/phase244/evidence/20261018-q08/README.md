# q08-i01 fixed-only Grid row span / host partial cleared

2026-10-18。grid_size_rowsのspan加算前に、horizontalかつ全行がpx/definite %の固定trackならcontent overflowへ進むguardを追加。end計算はrow_countで上限を先に取り、index加算overflowを避ける。mixed/auto/indefinite %/frは既存の処理へ渡す。公開ABI/solverの他の分配式は変更しない。

独立7条件はChromium153に1/7→7/7一致。px/gap/definite %/border-box/minmaxの小さいminimum/fixed minimum floor/mixed-smallを比較。q07の不備caseと同じauto-height/min-height12の条件を使う。初期のtest作成時に指定height40を与えるとauto-minimum自体が最大値を超える別caseになるとChromeで判明したため、これをcontrolsのminmax-largeとして保存し、既にscope外とした一般minimum-floorの残りへ分けた。expectedをengine出力へ合わせて書き換えず、修正対象の条件をWPTと一致させた。

controlsのauto-auto/indef-percentは前後同一で、Chromiumとの差は未修正。minmax-largeは旧10px/30px→10px/10pxだが正解20px/20pxには未達。この残りを修復したと主張しない。p244 wholeはuncleared、今回のfixed-only host部分だけをclearする。

静的Grid全529 unique IDは225→227 PASS、304→302 FAIL、error/timeout0、旧PASS後退0。改善2件はgrid-minimum-size-grid-items-016/020。manifest/ID/checkpoint/reportを照合した。q07で後退した1 callbackはtarget rawでstatus0へ回復。

DOM27/27、host build warning0、production amd64 Grid単体compile warning0、C全文の新helper/変更paragraph review・clang-format19.1.7・diff check。style candidateは138→138（既存領域のみ）。source-final buildで最終commentを含むsourceをcompileし、i02のbinaryはfinal/に固定。i01のstatic rawは同じ論理codeでcomment追加前のbinaryを使用した。Acid3 score100/差0、Acid2差873。production link/target runtimeは未実施。

q08-i01 host partial cleared、verified outputをp243 q08-i02へ渡す。i02が全Grid502/全callbackを再検証するまでp243の未達回復は確定しない。q07 historyはunclearedのまま保持。Master/共有Queue/GitHub projection pending。
