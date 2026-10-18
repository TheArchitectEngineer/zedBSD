# q19 native first-ready consumer revalidation

q18のrequired XHTML failureをnative p196 outputで修復し、元10対照すべてを再実行して通過。late500ms色を先取りせずbudget32赤、TestRendered解除64緑、delay100解除132緑。永久wait/zero-loopを非PASSとし、nonce/URL/readyState/除去済みprobeを保持。native ACK=0のままJSONだけcompleteでも非PASSにする負例は、実際に遅い緑画像を得てもcapture_boundary_invalidになる。native画像を使ったmatch/mismatchとfuzzy/graphの純粋対照を含め22/22。

[全22 log](runner-controls-final.log)、[元10＋境界11 log](controls.log)、[native/PIL controls全raw](controls-raw.tar.gz)を保存しtar memberを元bytesとread-back照合。C client/SDKはq18と同じbcfcf31f…、new libbrowser aa030e40…、[source proof](source-proof.json)。callback中のview再入は無し、最初のACK成功call後に描画する。Pixel比較では全画面寸法とnative footerも必須とし、JSONだけ/blank imageをPASSにしない。

q19-i02 scoped partial cleared。q18未達を保持し、新attemptのverified outputでscripted413へ進んだ。whole p190/WS/target full/p100/shared projectionは未達。
