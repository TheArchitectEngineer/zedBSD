<!-- awesome-plan project=zedbsd record=ws282-p006 -->

# ws282-p006 画素の試験と堅牢化（`camcheck`・`pattern.html`）

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（実装済み・試験未実施。この Phase の受け入れは実機（5330 + HDMI capture + pattern の PC）で、それは未実施）

## 実装した事

- `plan/ws282/tests/pattern/pattern.html`（D11 の前提: ユーザーの PC の browser が全画面で出す。D11 は提示中なので推しで作った）: 画面の割合で配置（上の帯 4〜30 %: 赤・緑・青・白・黒・灰 128・黄・シアン・マゼンタの 9 patch、34〜52 %: 灰の 8 段、58〜78 %: 16 bit の frame counter（左が MSB、白 = 1、`requestAnimationFrame` ごとに +1）、84 % 以下: 8 device pixel の市松（`?mode=yuyv` だけ、`?mode=mjpeg` は無し）。click か key で全画面。
- `userland/tests/camcheck/analyze.{h,c}`: pattern の読み（device に触らない）。領域の内側の平均（YUYV は Y と組の Cb/Cr、RGB）、BT.601/709 と限定/全 range の RGB⇄YUV、range は黒と白の Y、matrix は 6 色の YUV の二乗誤差で推定、patch の最大誤差と最悪の patch、counter の解読（黒と白の中間で閾値）、counter の run（16 bit の wrap、半分以上の後ろ向きは減少、増分 0・1・2・3 以上の分布）。
- `userland/tests/camcheck/main.c`: `-d -f -w -h -i -b -n -t -o --tolerance --cycles --second-open`。N frame の各々で patch（YUYV は最初の frame で推定した matrix/range で RGB に、MJPEG は DHT を補って libjpeg-compat で RGB に）と counter、ERROR・INCOMPLETE・sequence の飛び（drop）・解読の失敗を数え、最初の frame を PPM に。合否: frame の数・ERROR 0・drop 0・解読の失敗 0・許容（YUYV 8、MJPEG 16）を超える frame 0・counter の減少 0。`--cycles n`: 停止→開始→1 frame を n 回、前後の `vfs.io.stats` の `USB_TRANSFER_RESERVE`・`USB_BUFFER`・`DMA` の alloc と free の数が同じか（leak）、`XHCI_REQUEST_ALLOC` の増分も表示。`--second-open`: 所有者が buffer を持つ間に 2 つ目の open の `TRY_FORMAT`・`SET_FORMAT`・`REQUEST_BUFFERS` が `EBUSY` か。最後の行 `camcheck: PASS` / `camcheck: FAIL (<理由>)`、終了 0/1/2（ENOSPC）/3/4。
- `userland/tests/camcheck/Makefile` と `plan/ws282/tests/config.mk` に `camcheck`。

## 設計と違う判断

- libjpeg-compat に link せず、その source（7 file）を camcheck の source に入れた: test の basic program は platform の vmunix.mk が C library だけで link し（`AMD64_USER_BASIC_COMMAND`）、shared library を使う program は arch ごとの mk の列挙が要る（4 arch の mk の変更になる）。object の path は libjpeg-compat の package と同じ `dynamic/obj/userland/desktop/libjpeg-compat/` になる（同じ flag で compile される物なので共有で差し支えない見込み、未確認）。
- 出力は PNG でなく PPM（camcommon の writer）。PNG は host で変換する。
- device ごとの quirk（`no_fid` など）・bulk の device の実地は手元の device が要るので未着手。

## 確かめた事（host と build だけ）

- `sh plan/ws282/tests/run-camcheck-host-test.sh`（新規）: pattern.html と同じ割合で描いた frame（640x480・1280x720・320x240）を BT.601/709 × 限定/全 range の YUYV に符号化（device のように 0〜255 に clamp）して、推定が正しく patch の誤差が 2 以下・counter の値、±5 の雑音の RGB で誤差 6 以下・counter、色の誤りの検出、counter の run（増分・減少・wrap）、領域と stride の拒否。通常と ASan/UBSan で PASS。
- `make -j16 ZEDBSD_CONFIG=plan/ws282/tests/config.mk BUILD=build/ws282/p6d-amd64 build/ws282/p6d-amd64/bin/camcheck build/ws282/p6d-amd64/bin/camcap`: rc 0・warning 0、`check-dynamic-elf.py` まで通った（log `build/ws282/logs/p006-camcheck.log`）。

## 未実施

- 実機（5330 + capture + pattern の PC、ユーザーの時間）: `camcheck -f yuyv`・`-f mjpeg` で 100 frame、HDMI capture の YUYV と 1080p MJPEG（p005 の後）、drop の測定、`--cycles 100` の leak、`--second-open`、抜去と再挿入（ユーザーの試験の一覧へ）。pattern.html を browser で出した事も無い。
