<!-- awesome-plan project=zedbsd record=ws127-p004 -->

# ws127-p004: thumbnail の拡張（F-035）

Status: in-progress（q667、P2、2026-10-04。残りの実装と host 試験は済み、QEMU は p008 の回帰と一緒に T1 へ）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q667（Q1 の dispatch、2026-10-04）
依存: p001 でユーザーが採用（2026-10-04 Q1 の委任の採否）。WS079 の libpdf（読むだけ）
目安: 3h（1 Queue）。実行者の目安: phase-runner-mid
所有 path: `files/thumb.c`・`peek.c`・Makefile の link、`picture/` を変えるなら WS128 と直列、`plan/ws127/tests/`

## 範囲

(1) PDF の 1 頁目の thumbnail（libpdf の `pdf_page_render` を使う）。(2) disk の thumbnail の cache（freedesktop.org の `~/.cache/thumbnails/normal`、mtime と URI の MD5）。(3) 動画は WS122（動画プレーヤ）の decoder の後で、今回は外。

## 受け入れ

100 項目（PDF 10 を含む）の folder で PDF に thumbnail が出る（画面）、2 回目の表示で cache から読む（log）、壊れた PDF で落ちない（host 試験）。
変えた source の style-check の違反 0、build の warning 0、`files-regress.sh` の 14 本と boot test PASS。

## 範囲の照合（2026-10-04 P2）

(1) PDF の 1 頁目の thumbnail（libpdf を dlopen、無ければ従来の icon）と (2) disk の cache は [ws127-p002](../phase002/phase.md)（q616、QEMU の
files-p002 で `cached=0` → `cached=1` PASS）で実装済み。cache は freedesktop の `~/.cache/thumbnails/normal`（PNG、URI の MD5）でなく
`$XDG_CACHE_HOME/keiland/thumbnails`（path の SHA-256 の PPM、mtime と size を記録）。tree に PNG の encoder が無いので freedesktop の形には
移さない（他の desktop と cache を共有しないだけで機能は同じ）。この Phase で足したのは受け入れの残り: cache の上限と消し方、壊れた PDF の試験。

## 未決の判断

なし。cache の上限と消し方は委任の技術判断（P2、Q1 に連絡）: 書く度に件数を数え、2000 件を超えたら書いた時刻の古い順に消して 1800 件にする。

## 実装（2026-10-04、q667）

- `files/thumb-cache.c`: `cache_trim()`（folder の通常の file を mtime の古い順に並べ、`keep` 件まで消す、log `THUMB cache trim records= removed=`）を
  `fm_thumb_cache_write` の成功の後に呼ぶ。試験用に公開の `fm_thumb_cache_trim(maximum, keep)`（`files.h`）。同じ file の既存の style の指摘
  （閉じ括弧の後の空行 17 件、条件の中の fclose、1 行の 3 節以上の条件 3 件）も直した（振る舞いは同じ）。
- 試験 [host-pdf-thumb.sh](../tests/host-pdf-thumb.sh)（host で libpdf.so を作り dlopen させる）: 本物の 1 頁の PDF（512 の辺）、1/3 に切った PDF・
  signature だけ・空・signature の後に乱数 4 KB は EINVAL で落ちない、5 件を 3 件に trim すると古い 2 件が消える、上限の内なら消さない。

## 確認（host。QEMU は未実施、p008 の回帰と一緒に T1 へ）

- `sh plan/ws127/tests/host-pdf-thumb.sh` PASS（8 件）。`sh plan/tools/files/host-model.sh` PASS。
- zedBSD amd64 の files の build warning 0。`plan/tools/style-check.py`・`plan/tools/imageview/style-extra.py` とも thumb-cache.c で 0。
- 未実施: 100 項目（PDF 10）の folder の guest の目視（files-p002 の PDF の thumbnail と cache は QEMU で PASS 済み）。

## 検証の方法と範囲

QEMU の Venus（`plan/tools/files/files-guest.sh`、`files-regress.sh`）と host の試験。console・serial の log では判定しない。各 Phase の最後に `files-regress.sh` の 14 本と boot test。
Settings と共有の `canvas.c`・`text.c`・`icons.c` を変えたら `sh plan/ws089/tests/host-build.sh` と Settings の host 試験も流す。

## Event

2026-10-02 / ws127-beta1-plan: fg019 の計画で新設（planning）。p001 の結果とユーザーの選択で範囲を確定し planned にする。
