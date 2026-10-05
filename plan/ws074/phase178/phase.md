<!-- awesome-plan project=zedbsd record=ws074-p178 -->
# ws074-p178: B1 の試行 — Acid3 の 1 領域を pixel で一致させる

Status: planned（2026-10-05 Q1。B1 の起動はユーザーの指示の後）
Disposition: normal
Parent: [WS074](../ws.md)
Owner: B1（`.claude/agents/browser-runner-b1.md`、Sonnet 5.5、effort medium）
Queue: なし（起動の時に Q1 が振る）
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
- 字形: `userland/desktop/fonts/` の Inter・JetBrains Mono・Droid Sans Fallback。待ち時間は 15 秒（`SETTLE_MS`）。

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

（B1 が書く）
