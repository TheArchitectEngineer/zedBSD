<!-- awesome-plan project=zedbsd record=queue-q506 -->

# Queue q506: 複数の公開siteの画像・DOM比較とlayout改善

Status: finished（2026-09-30）
承認: ユーザー「Wikipedia、Hacker News、danluu.com、GitHubのリポジトリページ、Reddit、The Verge / Medium、Yahoo! JAPAN、阿部寛のホームページ、CraigslistでChromiumと比較してレイアウト改善。画像だけでなくDOMツリーも比較。最初に阿部寛のホームページのwindow枠を含むfull-screen screenshotをデモ用に作る。」。実行中に、現実的なwindow size、GitHubのJavaScript、WPT reftest、Acid2・Acid3を追加する指示。

- q506-i01 / [ws074-p097](../ws074/phase097/phase.md): cleared。阿部寛とAmazonのguest full-screenデモを作り、9公開siteを固定Chrome User-Agentと2 viewportで画像・box・DOM比較した。MediumはHTTP 403のchallengeを記録して除外した。
- intrinsic widthのauto margin、HTML presentational hint、tableのrowspanとvertical-align、同一contextの非同期`postMessage`と`MessageEvent`を一般化して修正した。
- GitHubの実行対象script 10本は全てES moduleで、現状はskipされると特定した。module graphと`import`・`export`の実行はws074-p098候補。
- 固定WPT CSS2 reftestは44 pass / 56 fail / 0 error。履歴的診断はAcid2 pixel agreement 90.56%（exact fail）、Acid3 9/100・40.35%だった。
- plain・ASanのhost build、DOM 22/22、position 22 checks、golden 16/16、Python compile、style-check、diff-checkを通した。aggregate smokeはユーザー指示により実施していない。GitHubへは未公開。
