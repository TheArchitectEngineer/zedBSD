<!-- awesome-plan project=zedbsd record=ws074-p096 -->

# ws074-p096: 公開サイトの固定比較corpusと一般化修正

Status: in-progress
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q505-i01

## 目的

Mozilla日本語サイトを入口に、そこからたどれる同一originのpageを固定captureし、同じ入力をbrowserとChromiumで描画する比較corpusをAmazon以外にも広げる。複数のpageで確認できるlayout・CSS・描画の差を優先して直す。

## 受け入れ条件

- host Chromiumと同じ固定Chrome User-Agentを取得とbrowserの比較条件に使い、HTTP status・最終URL・challengeらしい応答の有無をmanifestへ記録する。
- `https://www.mozilla.org/ja/`を入口に、topとそこからたどれる主要な同一originの日本語pageを少なくとも3 page、scriptを除いた自己完結の固定入力として保存できる汎用capture toolを作る。取得物と比較結果は`build/`だけに置く。
- 固定入力を同じviewport・font・時刻・locale・User-AgentでbrowserとChromiumに描かせ、pageごとの画素一致率・ink一致率・実行時の例外をJSONへ記録する。
- 差を画像とboxから調べ、複数siteまたは標準のfixtureへ一般化できる原因を、このQueueでは最大2件まで修正する。site固有の座標補正は入れない。
- 変更した機能のtargeted regressionをplain・ASanで通し、style checkと`git diff --check`を通す。guest・boot・smokeは、このhost比較だけのPhaseでは行わない。

## 前提・規約

- 比較器の土台はp094、Amazonでの動的scriptと固定captureはp092・p094、formとflexの直近の修正はp095でcleared。
- [Guardrail](../../guardrail.md)と[コーディング規約](../../coding-style.md)を適用する。HAL、公開ABI、vendor sourceは変更しない。
- Mozillaへは各pageとassetを1回ずつ取得し、短時間の繰り返し取得を避ける。captureのsourceとassetはcommitしない。
- ユーザーの指示どおりsmokeは行わない。

## 初期取得（2026-09-30）

固定Chrome User-AgentでMozilla日本語topはHTTP 200、最終URLは`https://www.mozilla.org/ja/`。展開後のHTMLは119,628 byteで、stylesheet 6、image 22、script 11、同一originの日本語link 17を含む。最初はtopから`/ja/products/`、`/ja/about/`、`/ja/about/manifesto/`へたどる。比較用の固定版ではscriptを除くが、linkの行先はmanifestに残す。
