<!-- awesome-plan project=zedbsd record=ws074-p178 -->
# ws074-p178: B1 の試行 — Acid3 の 1 領域を pixel で一致させる

Status: uncleared（2026-10-13 q1000-i01。差が画面全体に及び、1領域を選ぶ停止条件に該当）
Disposition: normal
Parent: [WS074](../ws.md)
Owner: この Codex セッション（今回の試行）。B1 の Sonnet 5.5 Mid のモデル評価は別途未実施。
Queue: [q1000-i01](../queue.md)
Related: [p100](../phase100/phase.md)（Acid3 の 100/100 と pixel の完全一致。この Phase はその一部の試行）

## 由来

ユーザー（2026-10-05）「WS074を担当する専用のサブエージェントを設計します。Sonnet 5.5 Midで実行することにします。B1という名前にします。まず少し作業を進めてもらって、Acid3の1項目をピクセル一致させられるかを見てみたいです。このモデル選択でその作業ができるか、作業設計をしたいという意味です。どんな作業をするのか、あなたがphase.mdに詳細な手順やコマンドを記載する必要があると考えます。」

## 目的

1. Acid3 の画面のうち **1 つの領域**（下の手順 3 で選ぶ、1 つの色の箱か得点の文字など）を、参照と pixel で一致させる。
2. 同時に、**Sonnet 5.5（effort medium）の B1 がこの種の作業をこなせるか**を確かめる。かかった時間・つまずいた所・Q1 の助けが要った所を記録する（下の「試行の記録」）。

全体の pixel の完全一致（p100）は目的にしない。1 領域で止める。

## 前提の事実（2026-10-05 Q1 が確かめた）

- 得点は 100/100、全体の pixel の一致は 37.04%（302208/480000 の画素が違う、800×600）。q579 の Codex の計測（[q596 の README](../phase172/browser3/q596/README.md)）。今の main で測り直してはいない。
- runner: `plan/ws074/tests/run-acid-tests.py`。WPT の固定の commit `2d66b9b7998bb58c336138c178323ddee857b586` の `acid/acid3/test.html` を host の browser（headless）で 800×600 に描き、**同じ browser で** `acid/acid3/reference.sub.html`（`{{host}}` を `invalid.test` に置き換えた写し）を描いた画像と比べる。比べ方は RGB の画素ごとの完全一致（`image_result`）。
- 参照の画像も自分の browser の描画なので、違いは「test.html の描画」と「reference の描画」の差（layout・CSS・文字・色の違い）。
- 字形: 現行 runner の `FONT_NAMES` は `userland/desktop/fonts/` の Mahora Regular・JetBrains Mono・Droid Sans Fallback。待ち時間は 15 秒（`SETTLE_MS`）。2026-10-06 に Inter は削除された。

## 2026-10-13 オンボーディングと実行前の整合

ユーザーは「WS074に取り組みます」および本 Phase の選択を指示した。これは 2026-10-05 の描画改善の停止を **p178 に限って**解除する指示として扱う。他の描画 Phase の再開には広げない。Q1 が管理する共有 Queue には本 Phase の項目がまだ無く、この記録だけで実装 attempt を開始しない。Q1 は担当・時間枠・この Phase の正確な範囲を Queue に記録し、開始前にその承認と依存を確認する。`master.md` はこのセッションで変更しない。

現在の checkout（2026-10-13、`main`）では `build/ws074-suites/wpt` が無く、`build/ws074-host/plain/browser` は存在し、B1 worktree は無い。存在する binary は現行 source との一致を未確認なので、baseline の根拠に使わない。

担当の削除禁止規則と、下記の既存コマンドには不整合がある。`fetch-suites.sh` は取得先を作り直す分岐で `rm -rf`、`run-acid-tests.py` は PPM と WPT 内の一時 HTML に `unlink()` を使う。B1 を含む担当はこれらをそのまま実行しない。Q1 が削除を含む処理を自分の pipeline で行うか、担当が使える削除なしの手順・runner を承認された Phase 範囲で用意してから、手順 1・2・5 を進める。失敗時に別経路で安全判定を回避しない。

手順 2b の `chrome-fonts.sh` はファイル存在確認を Mahora に変えているが、fontconfig の alias とコメントはなお Inter を指定している。Chrome 側の比較前に Q1 と担当が Mahora の実際の選択を確認し、揃わない画像は診断用として扱い、領域選択の一致根拠にしない。`chrome-shot.sh` は `file://` で撮るため、HTTP で動く WPT の参照と資産の取得条件も照合する。

