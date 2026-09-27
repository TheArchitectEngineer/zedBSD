<!-- awesome-plan project=zedbsd record=ws035p090 -->

# ws035-p090: demo の仕上げ（App Home の全 app を glass の desktop で起動し、目に見える粗を直す）

Phase ID: `ws035-p090`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「Demo polish pass: launch every App Home app ... take a screenshot sheet, and fix
visible rough edges you find (placement, titles, icons, glitches) — list what you fixed and what remains」。fg010: 2026-10-17 の
OSC Tokyo Fall の Wayland desktop の demo）

## 範囲

Venus の guest で zdesktop --glass（1280x800、壁紙）を動かし、App Home から全 app（Files、Terminal、Model viewer、X terminal、
Gears、Browser、組み込みの一覧では試験の client の Vulkan test・Shared memory も）を 1 つずつ起動して画面の一覧を撮り、
見える粗を直す。

## 見つけた粗と直したもの（2026-09-28）

1. **X terminal（zterm）が画面からはみ出す**: zterm は root の大きさ − 余白（1240x720）で窓を作るため、glass の desktop では
   窓の下端が画面の外に出た（`before-x-terminal.png`）。zterm に `-geometry COLUMNSxROWS` を足し（X の慣習の option。無い
   ときは従来どおり root に合わせる。Xzed の rootful も変わらない）、App Home の X terminal は `-geometry 80x24`（640x384）。
2. **Browser が App Home に無い**: 組み込みの一覧に Browser（`/bin/zdesktop-browser /usr/share/zdesktop-browser/start.html`）。
   start page は WS074 が main に入れた（61491025）。
3. **無い app が App Home に出る**: command の絶対 path（program、script、開く file）のどれかが無い entry は出さない
   （`ZWL HOME skip name=... missing=...`）。redirect などの shell の演算子より後の語は見ない（出力先の file は要らない）。
   これで demo の image に無い app（browser の無い image の Browser 等）が出ない。
4. **demo の Home に試験の client**: 組み込みの一覧は試験用の Vulkan test・Shared memory を含む。demo の image には
   `plan/ws035/demo/apps.conf`（Files、Terminal、Browser、Model viewer、Gears、X terminal）を `/etc/zdesktop/apps.conf` として入れる
   （`build-demo-image.sh`）。組み込みの一覧は試験のために変えない。
5. **Files の Home が空で、sidebar の usual folder が薄い**: demo の zdesktop は service として HOME 無しで始まり、/root には
   Desktop 等が無い（`before-files.png`）。demo の `run-zdesktop.sh` が HOME（無ければ /root）を決め、Desktop・Documents・
   Downloads・Pictures・Music・Movies を作る（xdg-user-dirs が session の始めに行うことと同じ）。
6. **image**: `plan/ws031/tests/config-zdesktop-hw.mk`（demo・実機の image）に zdesktop-browser、
   `plan/ws035/tests/config-amd64-zdesktop.mk`（glass の desktop の guest image）に zdesktop-files と zdesktop-browser（Files も
   無かった）。WS075 へは main を通じて事前に知らせた。

## 見つけたが直していないもの（残り）

- **BUG-079**（main が起票、libc/rtld、tracking）: App Home から起動した app は zdesktop の環境を失う（HOME・FOO も、zdesktop
  が子で setenv した XDG_RUNTIME_DIR・WAYLAND_DISPLAY も無く、PATH だけ）。Files・terminal は passwd の home（/root）に戻るので
  demo への影響は小さい。上の 5 の作り方（/root に作る）はこれに依らない。
- **Browser の窓が画面の下にはみ出す**: 既定の高さ 768 が 800 の画面の作業域（690）より大きく、configure_bounds を見ない
  （`demo-03-browser.png`）。WS074 の担当として main へ報告。
- 窓の置き方: 新しい窓は作業域の中央に置かれ、大きさが近い窓は重なって前の窓のタイトルバーが少しだけ見える（cascade 無し）。
  demo では許容とした。
- 暗い窓（zterm・Gears の黒）の上の glass の窓は灰色に見える（すりガラスの意図どおりだが demo では見栄えが落ちる）。
- App の icon は色付きの頭文字（Home、タイトルバー）。本物の icon は無い（第三者の画像は入れない）。
- Home を開いた時の右下の角の小さな四角は、縮んだ desktop の角（設計どおり、閉じる手がかり）。

## 検証（amd64、Venus の guest、2026-09-28。image は `plan/ws074/tests/build-browser-image.sh`）

- `plan/ws035/tests/zdesktop-p090.sh`（新規）PASS: 組み込みの一覧（8 app、Browser を含む）と `demo` の一覧（6 app）の両方で
  全 app の窓が map され、zdesktop の ERROR 無し。画面を目で確かめた。
- 回帰 PASS: files-p011（Home から Files。Browser の有無で 7 か 8 を受けるよう直した）、zdesktop-p070（Home から X terminal と
  Gears、x11server は 1 つ）、zdesktop-p071（Home の page）。
- 規約: `home.c` の style-check 0（前も 0）、zterm `main.c` は前と同数（61）。build warning 0。シェルは `sh -n`。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p090-20260928-venus-demo-00-home.png`…`-demo-06-x-terminal.png`・
  `-demo-99-all.png`（demo の一覧の sheet）、`-builtin-home.png`・`-builtin-all.png`（組み込みの一覧）、`-before-x-terminal.png`・
  `-before-files.png`（直す前）。
- 実機（i915）: 未実施（demo の image そのものは build していない）。boot test: ユーザーの指示で無し。
