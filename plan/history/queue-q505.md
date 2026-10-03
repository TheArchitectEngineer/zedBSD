<!-- awesome-plan project=zedbsd record=queue-q505 -->

# Queue q505: 公開サイトの固定比較corpusと一般化修正

Status: finished（2026-09-30）
承認: ユーザー「アクセス元のチェックが厳しくないサイトを見つけて、Chromiumとの比較により、改善を続けたいです。」、対象を`https://www.mozilla.org/ja/`とし、User-AgentはChromeにする指示。

- q505-i01 / [ws074-p096](../ws074/phase096/phase.md): cleared。Mozilla日本語topと直接linkされた3 pageを固定し、4 pageを同じChrome User-Agentで比較できるcorpusにした。
- custom-propertyの`@supports`とbutton内の空白のUA処理を修正。topは70.71%/ink 62.52%から78.23%/ink 69.54%へ改善し、全4 pageで退行なし・Uncaught 0。
- plain・ASanの対象golden、form 29/29、HTTP/TLS 15/15、style-check、diff-checkを通した。guest・boot・smokeはscope外。GitHubへは未公開。
