<!-- awesome-plan project=zedbsd record=ws074p057 -->

# ws074-p057: 部品化 4 — `libbrowser.so` への分割

Phase ID: `ws074-p057`（2026-09-28 main が割り当て、[p053](../phase053/phase.md) の手順 5）
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: ws074-p056

## 範囲

engine を `libbrowser.so` に分ける。公開の header を `include/libc/` の適所に置き、exports.map で公開の呼び出しだけを出し、Desktop の
menu の group に登録し、`/bin/browser` はそれを link する。Wayland の shell の code なしで部品が動くことを示す小さな 2 つ目の
使い手を作る。build の warning 0、試験と boot test をやり直す。

## 設計

- 公開の header: `include/libc/browser.h`（`<keiland.h>`・`<pdf.h>` と同じ置き方。sysroot の `usr/include/browser.h`）。p054〜p056 の
  `view/view.h` の API を移し（`view/view.h` は消した）、API の文書を兼ねる。draft（[browser_view.h](../phase053/browser_view.h)）に
  合わせて足したもの: `BROWSER_API_VERSION` と `browser_view_options.version`（違えば `ENOTSUP`）、engine の内部の
  `struct text_font_paths` の代わりの `struct browser_fonts`（NULL の欄は system の font。既定の path は engine が持つ）、
  `extern "C"`。main.c の `--js`・`--dump=ast`・`--dump=code` が使っていた engine の内部（JS の parser・compiler・VM）は
  `browser_script_tool`（`js/tool.c` 新、`BROWSER_SCRIPT_RUN`・`_DUMP_AST`・`_DUMP_CODE`、`BROWSER_SCRIPT_STRICT`・`_MODULE`）に
  移した。出力と stderr の文言と exit status は同じ。
- library: `userland/desktop/libbrowser/Makefile`（package `libbrowser`、class library・type shared-library、menu の group `desktop`、
  依存 libtruetype・libvulkan・libjpeg-compat・libpng-compat・libz-compat・libgif-compat、`/lib/libbrowser.so`、committed の表の
  license の notice はこちらへ）と `exports.map`（`browser_view_*`・`browser_offscreen_*`・`browser_add_ca_file`・
  `browser_script_tool`、他は local）。source は `userland/desktop/browser/` の module の directory に残した（試験と文書の path を変え
  ない。Makefile は source を登録するだけ）。`platform/amd64/vmunix.mk` に `libbrowser.so` の link（`-z defs`、`check-dynamic-elf.py`）。
- `/bin/browser`: package の source は `main.c` と `shell/` だけ。`<browser.h>` だけを include し（`shell/shell.h` は
  `UNUSED_PARAMETER` を自分で持つ）、link は libbrowser・libvulkan・libwayland-client・libkeiland・libc。
- 2 つ目の使い手: `userland/base/tests/browser-probe`（package `browser-probe`、menu の group `desktop`）。`<browser.h>` と libc だけ
  （compile に engine の include の path を渡さない）、link は libbrowser.so と libc.so だけ（Wayland も Vulkan の loader も直接は要らない）。
  view を作って page を読み、settle、`--tab=N` で Tab を押し（focus の ring）、CPU（`browser_view_draw_pixels`）か `--gpu` で engine の
  offscreen の image（`browser_offscreen_*`、`browser_view_set_gpu`・`_draw`）に描いて PPM に書く。callback の title・console・link
  （拒否）・load の失敗を `BROWSERPROBE` の行で出す。
- 試験の側: `list-sources.sh` は 2 つの Makefile を読む。`host-build.sh` は `browser.h` を host の include に link し、host の
  `browser-probe`（engine の object と静的に link）を作る。`config-amd64-browser.mk` に libbrowser と browser-probe。
  `host-view.c` は `<browser.h>` に。新しい guest の試験 `browser-p057.sh`。

## 確認（host は Debian の cc と lavapipe、guest は QEMU の Venus）