本 Phase の既定担当 B1 は Sonnet 5.5 Mid の評価も目的に含む。このセッションの担当が B1 でない場合、描画試行の結果を B1 のモデル評価と記録しない。Q1 が担当を確定するときに、この評価条件をどう扱うか記録する。

### Q1 への最初の Queue 選定案

- 根拠: 2026-10-13 ユーザーが p178 を最初の対象に選び、続いて Acid3 の全100項目・pixel 完全一致を最終目標に指定。
- 有限の範囲: 最大3時間の1 attempt。固定 WPT・現行 source・font・host binary の照合と baseline、test/reference/Chrome の比較条件の検証、差の候補を最大3領域まで分類し、1領域を選んで原因が局所的なら修正する。影響の広い設計変更は対象外で、証拠と次の有限単位を残す。
- 先行条件: p099/p172 の verified 出力、担当用 worktree、削除禁止に合う suite 取得・Acid runner、Chrome の font/HTTP 条件。これらが満たせない場合は実装せず、原因と再開条件を記録する。
- 成果判定: 実際の score・RGB 差画素数・Acid2・例外/timeout、選択領域の前後の画像、変更 file・build/短い host 回帰と全文規約の結果を保存する。1領域成功なら局所成果を cleared、未達なら attempt を uncleared とし、p100 全体は差0になるまで未達のまま。
- 共有 Queue の ID と開始時刻・担当・正確な承認文言は Q1 が記入する。この案と本 Phase の更新は Queue を active にしない。

## 手順とコマンド

すべて B1 の worktree（`/home/awe/zedBSD-worktrees/b1`）の top で行う。各手順の出力の要点を phase.md の「記録」に書く。

### 1. 準備（約 10 分）

```sh
cd /home/awe/zedBSD-worktrees/b1
git status --short                      # 空であること
sh plan/ws074/tests/fetch-suites.sh wpt # build/ws074-suites/wpt を固定の commit で取る（network）
sh plan/ws074/tests/host-build.sh plain # build/ws074-host/plain/browser を作る（host の cc、warning を見る）
python3 -c 'import PIL; print(PIL.__version__)'   # Pillow が要る
```

止まる条件: fetch か build が失敗したら、出力の最後の 30 行を記録して Q1 に返す（自分で build の仕組みを変えない）。

### 2. 今の baseline を測る（約 5 分）

```sh
python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-base
cat build/ws074-acid-base/report.json | python3 -m json.tool | head -60
```

記録すること: Acid3 の score、`different_pixels`、`agreement`、`exceptions` の数。`build/ws074-acid-base/acid3-side.png`（左 test・中 reference・右 差を 4 倍に明るくした物）の path。
q579 の値（score 100/100、37.04%）と大きく違えば、その旨を記録する（今の main の状態が基準）。

### 2b. 参照の描画を Chrome と比べる（2026-10-05 ユーザーの指示で追加、約 15 分）

自前の browser で描いた参照（`acid3-reference.png`）が正しいかを、Chrome（headless）の描画と比べる。違いを「参照の側（単純な CSS の誤り）」と「test の側（難しい機能の誤り）」に分けるため。

```sh
ls plan/ws074/tests/chrome-shot.sh plan/ws074/tests/chrome-fonts.sh   # 既存の道具、使い方は各 script の先頭の注記
# Chrome で reference を 800x600・同じ字形（userland/desktop/fonts/）で撮る（runner が作る reference の一時の写しと同じ HTML。`{{host}}` は invalid.test）
python3 plan/ws074/tests/acid3-regions.py build/ws074-acid-base/acid3-reference.png build/ws074-acid-base/chrome-reference.png > build/ws074-acid-base/ref-vs-chrome.txt
```

- Chrome が無い・撮れない時は、その旨を記録して手順 3 に進む（止めない）。
- 字形の anti-alias の違いだけの小さな差は、参照の誤りに数えない（差の塊の大きさと形で判断し、記録する）。
- 手順 3 の領域の選び方に次を加える: **参照が Chrome と一致していて、test だけが違う領域**を選ぶ。参照の側に大きな差があれば、それは別の候補（単純な CSS の不具合）として記録する。

### 3. 差を領域に分けて、1 つを選ぶ（約 15 分）

