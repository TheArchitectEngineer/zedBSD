<!-- awesome-plan project=zedbsd record=queue-q504 -->

# Queue q504: Amazon検索欄の文字の位置

Status: finished（2026-09-30）
承認: ユーザー「検索ボックス内の文字が、縦方向の位置がおかしいです。修正できますか？」、続けて「同じ文字列の水平位置が、Chromiumだともっと右になっており、ここも修正できるかもしれないです。」

- q504-i01 / [ws074-p095](../ws074/phase095/phase.md): cleared。flex itemのcross axisで元の`box-sizing`を保ち、Amazonのinputを55pxから指定どおり38pxにした。
- form controlへ`text-indent`を通し、placeholder・値・caret・clickの文字原点を揃えた。検索文字の範囲はoursとChromiumの両方で`x=435..574, y=22..36`。
- plain・ASanのDOM 22/22、form 29 checks、position 22 checks、style-check、diff-checkを通した。最終比較は72.88%/ink 65.76%、Uncaught 4。GitHubへは未公開。
