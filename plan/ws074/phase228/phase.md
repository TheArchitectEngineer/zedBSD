<!-- awesome-plan project=zedbsd record=ws074-p228 -->

# ws074-p228: UAT の指摘（google.com の表示、既定の URL、アドレスバー）

B1 (2026-10-14)。ユーザーの UAT（原文「google.co.jp のロゴ画像が表示されない。既定の URL を google.com に。google.com が表示できることをゴールに」「アドレス入力の箱が小さい。キーボードリピートが効かない」）への手短な対処。Status: uncleared 相当（host の確認まで。QEMU・実機は未実施、アドレスバー 2 件は compositor 側で範囲外）。

## 原因と直し

1. **ロゴが出ない**: 現在の Google の top は、ロゴを `<img>` ではなく **inline の `<svg>`**（272x92、`<path>` 群）で出す。エンジンは inline svg を描かず（ws074-p063 は計画のまま）、箱が 0 になっていた。検索の箱の中のアイコン（マイク・カメラ・AI モード・アプリ一覧）も同じ。
   - 新規 `userland/desktop/libbrowser/page/svg.c`: `<svg>` を width/height（無ければ viewBox）の大きさの bitmap に描く（g・path の全 command と arc・rect・circle・ellipse・polygon、viewBox、transform、fill・fill-opacity・opacity・fill-rule・display と style 属性、gradient は最初の stop の色）。scanline の sub 5 行・横は正確な coverage、nonzero と even-odd。stroke・clipPath・mask・filter・use・text・style sheet の規則は未対応。
   - `page/images.c` `images_svg`: svg 要素の bitmap を page の画像の table に要素の address で 1 回だけ保つ（後から svg を書き換えても描き直さない）。`layout/box.c`: 外側の `<svg>` は replaced の box。`layout/replaced.c`: width・height の属性が無く viewBox だけの svg は比だけを持ち、containing block の幅いっぱいに広がる（CSS の既定。これが無いと `.wPSZBe svg` のアイコンが 512px で出た）。`page/page.h`、`libbrowser/Makefile` に svg.c を追加。
2. **既定の URL**: `userland/desktop/browser/main.c` の window mode で page 無指定の既定を `https://www.google.com/` に（定数 `MAIN_HOME_PAGE`、旧 `MAIN_START_PAGE` は削除）。**注意**: launcher の `userland/desktop/wayland/apps.conf`（`Browser|/bin/browser /usr/share/browser/start.html|…`）と `wayland/home.c`（`HOME_BROWSER_START`）は start.html を引数で明示しているので、launcher からの起動は直らない。Q1 が compositor 側で引数を外すか https://www.google.com/ にする必要がある。`plan/ws074/tests/browser-start.sh` は start.html を引数で開くので変更なし。
3. **アドレスの箱が小さい**（範囲外、根拠のみ）: 箱は browser の shell ではなく **compositor の titlebar** が描く（`titlebar.c` の `titlebar_controls` の `KL_CONTROL_BREADCRUMB` を `userland/desktop/wayland/titlebar-shell.c` が配置）。幅は `shell_layout`（titlebar-shell.c の 1395 行付近）が決め、breadcrumb の最小幅は `CRUMB_LEAST 120`、tab の題は `TAB_MOST 200` まで。browser 側（`shell/titlebar.c`）は controls の宣言（Back・Forward・Reload・Location）と text・parts を渡すだけで、幅を指定する手段が無い。tabs の presentation で breadcrumb が余りを取らないのは compositor の配置。
4. **キー repeat**（範囲外、根拠のみ）: アドレスバーの編集は compositor が持つ（`titlebar-shell.c` `kwl_titlebar_key`、`state != 0` を押下 1 回として処理し、`eaten_key` に保つだけ）。kernel の autorepeat（value 2）は `input-thread.c:948` で転送されず、field に repeat の timer が無い（server の `repeat_rate/delay` は client へ `repeat_info` として渡すだけで、field は client ではない）。browser のページ内の入力には影響しない。直すのは compositor の titlebar field に repeat の timer を持たせること（Q1 の担当）。QEMU のせいではない（zterm など client 側の repeat は repeat_info を使う）。

## 確かめ（host）

- build: `sh plan/ws074/tests/host-build.sh plain`、warning 0。
- 画面: `python3 plan/ws074/tests/live-compare.py https://www.google.com/ --width 1024 --height 768 --program build/ws074-host/plain/browser --out build/uat1/live`。ink の一致は 11.67%→23.47%（全体 93.74%→97.30%）。左から ours・Chromium・差分: [after](../../../build/review/uat-google-after-vs-chromium.png)。before: [before](../../../build/review/uat-google-before.png)（ロゴ・マイク等なし）。
- svg の大きさの最小例（inline svg の 4 通り、Chromium と並べた）: [png](../../../build/review/uat-svg-sizing-ours-vs-chromium.png)、[html](evidence/svg-sizing-min.html)。4 行目は `<img src="data:image/svg+xml">`（img の svg は未対応、今回の範囲外）。
- 取得した HTML（この browser の UA）: [google-com-20261014.html](evidence/google-com-20261014.html)（230,603 byte、地域は日本、gbv=1 でない現代の版）。
- Acid3: score 100、差 0（exact match）。Acid2 差 1,865（不変）。host CSS 51/51・JS 14/14・Grid 48/48。

## 残り

- google.com の差: ロゴと検索の箱の縦の位置（Chromium は 1fr の row の中央で約 75px 低い、ours は上詰め。`display:grid; grid-template-rows:minmax(0,1fr)` の高さの扱い）、「Gmail」「画像」のリンクと下の「English」の行（script が作る、`BigInt is not defined` で script が止まる）、検索の箱の `|`（cursor）。
- QEMU・実機: 未実施。T1 に頼む手順: 変更した browser の package を含む image を `plan/ws074/tests/config-amd64-browser.mk` ＋ `plan/ws074/tests/build-browser-image.sh` で作り、`browser` を引数なしで起動して window に google.com のロゴと検索の箱が出ることを `plan/tools/guest/` の QMP screendump で確認（網が要る。launcher 経由は上の 2 のため引数が要る）。