- host の build（-Werror、plain と ASan）warning 0。amd64 の image の build（`build-browser-image.sh`、sysroot も新しい header で
  作り直し）: browser・libbrowser・browser-probe の warning 0（warning は perl・openssh・openssl の package のもの）。
  style-check（`include/libc/browser.h`・`js/tool.c`・`main.c`・`view/view.c`・`shell/shell.h`・`browser-probe/main.c` ほか）0。
- 分割の前（p056 の commit の build）と後で、試験の page 7 つと画像の page 3 つの `--render`・`--render-gpu`・`--run`・4 種の dump と
  stderr の 126 file が byte で同じ（`build/p056/compare.sh`）。`--js`・`--dump=ast`・`--dump=code`（`--strict`・`--module`、構文の誤り・
  例外・読めない file・file なしを含む 112 の出力）も同じ（違いは試験の一時 file の path だけ）。`run-js-tests.py` 7/7。
- golden の dump 32/32、`host-view` 59/59（plain と ASan）、`run-http-tests.py` 同期 14/14・`--async` 16/16、`run-loader-tests.py` 11/11。
- host の `browser-probe` は `browser --render`・`--render-gpu` と byte で同じ（first・script・images、plain と ASan）。ASan で CPU の
  probe（`--tab=3`）は leak の報告なし。
- guest（Venus）`browser-p057.sh` status 0: libbrowser.so の export は `<browser.h>` の 36 の呼び出しと完全に一致し、他は無い。
  NEEDED: browser は libbrowser・libvulkan・libwayland-client・libkeiland・libc（engine の library は要らない）、browser-probe は
  libbrowser.so・libc.so だけ、libbrowser.so は libvulkan・libtruetype・libjpeg-compat・libpng-compat・libz-compat・libgif-compat・libc。
  guest の中で probe と `/bin/browser --render`・`--render-gpu`（Venus）の PPM が first・blocks・script で byte で同じ。`--tab=1` で
  keydown・focusin three と ring。`/bin/browser --js` は library を通って動く。（1 回目は export の数の閾値を誤って 40 にした試験の
  側の誤りで status 1。試験を `<browser.h>` の宣言との比較に直して status 0。probe と link の結果は 1 回目も同じ。）
- 窓の試験（同じ image と guest、続けて）: `browser-p045.sh`・`p030`・`p017`・`p021`・`p052`・`p056` は 1 回目で status 0。
  `browser-p014.sh` と `browser-p050.sh` は 1 回目に、zdesktop を起動した直後の最初の browser の起動で READY（p050 は窓の MAP も）が
  無く status 1（その後の起動は同じ run の中で成功）。p054・p055 でも出た、zdesktop の socket の前に browser が起動する試験の起動の
  待ちの揺れと同じ形。library の読み込みの遅れでないことは guest で `time /bin/browser --version` が 0.00 s であることで確かめた。
  変更なしの再実行: p014 は 2 回目で status 0。p050 は 2 回目に guest の SSH の接続が切れて NAVIGATE の読み取りだけ失敗
  （「Connection to 127.0.0.1 closed by remote host」、guest の HTTP 16/16）、3 回目で status 0（guest の HTTP 16/16）。
- 写真: `/home/awe/zedBSD-rpi4/build/ws074-shots/p057-20260928-probe-gpu-first.png`（guest の probe の GPU の描画）・
  `p057-20260928-probe-tab-ring.png`（probe の Tab の ring）。boot test は下。実機は未実施。

## 残り・移管

- API の安定性: `BROWSER_API_VERSION` は 1。struct を変えるときは version を上げる。symbol の version（`LIBBROWSER_1` の node）は
  付けていない（他の base の library と同じ匿名の version script）。
- 複数の view を別々の thread で使うこと、1 つの device に複数の view（queue の共有は呼ぶ側の同期）は範囲外のまま。
- System Settings の窓などの実際の使い手、HTML の文字列を読む `browser_view_load_html`（draft にある）: 使い手ができる Phase で。