差の画素を、画面の要素ごとに分ける。Acid3 の画面は、上の 6 つの色の箱（bucket）、得点の文字、その他の背景から成る。
次の小さな script を `plan/ws074/tests/acid3-regions.py` として書く（読むだけの解析、試験の入力は変えない）:

- 入力: `acid3-test.png` と `acid3-reference.png`。
- 違う画素の mask を作り、4 近傍の連結成分に分ける（Pillow と純 Python で十分。numpy が使えれば使ってよい）。
- 各成分の外接の矩形・画素の数・test と reference の代表の色を、画素の数の少ない順に表で出す。
- `--crop X Y W H` を付けると、その矩形を test・reference・差の 3 枚に切り出して 8 倍に拡大した PNG を出す。

```sh
python3 plan/ws074/tests/acid3-regions.py build/ws074-acid-base/acid3-test.png build/ws074-acid-base/acid3-reference.png > build/ws074-acid-base/regions.txt
python3 plan/ws074/tests/acid3-regions.py ... --crop X Y W H --out build/ws074-acid-base/crop
```

**選び方**: 1 つの画面の要素（1 つの bucket か得点の文字）に収まり、差の画素が少なく、原因が 1 つに見える物。候補を 3 つまで挙げて記録し、1 つを選ぶ。
選んだ領域の test と reference の DOM・style・layout・paint を比べる（runner と同じ引数で `--dump=style` 等。`run-acid-tests.py` の `dump_page` を参考に、test と reference の該当の要素の箱の位置・大きさ・色・字形を並べる）。

止まる条件: 1 つの要素に収まる差が無い（全部の差が画面全体に広がる、例えば全体の 1 px のずれ）なら、その事実と原因の見込みを記録して止める。

### 4. 原因を特定する（上限 60 分）

- 選んだ領域で、test と reference の描画が何で違うかを特定する: 箱の位置（layout）、色（style の計算・color の解析）、文字（字形・baseline・anti-alias）、描画の順・clip。
- 該当の engine の source を `userland/desktop/libbrowser/` の中で探す（`style/`・`layout/`・`paint/` など）。仕様（CSS 2.1・CSS3 の該当の節）と照らす。
- 原因が分かったら、直す前に「原因・直す file と関数・期待する差の変化」を phase.md に書く。

止まる条件: 60 分で原因が 1 つに絞れない、または直しが engine の広い作り直し（複数の module にまたがる、data の構造を変える）になる見込みなら、そこで止めて記録し Q1 に返す。

### 5. 直して確かめる（上限 60 分）

```sh
sh plan/ws074/tests/host-build.sh plain                        # warning 0
python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-fix
python3 plan/ws074/tests/acid3-regions.py build/ws074-acid-fix/acid3-test.png build/ws074-acid-fix/acid3-reference.png > build/ws074-acid-fix/regions.txt
```

- 合格: 選んだ領域の差の画素が 0、Acid3 の score が下がらない（100/100 のまま）、Acid2 が下がらない（runner の Acid2 の結果）、全体の `different_pixels` が増えない。
- 回帰: `sh plan/ws074/tests/host-build.sh asan` の後、変えた module の host の試験（`build/ws074-host/asan/host-NAME`、該当する物）を走らせる。
- style-check: 変えた C の hunk に新しい違反が無いこと（`plan/tools/` の style-check の使い方は `plan/coding-style.md` の注記）。
- 直しは最小に。参照の HTML・runner・比較の方法を変えて差を消すのは禁止。

止まる条件: score か Acid2 が下がる、他の領域の差が増える、3 回直しても選んだ領域が 0 にならない。

### 6. 記録と返却

- この phase.md に「記録」と「試行の記録」を書く。証拠の PNG: `build/ws074-acid-base/acid3-side.png`、`build/ws074-acid-fix/acid3-side.png`、選んだ領域の crop の前後。
- commit（`git commit -m WIP -- userland/desktop/libbrowser/... plan/ws074/...`）、SHA と要点を Q1 に送る。

## 受け入れ

- 選んだ 1 領域の差の画素が 0（host の headless、固定の WPT・字形・800×600）。
- Acid3 の score 100/100 のまま、Acid2 の結果が下がらない、全体の差の画素が増えない。
- build の warning 0、変えた module の host 試験 PASS、変えた hunk の style の違反 0。
- 「試行の記録」が書かれている（成功しなくても、記録が揃えば試行としては cleared にできる。その時は Disposition に「領域は未達」と書く）。

