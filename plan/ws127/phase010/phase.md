<!-- awesome-plan project=zedbsd record=ws127-p010 -->

# ws127-p010: Files の directory の名前（breadcrumb）をタップ・クリックすると path を入力でき、約 1 秒後に path の候補の dropdown を出す

Status: planning
Disposition: normal
Parent: [WS127](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、原文）

「Filesアプリで、ディレクトリ名をタップ・クリックすると、パスを入力できるようにしてほしいです。また、文字入力から1秒後くらいにタイマーで、パスの候補をドロップダウンで表示して選択可能にしてほしいです。」

## 範囲

1. title bar の directory の名前（breadcrumb、`ZWL_CONTROL_BREADCRUMB`）をタップ・クリックすると、path の入力欄（今の path を全部選んだ状態）に変わる。Enter で移動、Esc か focus を失うと元の breadcrumb に戻る。存在しない path・権限の無い path の表示。`~` の展開。
2. **path の候補**: 文字を入力してから約 1 秒（timer、入力のたびに延ばす）で、入力中の path の親の directory の中から、入力した前方と合う directory（と file を含めるかは設計で決める）の候補を dropdown で出す。上下の key・タップ・クリックで選べる。選ぶと欄に補い、directory なら続けて入力できる。
3. 入力欄では IME が使える（[BUG-177](../../bugs/BUG-177.md) の Files の検索欄の IME と同じ text-input の扱い、[ws095-p016](../../ws095/phase016/phase.md) の app ごとの IME の状態）。
4. title bar の描画と入力は compositor の server-side の title bar（zwl の titlebar の control）か Files の側かを、今の breadcrumb の実装に合わせて設計で決める。タップの扱いは [BUG-190](../../bugs/BUG-190.md)（タップダウンを押下に）と揃える。

## 受け入れ（案）

- breadcrumb のタップ・クリックで path の欄になり、入力した path へ移動できる。1 秒後に候補が出て、選べる。Esc で戻る。
- 大きな directory（数千の項目）でも候補の表示で UI が止まらない（候補の列挙は別の thread か件数の上限）。
- QEMU（T1、マウス・タッチ・キーボード）、実機の UAT。C の全文の規約、build warning 0。
