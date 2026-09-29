<!-- awesome-plan project=zedbsd record=ws074-p094 -->

# ws074-p094: Chromiumとの再現可能な比較手順

Status: cleared（2026-09-30）  
Disposition: normal  
Parent: [WS074](../ws.md)  
Queue: q500-i01

## 目的と範囲

Amazon.co.jp のトップと検索を同じ入力・font・viewportで browser と Chromiumに描かせ、画素差を繰り返し測れる1コマンドの手順にする。比較用の取得物と画像は `build/` のみに置く。

## 完了条件

- scriptを除いた固定captureを既定の回帰入力にし、live・script有効版と分ける。
- browserとChromiumの版、入力・font・実行file・出力のSHA-256、viewport、指標をJSONに残す。
- 前回のJSONをbaselineにして、画素一致率と非白画素一致率の低下を検出できる。
- 初回の取得・host buildを含む手順と、ネットワークを使わない再実行が通る。

## 実装

- `chromium-regression.py`: `--prepare`でhost版browser・capture・fontconfigを準備し、既定で`top-local-noscript.html`と`search-local-noscript.html`を比較する。Chromiumは毎回隔離profile、device scale 1、1280x900、UTC、外部hostを名前解決しない条件で実行する。`--baseline`の既定許容は0.25 percentage point。
- `amazon-capture.py`: `<picture><source>`、引用符が単一の`src`、iframe、inline styleを固定入力向けに処理し、全入力とassetのhashを`capture-manifest.json`へ書く。
- `chrome-fonts.sh`: 生成済みfont artifactが無いcheckoutでも、tree内の同じ3書体を使う。

## 検証と結果

環境: Debian 13 (WSL2)、Chrome Headless Shell 153.0.8010.12、Pillow 12.3.0、viewport 1280x900。

```text
python3 -m py_compile plan/ws074/tests/amazon-capture.py plan/ws074/tests/chromium-regression.py
sh -n plan/ws074/tests/chrome-fonts.sh
python3 plan/ws074/tests/chromium-regression.py --prepare --chromium CHROMIUM
python3 plan/ws074/tests/chromium-regression.py --chromium CHROMIUM --baseline build/ws074-compare/baseline.json
```

- host build: warning 0、browserとhost試験programを生成。
- 固定capture: top 966,301 bytes、search 1,407,952 bytes、asset 232件。topの描画resourceは外部URL 0。
- 初期値: top 画素 82.96%、ink 79.01%、search 75.85%、ink 32.97%。script error 0。
- 同じreportをbaselineにした再実行: regression 0、status passed。

比較器だけの変更なのでguest・bootは未実施。次はtopの2番目のcardで高さが約200 pxに縮む差をws074-p089で直す。
