<!-- awesome-plan project=zedbsd record=ws079-p016 -->

# ws079-p016: デモの S8・S9 の通しの試験と頁送りの時間（L2）

Status: cleared（2026-09-30、サブエージェント P4、worktree `ws090-widgets`（branch `wt/ws090`、main を merge した上。WS079 の worktree は無い）。
QEMU の Venus の注入の touch と pen。実機と Windows の QEMU は未実施（ユーザーの手順を用意した））
Disposition: normal
Parent: [WS079](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て「WS079 の L2: ws079-p016」）

## 範囲と受け入れ（ws.md の段の計画 L2）

- 台本の S8（右上の角の swipe で Notes、書く、Esc で窓に）と S9（PDF の頁送りと拡大）を、Linux の QEMU の注入の touch で自動で通す script
  `plan/ws079/tests/demo-s8-s9.sh`。
- A4 の 10 頁の PDF の頁送り 10 回の描画の時間の最大を PDF Viewer の log で測る。目標 200 ms 以内。log の行が無ければ 1 行足す。
- 実機と Windows の QEMU での確認の手順の 1 枚（ユーザー向け）。

## 変更

- `userland/desktop/pdfviewer/main.c`・`view.c`・`viewer.h`: 頁送りの時間の log `PDFVIEWER TURN done page= ms= frame_ms=`。
  - 頁を替える操作（scroll の頁の移動、page の頁の turn の始まり）で時刻を取る。
  - 頁が止まった後の最初の frame の表示で、操作からの ms と、その間の frame の最長（描画と present）を出す。
  - 描画の中身は変えていない。
- `plan/ws079/tests/config-amd64-demo.mk`（新）: Notes の image（PDF Viewer・libpdf・Notes・注入・peninject）に touchinject。
- `plan/ws079/tests/make-a4-document.sh`（新）: host の quilt の説明書（pdfTeX、埋め込みの Type 1）の先頭 10 頁を Ghostscript で A4 にした文書。
- `plan/ws079/tests/demo-s8-s9.sh`（新）: 下の試験。image の build（`config-amd64-demo.mk`）も含む。
- `plan/ws079/demo-s8-s9-manual.md`（新）: 実機（mouse）と Windows の QEMU（touch）の手順の 1 枚。

## 試験（`demo-s8-s9.sh`、QEMU の Venus、1280x800、`build/ws079/demo.img`）

**PASS**（3 回目。worktree の `build/ws079-demo-run3.log`、画面と log は `build/ws079-shots/p016/`）。

| 確かめ | 結果 |
| --- | --- |
| S8 | 右上の角から左下への指の swipe → `ZWL CORNER commit` → `NOTES START … fullscreen=1`（`s8-fullscreen.png`）。<br>pen の線 → `NOTES STROKE`（`s8-written.png`）。<br>Esc → 窓（`NOTES LAYOUT window=`、`s8-window.png`） |
| S9 a（scroll、→ 9 回と ← 1 回） | 10 回の turn の最長 **142 ms**（目標 200 ms 以内、他の回は 96〜130 ms 台） |
| S9 b（page、指の swipe 9 回と逆 1 回） | 10 回の turn。frame の最長 **140 ms**（目標以内）。<br>slide の動き（約 250〜300 ms）を含む操作から止まるまでは最長 422〜474 ms |
| S9 c（拡大） | double tap で拡大 → fit（`TOUCH double-tap zoom=1.563`）。<br>2 本の指で拡大（`TOUCH pinch end scale=1.8`〜`3.6`）。画面 `s9-double-tap.png`・`s9-pinch.png` |
| compositor | `ZWL ERROR` 0 |

- 頁送りは 200 ms 以内なので、先読みの Phase は要らない。PDF Viewer は次の頁・前の頁を既に先読みしている（`view.c` の prefetch）。
- 途中で直した試験の誤り:
  - 1 回目: S8 の swipe が compositor に届かなかった。compositor の起動の直後で、角の zone の準備の前だった。
    → `ZWL CORNER zone` を待ってから 3 秒置く形にした。
  - 1 回目: page の swipe が 1 回欠けた。touchinject の device の登録の前の最初の指だった。→ script の頭の待ちを 1.5 秒にした。

### 回帰

- `plan/ws079/tests/run-pdf-render.sh` ok、`run-pdfviewer-host.sh` ok。
  - `build/ws035-fonts` が worktree に無く「font opens」で落ちたので、main の `build/ws035-fonts` を読み取りの symlink で使った。変更の前の source でも同じ失敗だった。
- 規約: `style-check.py`（pdfviewer の main.c・view.c・viewer.h）違反 0、`git diff --check` 0。
- build: pdfviewer と demo の image、warning 0。

## 未実施・残り

- 5330 の実機（mouse）と Windows の QEMU（touch）: 未実施。ユーザーが `plan/ws079/demo-s8-s9-manual.md` の手順で確かめる。
- touch だけで Notes の全画面を出る操作は無い（Esc か F11）。台本で要るなら次の Phase の候補。
