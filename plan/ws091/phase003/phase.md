<!-- awesome-plan project=zedbsd record=ws091-p003 -->

# ws091-p003: 全文の規約と回帰

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS091](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws091-imageview`、branch `wt/ws091`、main e56f52da に同期してから）
Approval: main の依頼「ws091-p003（規約の全文との照合と回帰）」。touch の guest の確認（`CONFIG_INPUT_TEST_INJECT=y` の image、clang・libcxx を
含まない config）は「可能なら」。

## 範囲と受け入れ

WS091 が作った・変えた source（`userland/desktop/imageview/` の全体と、他の file への最小の差分）を `plan/coding-style.md` の全文と照合して直す。
受け入れ: `plan/tools/style-check.py` の残りは design の例外（`setjmp`）だけ、目視の照合、host の `run-host.sh` PASS、build の warning 0、
guest の `imageview-guest.sh` の全 step ok、boot test PASS。

## 照合の方法

1. `python3 plan/tools/style-check.py userland/desktop/imageview/*.[ch]`（機械で判る規則）。
2. style-check.py が見ない規則を補う発見的な走査（[tests/style-extra.py](../tests/style-extra.py)）: 3 つ以上の節・`&&` と `||` の混在を 1 行に
   置いた条件（§6）、呼び出しの結果の直接の return（§11）、式で作る Boolean（§6）、入れ子・複数行の条件演算子（§6）、引数の `(void)` の
   cast（§4 の `UNUSED_PARAMETER`）、2 つ以上の引数を 1 行に置いた分割の呼び出し（§8）、注釈の無い関数・型・file-scope の変数（§2・§3）。
3. 目視: 全 18 file（約 11000 行）を上から読み、段落の注釈・空行、確保と検査、関数の順、注釈の中身を §14 の checklist で見た。

## 直したこと（file ごと）

| file | 直したこと |
| --- | --- |
| `image.c` | `image_refuse` を void にし、呼び出し元が errno 値を return する（呼び出しの結果の直接の return 16 件）。`image_kind` の 1・2・3・-1 を `enum image_kind` に、unreadable の errno を引数で返す。JPEG の画素と行の 2 つの確保を 1 つずつ確保・検査。GIF の表の確保を段落に分け、1 枚だけの条件を `else if` の連鎖に。3 つ以上の節の条件を 1 行 1 節に。非標準の `for` を 3 行に。`32768` を `IMAGE_SIDE_MAX` に。段落の注釈と空行、`UNUSED_PARAMETER` |
| `imageview.h` | `UNUSED_PARAMETER` の定義。使われていない `iv_canvas_copy`・`iv_canvas_stretch` の宣言を削除 |
| `view.c` | 式で作る Boolean（`control`・`shift`・`playing = !playing`）を `if` に。3 つ以上の節の条件（10 件）を 1 行 1 節に。分割の log の呼び出しを 1 行 1 引数に。case の中の `if` と guard の後の段落に注釈と空行。`view_min_scale` の実装と合わない注釈を直した |
| `main.c` | 待ち時間の計算を段落に分けた。`!main_window.fullscreen` の引数を `if` で作る `wanted` に。3 節の条件、分割の log、guard の後の段落 |
| `window.c` | 引数の `(void)` 26 関数を `UNUSED_PARAMETER` に（宣言の後の空行の後）。listener の表を 1 行 1 項目に。成功の return の前の代入を段落に |
| `present.c` | 公開の `iv_present_set_image`・`iv_present_set_frame` を static 関数の前へ移した（§2）。`vkCreatePipelineLayout` の結果をすぐに検査（§9、以前は pipeline の作成の後でまとめて検査）。非標準の `for` 2 件（1 件は guard の `if` に分けた）。3 節以上の条件、分割の呼び出し、scissor・sampler の段落 |
| `menu.c`・`titlebar.c` | `if (error == 0) error = ...;` の連鎖（1 つずつ検査していない）を、transaction の中の項目を 1 つずつ設定して最初の拒否で返す関数（`menu_state_items`・`titlebar_build_controls`・`titlebar_state_controls`）に分け、呼び出し元が拒否でも commit する形に。listener の表、分割の呼び出し、`UNUSED_PARAMETER` |
| `touch.c`・`touch.h` | 頭の注釈の「PDF Viewer」を Image Viewer に。局所変数の `(void)dy;` を削除。case の段落の注釈、`UNUSED_PARAMETER` |
| `draw.c`・`glass.c`・`chooser.c`・`folder.c`・`text.c`・`canvas.c` | 段落の注釈と空行、3 節の条件、分割の snprintf。`chooser.c`・`text.c` の壊れた注釈（`/* \| (size_t)written >= size)\|...`、`/*  (bytes[part] & 0x3fU);\|...`）を直した。`text.c` の `fseek`・`ftell` をまとめて検査していたのを 1 つずつに。`canvas.c` の使われていない `iv_canvas_copy`・`iv_canvas_stretch`（PDF Viewer の名残）を削除 |

- 他の file への最小の差分（`userland/desktop/wayland/home.c` の 1 行、`icons.c`・`icons.h` の表の行、`plan/ws035/demo/apps.conf`、
  `config/ci/config-amd64.mk`・`plan/ws075/demo/config-demo-hdmi.mk` の一覧、`platform/amd64/vmunix.mk` の link の規則）は周りの表・規則と同じ形で、
  直すことは無かった（変えていない）。
- 意味を変えた点: 無し（出力の log の文字列・順序、error の値、確保の失敗時の解放を保った）。`present.c` の pipeline layout の失敗は以前は
  pipeline を作らずに同じ error を返していた。今は shader module を解放してすぐ返す（同じ結果）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| style | `python3 plan/tools/style-check.py userland/desktop/imageview/*.[ch]` | 残り 1 件: `image.c` の `if (setjmp(failure.back) != 0)`（setjmp は条件の中にしか置けない、design の例外） |
| style（補い） | `python3 plan/ws091/tests/style-extra.py userland/desktop/imageview/*.c` | 残り 5 件: `present.c` の頂点の表の初期化子（1 行 1 頂点の 4 つの float、呼び出しではない） |
| diff | `git diff --check` | 問題なし |
| build | `timeout 900 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws091-amd64 build/ws091-amd64/bin/imageview`（wayland も同様） | rc 0、imageview の 14 file を compile、warning 0（`build/ws091-p003-build.log`） |
| host | `timeout 600 sh plan/ws091/tests/run-host.sh` | PASS（PNG・JPEG・GIF と PIL の比較、folder、EXIF、view）。image.c の変更後と全 file の変更後の 2 回 |
| guest（QEMU、Venus） | main の `build/ws035-sq/hdd-image.img` の複写（`build/ws091-run/base.img`）を起動し直して、`timeout 2400 sh plan/ws091/tests/imageview-guest.sh build/ws091-shots/p003 install home empty fit next zoom wheel portrait rotate alpha gif pixels broken swipe fullscreen chooser stop` | 全部の expect_log が ok、zdesktop の ERROR 0（`build/ws091-p003-guest.txt`）。画面 `build/ws091-shots/p003/`（19 枚） |
| touch（QEMU、Venus、注入） | main の pen の image `build/main-pen/hdd-image.img`（`plan/ws079/tests/config-amd64-pen.mk`、`CONFIG_INPUT_TEST_INJECT=y`）の複写を `build/ws091-pen-run` で起動し、新しい試験 `timeout 1500 sh plan/ws091/tests/touch-guest.sh build/ws091-shots/touch` | PASS: 2 本指の pinch で scale 0.281 → 0.544（1.94 倍）、1 本指の flick で drag・lift 2942 px/s・lift の後に 181 px 滑って rest、double tap で fit（0.281）と 100 %（1.000）、fit の横の flick で次の画像（03-portrait、index 2）、long press で context menu。zdesktop の ERROR 0（`build/ws091-p003-touch.txt`、画面 `build/ws091-shots/touch/`） |
| boot | `OUTPUT=build/ws091-boot-test timeout 400 bash plan/tools/boot-test.sh build/ws091-run/disk.img`（guest の試験で imageview・wayland・library を入れた disk） | PASS、login prompt（`build/ws091-shots/p003/boot-login.png`） |

- 判定は program 自身の log（`IMAGEVIEW ...`・`ZWL ...`）を SSH で読んだものと画面。console・serial log は読んでいない。
- clang・libcxx を含む image は build していない（config-amd64 は含むため、boot test は guest の disk で行った）。共有の toolchain は触っていない。
- touch の試験の image は main が以前に build した pen の image（2026-09-29 03:00）で、その上に worktree の wayland・imageview・library を入れた。

## 未実施・制限

- 実機（i915、素の 5330 の touch panel）は未実施。上の確認はすべて QEMU（Venus）。
- 目視は発見的な走査で絞った箇所と全 file の通読による。§5 の「すべての `if` の前に注釈と空行」は、既定値の代入の直後にそれを上書きする
  短い `if`（`x = 0; if (...) x = 1;`、規約 §6 の例と同じ形）と、段落の注釈が覆う unbraced の guard の並びは 1 つの段落のまま残した。

## Resume point

完了。WS091 の完了の処理（Phase の directory の削除、試験の `plan/tools/` への移動）は main。