## 試行の記録（B1 が書く。Q1 がモデルの選択の判断に使う）

| 項目 | 記録 |
| --- | --- |
| 各手順にかかった時間 | |
| 自分で解けたこと | |
| つまずいた所・やり直した所 | |
| Q1 に聞いた・助けが要った所 | |
| phase.md の手順で足りなかった情報 | |
| 次に同じ種類の作業をするなら手順に足すべき事 | |

## 記録

### 2026-10-13 q1000-i01 / 試行終了、uncleared

Codex の独立 tree `/tmp/zedbsd-ws074`（開始時 source `7433ca18d`）で実行。固定 WPT `2d66b9b7998bb58c336138c178323ddee857b586` をライセンス確認付きで取得。削除を実行しないよう `fetch-suites.sh` と `run-acid-tests.py` の一時物の扱いを変更した。現行 libmedia の source 構成へ `host-build.sh` を追従させ、`sh plan/ws074/tests/host-build.sh plain` は warning 0、終了値 0。host browser SHA256 `271aa0b1443677687187ee30347748b57a72985e685eb6edba5b820b0740f963`。

`python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-base` と、Acid3 の style/layout/paint の dump を追加した同 runner の `--out build/ws074-acid-diagnostic` を各1回実行。両回とも Acid3 得点 100/100、inventory 100/100、例外 0、RGB 一致 37.0717%、差 **302056/480000画素**、pixel fail。Acid2 は 98.4458%、差 1865/120000画素で fail（歴史的な exact pass からの差。今回 engine の変更は無く、原因未確定）。[report](evidence/20261013-q1000/report.json)、[Acid3 並列画像](evidence/20261013-q1000/acid3-side.png)、[Acid2 並列画像](evidence/20261013-q1000/acid2-side.png)、同 directory の layout/paint dump が証拠。

固定 WPT の `acid/acid3/reference.png` には6色の箱が描かれている。しかし当 browser の reference 最終画面と paint dump にはその背景画像が現れず、test 側には大きな白い body と色の箱がある。この参照側の欠落だけで広い面積が不一致となる。背景画像の CSS 解析・URL 解決・取得・decode・canvas painting のどこが原因かは未確定。`chrome-shot.sh` は終了値 133 で画像を作れず、Chrome 対照は未実施。`chrome-fonts.sh` は Mahora に更新したが、fontconfig の generic sans-serif の選択は未解決。

手順3の停止条件「差が画面全体に広がり、1要素に収まる差がない」に該当するため、局所領域の選択・engine 修正・修正後の ASan/回帰は実施していない。p178 の1領域 pixel 一致は未達。B1/Sonnet のモデル評価はこの Codex 実行からは判定しない。

再開条件: 参照側の `reference.png` の読み込み/paint を独立した有限 scope で調査・修正し、固定条件で再計測してから、p178 の1領域選択へ戻る。Acid2 の差も別途原因を判定する。p100 全体の clearance は未達。

2026-10-13 / design-revision-after-q1000: 参照側の背景画像欠落を [p179](../phase179/phase.md) に分け、p178 の再試行をその verified 出力の後に置く。q1000 の全画面差により1領域選択が成立しなかったため。p179 は CSS/URL/取得/decode/paint を切り分け、一般的な欠陥の最小修正と同条件での再計測を受け入れる。p178 の局所 pixel 一致、Acid2 回帰、p100 の全画面差0を免除しない。p179 の新 Queue は未選定。

2026-10-13 / q1001-p179-output: [p179](../phase179/phase.md) は cleared。参照画像が canvas に描かれ、同条件の Acid3 差は302056から56647へ減った。q1000-i01 の uncleared は書き換えない。次の局所領域を差画像から選び、この Phase の新 attempt を exact scope/時間枠を定めた Queue に入れる必要がある。Acid2 の差1865/120000と p100 の全画面差0も残る。

2026-10-13 / next-attempt-proposal: 残る差のうち参照ページの得点「100/100」の位置を [次の有限試行案](next-queue-proposal.md) に選んだ。参照では `right:0` の絶対配置子が x=625 から右へ出ており、test の得点は x=231.80。一般的な絶対配置の auto 幅/right 計算を原因候補として最大90分の調査・最小修正・同条件再計測を提案する。これは計画のみで Queue 未選定・未承認。
