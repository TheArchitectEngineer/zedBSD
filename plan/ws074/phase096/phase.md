<!-- awesome-plan project=zedbsd record=ws074-p096 -->

# ws074-p096: 公開サイトの固定比較corpusと一般化修正

Status: cleared
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


## 結果（2026-09-30）

- 取得・Chromium・browserのHTTP送信・`navigator.userAgent`を、Chrome for Testing 153と同じ固定User-Agentに揃えた。比較器はcapture manifestのUser-Agentが違うと停止する。
- 汎用の`site-capture.py`を追加した。Mozilla日本語top、Products、Mozillaについて、Mozilla Manifestoを取得し、HTML・stylesheet・image・CSSの`url()`を`build/ws074-mozilla/`へ固定した。4 pageはHTTP 200、要求URLからredirectなし、challenge signalなし、asset 78件・取得失敗0。再実行は全pageをcacheから読み、manifestのSHA-256が一致した。
- `@supports (--css: variables)`をcustom propertyの有効なfeature queryとして扱うようにした。MozillaのCSS variableを使うruleが有効になり、4 pageすべての指標が改善した。
- UA stylesheetの`button { white-space: pre; }`をやめた。整形用の改行・字下げがbutton内の匿名boxになって高さを増やしていた。専用fixtureはChromiumと4/4 boxが1 px以内で一致した。

| 固定page | 修正前 pixel / ink | 最終 pixel / ink | Uncaught |
| --- | ---: | ---: | ---: |
| Mozilla top | 70.71% / 62.52% | 78.23% / 69.54% | 0 |
| Products | 93.19% / 16.46% | 94.83% / 31.35% | 0 |
| Mozillaについて | 43.98% / 19.74% | 48.53% / 25.65% | 0 |
| Mozilla Manifesto | 86.49% / 4.09% | 89.76% / 12.90% | 0 |

比較reportは`build/ws074-mozilla-compare-final/report.json`、画像は同じdirectoryにある。取得物と比較結果はcommitしていない。

## 回帰

- host buildはplain・ASanともwarning 0。
- `values`のcustom-property `@supports`と`button-whitespace`のlayout goldenをplain・ASanで一致させた。formは両方29/29。
- HTTP/TLSはplain・ASanとも15/15で、送信User-Agentの完全一致を含む。
- Pythonの変更fileは`py_compile`を通した。変更したC/headerの`style-check.py`は0件、`git diff --check`は0件。
- scopeどおりguest・boot・smokeは実施していない。GitHubへは未公開。

## 残りの候補

Mozillaの固定assetにはWOFF2が8件あるがbrowserはまだWOFF2を読めず、文字の形とmetricの差が大きい。topの緑のflagはinline SVG不足で欠ける。どちらも既存の一般Phase候補として次のQueueに分ける。
