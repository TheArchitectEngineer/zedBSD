<!-- awesome-plan project=zedbsd record=ws089-p017 -->

# ws089-p017: accent の色・dark の外観（D2）

Status: in-progress（p017a、2026-10-05 夕 P2 g15、q766）
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

## ユーザーの決定（2026-10-05 夕、Q1 経由）

- 「p017はベータ2に入れます。ただし、従来の調整を壊さないようなデフォルト値で開始できるようにします。」
- 「ライトモードとダークモードだけでいいです。アクセントカラーの変更は不要です。」→ accent の選択は作らない。`appearance.dark`（既定 off = 今の見た目）だけ。
- 「アプリにテーマ変更による再描画を通知するためのWayland拡張がほしいですね。libkeilandを通じて利用します。」→ compositor が theme の変化を伝える内部の拡張、libkeiland に取得と callback の口。app は settings を直接見張らない。
- 段の組み直し: p017a（key・拡張・口・Settings の切り替え、libkeiland ui・compositor・Settings・Files の dark）→ p017b（残りの app の dark）。

## 設計（p017a、2026-10-05 夕）

- **設定**: `appearance.dark`（compositor の key、bool、既定 0、KEPT）。書くのは Settings（kl_settings_set）、持つのは compositor。
- **拡張 `keiland_theme_v1`**（別の global、version 1。system 拡張に入れない理由: system 拡張は compositor の利用者の client にだけ見せる口で、theme は全ての client が知ってよく、軽い一つの値なので独立の global が単純）:
  - request 0 `destroy`
  - event 0 `appearance(uint mode)`: 0 light、1 dark。bind の時と、変わるたびに全ての object へ。未知の値は light として扱う（後の版で値を足せる）。
- **libkeiland**（KL_VERSION 34）: `kl_appearance_open(display, changed, data)`・`kl_appearance_get`・`kl_appearance_close`（`KL_APPEARANCE_LIGHT`・`_DARK`）。callback は display の default queue の dispatch の中で呼ばれる。process の今の appearance を libkeiland が覚え、`kl_theme_default()` は light か dark の theme を返す（app が持つ pointer は同じ物で、中身が替わる）。`kl_app` は開く時に自分で appearance を見張り、変わったら `KL_APP_THEME` の event を積む（app はそれで描き直す）。
- **compositor**: `server->dark`（`appearance.dark` から）。変わったら全ての `keiland_theme_v1` に event。描画は `glass_shape_draw` の一か所で、dark の時に色を写す: 灰色に近い（彩度の低い）色は明るさを反転（白い glass の地は暗い glass に、暗い文字は明るい文字に）、色の付いた色（accent・app の印の色）はそのまま、影と画像（窓の中身・壁紙・app の絵）は変えない。白い glass を持ち上げる shader の処理（`GLASS_LEAST_LUMA`）は白の地にだけ効くので、暗い glass には効かない。
- **Settings**: Appearance の頁に「Light・Dark」の切り替え（`appearance.dark` を set）。自分の色（`SE_COLOR_*`）は light と dark の 2 組の表を持ち、`kl_appearance` の callback で替えて描き直す。
- **Files**: `FM_COLOR_*` を同じく 2 組の表に、`kl_appearance` の callback で替える。
- **既定で見た目は変わらない**: 既定 off、light の表は今の値そのまま。
