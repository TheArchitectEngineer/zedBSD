<!-- awesome-plan project=zedbsd record=ws089-p017 -->

# ws089-p017: accent の色・dark の外観（D2）

Status: planning（ユーザーの判断: ベータ1 に入れるか）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q741（Q1、2026-10-05、P2）
依存: p010。zdesktop と各 app の固定の色を preferences から読む変更（他の WS の source）
目安: 4h 以上（複数 Phase の見込み）（1 Queue）。実行者の目安: phase-runner
所有 path: Q1 が決める（settings・compositor・libkeiui の theme・各 app）

## 範囲

Appearance の accent の色の選択と dark の外観。zdesktop・libkeiui の theme・Files・Settings ほかが preferences の色を読む。

## 受け入れ

（採用されたら分割して決める）

## 検証の方法と範囲

QEMU の Venus。 やっていない確認は「未実施」と書く。

## 未決の判断

ベータ1 に入れるか（ユーザー、計画エージェントの案: 入れない）。

## Event

2026-10-02 / ws089-beta1-plan: fg019 の計画で新設。

## 設計（2026-10-05、P2、q741）

- 今の accent は各所に固定の色（`FM_RGB(0x2f7cf6)` など）で 15 file に散っている（compositor の `home.c`・`icons.c`、Files 9 file、Settings、Notes、libkeiland の `ui/theme.c`・`ui/widgets.c`）。dark の外観は無い（白い glass と slate の文字が前提）。
- 案:
  1. settings の key `appearance.accent`（色の名前の列挙: blue・purple・pink・red・orange・green・graphite、既定 blue）と `appearance.dark`（bool、既定 off、後で「自動」）。compositor が持つ（Guardrail の「app と設定」）。
  2. libkeiland に theme の口（`kl_theme_accent()`・`kl_theme_color(KL_THEME_TEXT)` など）を置き、`kl_settings_watch` で変わったら app に描き直しを求める（WS158 の言語の通知と同じ形）。
  3. 段: (a) accent だけ（固定の色を口に置き換える。15 file）、(b) dark（glass の色・文字・線・影の組を 2 つ持つ。全 app の描画に及ぶ大きな変更）。
  4. Appearance の頁に accent の色の丸 7 つと Dark の switch。
- **判断が要る点**（ユーザー）: (1) ベータ2 に入れるか（計画エージェントの案は入れない、2026-10-02）、(2) 入れるなら accent だけか dark もか、(3) 各 app の source に及ぶので担当の分け方（Q1）。
- 判断が出るまで実装しない。
