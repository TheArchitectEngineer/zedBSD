<!-- awesome-plan project=zedbsd record=ws138-p003 -->

# ws138-p003: 全文規約の見直しと、F-071・WS089・WS099 への結果の案

Phase ID: `ws138-p003`
Parent: [WS138](../ws.md)
Status: in-progress（2026-10-05 P1 generation17、q731。見直しと直しは済み。Q1 の判定待ち）
Phase disposition: normal
Queue: q731
依存: p002（cleared、T1-172）

## 範囲

- WS138 で変えた・足した C の全部を、[全文の C の規約](../../coding-style.md)（890 行、2026-10-05 の main）で見直す。
- T の結果（T1-169・T1-171・T1-172）の反映。
- F-071（Future Work）・WS089（Settings）・WS099（試験の基準）への結果の案（他の WS の記録なので Q1 が書く）。

## 見直した C

| file | WS138 の変更 | 見直しの結果 |
| --- | --- | --- |
| `userland/desktop/picture/wallpaper.c`（新） | 共通の復号（PNG・JPEG） | 直した: (1) 画素の数の比較の `unsigned long long`（C99 の型）を消した（一辺が `KL_WALLPAPER_SIDE_MAX` 以下と確かめた後なので積は `unsigned long` に収まる、i386 の 32 bit でも 2^26）、(2) PNG の枝の call に comment、(3) `longjmp` の call に comment。例外: `setjmp` を条件の中に置く（C の規格が `setjmp` を置ける場所を条件などに限るため。`picture.c` 294 と同じ） |
| `userland/desktop/picture/wallpaper.h`（新） | 型と関数 | 直した: file の説明の comment の行の折り返し |
| `userland/desktop/wayland/glass.c` | prefetch・loader の復号、`zwl_glass_landscape`、`wallpaper_read`・`prefetch_take`・`prefetch_drop` | 直した: (1) `zwl_glass_landscape` の memset と `wallpaper_draw` の call を別の段落にし、call に comment、(2) `prefetch_take` の `started` を消す所に、それが `prefetch_drop` との約束であることを書いた。`printf` の `%llu` と `unsigned long long` の cast は、64 bit の時刻を ANSI C で表す手段が無いので残す（同じ file の既存の書き方と同じ） |
| `userland/desktop/wayland/settings.c` | `settings_apply_wallpaper` の風景と thread の道 | 指摘無し |
| `userland/desktop/wayland/zwl.h` | `zwl_glass_landscape` の宣言 | 指摘無し |
| `userland/desktop/settings/look.c` | 一覧（`look_found_add`・`look_compare_names`）、縮小（`look_thumbnail`）、`look_file_read` | 指摘無し（p001 で条件演算子と call の条件を直してある） |
| `userland/desktop/files/ui-home.c` | hero を `fm_image_load` に | 指摘無し |
| `plan/ws138/tests/host-wallpaper-decode.c`（新、試験） | host 試験 | 直した: (1) 3 つの節の条件を 1 節 1 行に、(2) 平均の差の和の `unsigned long long` を `double` に（最大 6M×3×255 で 2^53 より小さく、正確） |

`plan/tools/style-check.py` の結果: WS138 の file・hunk に残る指摘は `wallpaper.c` の `setjmp` の 1 件（上の例外）だけ。`glass.c` の 4 件（`zwl_glass_wallpaper_begin` の
`S_ISREG`・`close` の後の空行、`file_read` の `S_ISREG`、`loader_run` の `(void)argument;`）と `settings.c` の 1 件（922 行の条件演算子）は WS135 の既存の行で、
WS138 で変えていない（範囲外、変更前と同じ数）。

Python の道具（`userland/desktop/wallpapers/ppm-to-png.py`・`generate.py`）と shell の試験の script には、プロジェクトに全文の規約が無い（Guardrail の
規約の表は C だけ）。読みやすさは C の規約の考え方（1 つの段落に 1 つの comment、短い関数）に合わせて見直し、直す所は無かった。この制限を記録する。

## 確かめ（2026-10-05、直しの後）

| 確かめ | 結果 |
| --- | --- |
| `plan/ws138/tests/run-host-wallpaper-decode.sh`（ASan/UBSan） | 10 項目すべて ok |
| zedBSD `make build/amd64/bin/wayland build/amd64/bin/settings build/amd64/bin/files` | rc 0、warning 0 |
| Linux `make -f userland/desktop/keiland-linux.mk … bin/wayland bin/settings` | rc 0、warning 0 |
| `plan/ws089/tests/host-build.sh` と `host-wallpaper.sh` | PASS |
| `git diff --check` | 空 |
| QEMU | 直しは comment・段落・型の書き方だけで、動きを変えない。T1 の再試験は依頼しない（T1-172 の PASS の後の変更は、cast の型と comment と段落の分け方だけ） |

## T の結果の反映

- T1-169: settings-p009・p004・p007・boot-test PASS。wallpaper-time の errno（zedBSD の EINVAL は 3）、c7 の測る箱、greeter の確かめの手順を直した（p002 の結果）。
- T1-171・T1-172（Q1 の記録）: 直した試験で PASS。p001・p002 は Q1 が cleared（2026-10-05、dd1b3bd9）。

## F-071・WS089・WS099 への結果の案（Q1 が書く）

1. **F-071**（`plan/future-work.md` 80 行、disposition は promoted）: 「WS138 で完了（2026-10-05）。背景は PNG と JPEG を読み、tree と生成の背景は PNG、PPM の読み込みは消した（ユーザーの U4）」を足す。
2. **WS089**（Settings）:
   - 背景の頁は PNG・JPEG を一覧し、同じ名前の物は png・jpg・jpeg の順で 1 つにする。縮小は全体の復号から（`look.c`）。
   - 生成の背景（ws089-p009、`settings-p009.sh`）は `.png`。
   - Settings は `userland/desktop/picture/wallpaper.c` と libpng・libjpeg・libz-compat を link する（WS168 の後は preview の command に移る予定）。
   - ws089 の host build（`plan/ws089/tests/host-build.sh`）は compat の library と `wallpaper.c` を compile する（WS138 の `apply-q1.sh` で入った）。
3. **WS099**（試験の基準）:
   - p019 の背景（Birch-Lake・Lakeside）は PNG になった（README の出どころの記録は保った）。
   - c7-contrast の測る箱は 2026-10-05 の画面の配置に直した（`c7-boxes.diff`、Files の sidebar の Today・Home の追加と Settings の section の見出しの位置）。
     f-inactive は Desktop の行に移した。今後 Files・Settings の配置を変える WS は c7 の箱を確かめる、という注意を WS099 に。
4. **残す試験**（WS の完了の時、`plan/tools/` に移して Master の Tools 節に登録する候補）: `run-host-wallpaper-decode.sh`・`host-wallpaper-decode.c`（背景の復号の host 試験）、
   `wallpaper-time.sh`（背景の読み込みの時間）、`greeter-wallpaper.sh`（greeter の背景）。移す先の案は `plan/tools/wallpaper/`。

## 結果

全文規約の見直しで 7 件を直した（上の表）。残る指摘は C の規格による `setjmp` の 1 件（例外）と、WS138 で変えていない既存の 5 件。build と host 試験は通る。
受け入れ（Q1 の判定）を待つ。WS138 の全部の Phase が cleared になったら、ws.md を完了の形に書き直し、phase の directory を消し、上の 4 の試験を
`plan/tools/wallpaper/` に移す（Master の Tools 節は Q1）。
