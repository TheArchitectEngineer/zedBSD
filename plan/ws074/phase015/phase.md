<!-- awesome-plan project=zedbsd record=ws074p015 -->

# ws074-p015: URL（WHATWG）、data: URL、WPT の URL の試験の runner

Phase ID: `ws074-p015`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p002（cleared）

## 範囲（正常系のワンパス）

- `net/`（新、`net/net.h`）:
  - `url.c`: WHATWG URL Standard の基本の URL parser（state override の無いもの。URL の setter は後）、serializer、URL の
    interface の部分（href・origin・protocol・username・password・host・hostname・port・pathname・search・hash）、percent-encode の
    集合、file: URL と path の変換、percent-decode。入力は UTF-8 の byte で走る（非 ASCII の byte はどの集合でも encode される）。
  - `host.c`: host の parser（IPv6 と圧縮した serialize、IPv4 の 1〜4 部分と 8・16 進、opaque host、domain）。domain to ASCII は
    縮めた UTS #46（ASCII の小文字化、全角・互換の空白・数学の文字・句点の写像、無視する文字、非文字の拒否、Unicode の小文字化、
    punycode の encode と `xn--` の検査）。
  - `data.c`: Fetch Standard の data: URL の処理（MIME type の parse と serialize（MIME Sniffing）、forgiving-base64）。
- page: link の解決（`page_resolve_file`）を URL の parser に置き換えた。`page_fetch`（新）: data: と file: の URL の中身
  （`<script src>` が使う）。
- 試験: `plan/ws074/tests/host-url.c`（batch の driver）、`run-url-tests.py`（WPT の `url/resources/urltestdata.json` と
  `fetch/data-urls/resources/data-urls.json`）。`fetch-suites.sh` の WPT の sparse に `/fetch/data-urls/` を足した（固定の SHA は
  同じ。既にある suite にも sparse の path を当て直す）。

## 受け入れ

1. amd64 の build（warning 0）、新しい file の style-check 0。
2. urltestdata の通過率 ≥ 85%（M1、design.md §15）。data: URL の試験。host plain・ASan、guest。
3. link の試験（host-link）、DOM の試験、窓の link の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- **urltestdata 896/896（100.0%）、data-urls 72/72（100.0%）**（host plain・ASan、guest の zedBSD でも同じ）。
  `plan/ws074/results/url.txt` に記録。M1 の URL ≥ 85% を越えた。
- `dom/urls.html`（新）: data: の script（percent-encode と base64）、query と fragment 付きの src、`./sub/../` の src。DOM の試験
  6/6 が Chromium 153 と同じ（host plain・ASan、guest）。
- host-link 22/22（link の解決が URL の parser に替わっても同じ）。guest の窓: browser-p045（link・履歴・場所の欄）・p030 status 0。
- style-check: `net/*.c`・`net/net.h`・`page/link.c`・`page/script.c` 0。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p015-20260928-boot-login.png`）。実機: 未実施。
- commit: `cc9298b7`（code と試験）、この記録の commit。

## 後回し（follow-up）

- UTS #46 の全体（写像の表、NFC、disallowed、bidi と joiner の規則、CheckHyphens 等）。WPT の `toascii.json`・`IdnaTestV2.json`
  はまだ測っていない。
- URL の setter（state override）と JS の `URL`・`URLSearchParams`（p032 の binding と一緒に）、`application/x-www-form-urlencoded`。
- 窓の場所の欄に data: や http の URL を入れる（今は file: の path だけを開く）。
