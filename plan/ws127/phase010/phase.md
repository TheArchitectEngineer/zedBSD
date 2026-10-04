<!-- awesome-plan project=zedbsd record=ws127-p010 -->

# ws127-p010: Files の directory の名前（breadcrumb）をタップ・クリックすると path を入力でき、約 1 秒後に path の候補の dropdown を出す

Status: in-progress（q705-i01、P2 generation14、2026-10-05。範囲 1・2・4 を実装・host 試験済み、QEMU（T1）待ち。範囲 3（IME）は BUG-177 の保留に従い未実施）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q705-i01（P2 generation14）

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

## 設計と結果（2026-10-05、q705-i01 P2 generation14）

- **範囲 4（どちら側か）**: title bar は compositor の server-side（WS070 の CONTROLS、`titlebar-shell.c`）で、path の欄（`focus_control(edit)`、Ctrl+L）は既に compositor の text field。それに合わせ、欄と候補の dropdown は compositor が描いて操作し、候補を作るのは Files（file system を読むのは app）。
- **範囲 1**: breadcrumb の最後の段（今の場所の名前）の click・tap を、Files が Ctrl+L と同じ `fm_input_location` にする（compositor の edit の focus は欄の全文を選んだ状態で始まる）。最後より前の段は今まで通り移動。Enter で移動・Esc で元に戻る・存在しない path は「No folder at …」・`~` の展開は既存の `fm_input_location_go`。候補を選んだ path の末尾の `/` は移動の前に落とす。
- **範囲 2（候補）**: 欄の text が変わってから 1 秒（`INPUT_SUGGEST_MS`、打つたびに延びる）で、Files が path の最後の `/` までを folder（先頭の `~` は home）、残りを名前の頭として、その folder の **folder だけ**（link の先が folder の物を含む。file は移動先にならないので含めない）を ASCII の大小を区別せずに前方一致で探す。隠し folder は頭が `.` か「隠しファイルを表示」の時だけ。名前の順に最大 12（`FM_SUGGESTIONS`）、見る entry は最大 4096（`INPUT_SUGGEST_SCAN`、数千の項目でも window が止まらない）。label は「名前/」、欄に入る text は「入力した folder＋名前/」（`~` を保つ）で、選ぶと続けて下の階層を入力できる（1 秒後にその中の候補）。
- **protocol**: `keiland_titlebar_v1` に request 16 `set_suggestions(uint id, array suggestions)`（version 4、transaction の外、label と text の組を NUL で区切る、最大 12、search と breadcrumb の欄だけ）。compositor は欄が keyboard を持つ間だけ欄の下に一覧を出し（menu の popup の後、全ての上）、↑↓で選び、Enter か click・tap でその text を欄に入れて `text_changed` を送る（欄は keyboard を保つ）。一覧は欄の text が変わるか編集が終わると消え、Esc はまず一覧を消し、次の Esc で編集を終える。libkeiland `kl_titlebar_set_suggestions`（KL_VERSION 25、旧い compositor には ENOTSUP）、libwayland の `keiland_titlebar_v1_set_suggestions`、manager の global の version 3→4。
- **範囲 3（IME）**: 欄は compositor の text field で、IME（text-input）は今の欄にも無い。[BUG-177](../../bugs/BUG-177.md)（検索欄の IME）が保留（ユーザーの方針: 既存 Bug は朝まで立ち上げない）なので、同じ直しで行う形で未実施。
- **範囲 3（IME）の追記（2026-10-05 P1、BUG-177 の修正）**: compositor の IME が compositor 自身の title bar の欄（検索欄と path の欄で共通の `shell_field`）を serve するようにした（[BUG-177](../../bugs/BUG-177.md) の「原因と修正」）。path の欄にも同じく効く。QEMU は `plan/ws127/tests/bug177-guest.sh`（検索欄で確かめる）、T1 待ち。
- **確認**: zedBSD の `bin/files`・`bin/wayland`・`libkeiland.so` warning 0、Linux warning 0、FreeBSD は方針で不要、OS の境界の checker PASS。host: `plan/tools/files/host-p014.sh` に case 8（最後の段の click で focus=4、1 秒前は候補なし・後に Pictures/・Projects/、`~/D` で `~/Desktop/` 等、無い folder で 0）、PASS。host-p009・host-p013 PASS。files-render に `suggestions` の命令を足した。
- **試験の依頼（T1、Q1 経由）**: `plan/ws127/tests/files-p010.sh`（files の image、最後の段の click → 欄、`/tmp/fhome/P` で 2 件の候補と dropdown の PNG、↓Enter で Pictures/、click で 2 行目の Documents/、Esc の 2 段）。
- **Q1 に依頼**: WS070 の `plan/ws070/titlebar-design.md` の request の表に 16 `set_suggestions`（version 4）と §9 の keyboard（↑↓・Enter・Esc の順）を足す（他の WS の文書）。
