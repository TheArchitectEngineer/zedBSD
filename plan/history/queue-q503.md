<!-- awesome-plan project=zedbsd record=queue-q503 -->

# Queue q503: Amazonの後続scriptが使うWeb API

Status: finished（2026-09-30）
承認: ユーザー「Amazon.co.jpのトップページがうまくレンダリングできるようになるまで、自走をお願いします。」

- q503-i01 / [ws074-p092](../ws074/phase092/phase.md): cleared。`atob`・`btoa`、observer、`elementsFromPoint`、非同期`fetch`、最小のXHR、MutationObserver、有界なheadless settleを実装した。
- sign-in tooltipのpercentage子の幅と、明示的なz-indexの親から子が抜けるstacking orderを修正した。白いtooltipと黄色いbuttonはAccount & Listsの直下で前面に表示される。
- DOM 22/22、JS 14/14、HTTP 18/18、position 22 checks。ASan・UBSanのDOM・HTTP・position、style-check、diff-checkを通した。
- dynamic topは約32秒、71.81%/ink 64.58%、Uncaught 4。searchは約65秒、73.79%/ink 28.78%、Uncaught 5。残りは次のQueueの候補。GitHubへは未公開。
