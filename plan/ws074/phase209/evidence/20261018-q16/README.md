# q16 / producer implemented, required attribute control uncleared

Native browser_view_settleはfont/layout/checkpoint後のeligible taskを同じ絶対budgetへ戻し再layoutするbounded loopへ変更。no-font path/初期font resolution/public ABIを維持し、page消失をguard、32 pass超過はETIMEDOUT。source管理ID commentも除去した。

font-ready→2RAF/DOM mutationの新fixtureは旧5行→全7行。budget0/16/32/200、継続RAF2回で停止、zero-delay無限再予約のETIMEDOUTを6条件で確認。旧q1065 font/no-face両5/5、DOM34/34、CSSOM各12/12。host/target view.c warning0、生成関数C全文/format19・style候補0、full file既存2件の追加0。Acid3 score/inventory100・差0、Acid2差873維持。Chromium CLIは新fixture先頭6行一致、二段目RAFはheadless dump終了前に未観測のため全7行Chromium一致とはしない。

strict controlsは14/15（parser8/8、native6/7）。残るTestRenderedはEvent自体/ class解除は通るがMutationObserverのattributes recordが通知されず停止。observer optionsはattributesをvalidationにだけ使用しregistrationはchild_list/subtreeのみ。別のnative prerequisiteであり、runnerのscript/待機を改変して通さない。

q16-i01はrequired全15とglobal gate未達でuncleared、i02は前提不足でfixed48未着手uncleared。native candidate pairを保存。全1491/static1561比較はこの既知依存を直した同じ最終sourceで行うため今回skipped。last full verified count1330/3805はq14 source88713baddの値であり、現在のcandidate全体の維持を未検証のまま主張しない。p253 attributesを別Phase/新Queueへ設計し、p209再検証→p190 strict48の順に再開する。p100/target全体/共有projection/Tools登録はpending、push無し。
