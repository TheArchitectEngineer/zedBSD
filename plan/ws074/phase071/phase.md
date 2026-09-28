<!-- awesome-plan project=zedbsd record=ws074p071 -->

# ws074-p071: 大きな page の速さ（1.4 MB の HTML、2 MB の CSS）

Phase ID: `ws074-p071`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p068、p061

## 範囲（amazon-goal.md §4 の 9）

大きな page（Amazon の検索: HTML 1.4 MB、外の CSS 約 2 MB、要素 約 6000）を guest で操作できる時間まで速くする。測って直す。

## 測定（直す前）

- guest（QEMU）の live の検索の page: Enter から 30 s で途中までの style（巨大な画像・style の無い filter）、90 s でようやく
  header・結果・filter が揃った（p035 の写真）。browser の process の CPU 時間は 150 s の間に 54 s。
- guest で engine だけ（保存した capture を `--render`、`/root/az` に複写）: トップ 約 1 s、検索 約 3 s。engine の 1 回は速く、
  遅さは資源（sheet 約 12、画像 約 230）が届くたびに style と layout を全部やり直すこと。
- host の `perf`（検索の `--render`、1.42 s）: `cascade_has_class` 25.5% と `vm_string_at` 18.9%。class の selector ごとに要素（と
  祖先）の class 属性の文字列を 1 文字ずつ分けて比べていた。

## 直したこと

1. **class の atom の cache**（`css/cascade.c`）: engine が class 属性の文字列ごとに、その語のうち sheet が名指す語の atom の列を
   一度だけ作って保つ（文字列の address・長さ・hash で引く表）。class の selector は atom の pointer を比べる。rule の索引の
   class の key も同じ列から。host の検索の `--render` 1.42 s → 0.82 s。
2. **計算した style の cache**（`css/cascade.c`）: engine が要素ごとに計算した style を保つ。engine は document か sheet が変わると
   作り直され、viewport が変わると cache を捨てる。画像・font が届いた後の layout、再描画は cascade をやり直さない。
   host の再 layout（`host-relayout`）: 検索 0.47 s → 0.035 s、トップ 0.26 s → 0.003 s。
3. **sheet の到着をまとめる**（`page/sheets.c`・`page.c`・`script.c`）: 一度 style した page は、他の sheet がまだ届いていない間は
   style をやり直さない（`page_sheets_pending`）。最後の sheet が届いた時に一度だけ style する。

## 試験

- `tests/host-relayout.c`（新）: 各 page を 1280x900 で layout、画像が届いた扱いで再 layout、800x600 で layout し、再 layout の
  dump（layout と display list）が最初と同じ、800x600 の dump が新しい page の 800x600 と同じことを確かめ、時間を出す。
  test page 全て（21）と Amazon の 2 つの capture: 126/126（plain と ASan）。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（`cascade.c`・`page/*.c`）: 新しい指摘 0（既存の 1 件）。
- 回帰（plain と ASan）: golden 56/56、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、
  host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、run-loader-tests 11/11、
  run-http-tests 14/14・`--async` 17/17（async-sheets を含む）、run-font-tests 8/8、host-relayout 126/126。ASan の `--render` で
  Amazon の 3 つの capture に報告なし。
- host の `--render`: 検索 1.42 s → 0.86 s、トップ 0.55 s → 0.56 s（トップは画像の decode が主）。
- guest（QEMU の Venus、zdesktop 1280x800、live の `https://www.amazon.co.jp/s?k=kei`、取得 1 回）: 15 s は sheet を待つ間の
  style の無い描画、**30 s で header・結果・filter が揃う**（前は 90 s）。60 s でも同じ。browser の CPU 時間は 60 s の間に 7 s
  （前は 150 s の間に 54 s）。
- boot test（`plan/tools/boot-test.sh`、この worktree の image、QEMU の uefi-nvme）: PASS（login prompt）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`、guest は QEMU）

- `p071-20260929-guest-amazon-search-15s.png`・`…-30s.png`・`…-60s.png`、`p071-20260929-boot-login.png`。

## 未実施・残り

- 実機は未実施。
- 最初の 15〜30 s の style の無い描画: head の sheet を待ってから描く（Chromium の render blocking）は入れていない（空白を出す
  path と、sheet が届かない時の打ち切りの時計が要る）。
- sheet の取得自体の時間（guest の TLS と 2 MB の転送）、画像の decode（トップの初回の大部分）、layout の中の測定の回数（入れ子の
  flex と shrink-to-fit は box ごとに一度だけ測るようにした（p060）が、各段の本番の layout は残る）。
