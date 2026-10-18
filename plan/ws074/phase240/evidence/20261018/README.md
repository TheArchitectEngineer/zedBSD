# p240 / ws074-q20261018-01（2026-10-18）

Host の部分範囲は cleared。Phase 全体は uncleared（target/GPU の確認と残る flex basis の条件は未実施）。[集計と257 IDの前後](results.json)。

## 修正と確認

`layout/flex.c` で、非置換の preferred-ratio item の自動最小サイズを hypothetical size・line collection の前に決める。従来は縮小が必要な row でしか測らず、base より大きな下限も切り捨てたため、`flex-basis:0` で内容がある箱も幅/高さ0になった。ratio による架空の内容を除いて測り、内容と transferred size の大きい方を preferred main size・main maximum で制限する。cross min/max、content/border-box、definite な percentage にも対応。explicit min と scrollable overflow は従来の経路を維持する。画像などの replaced item は変更しない。

| 確認 | 結果 |
| --- | --- |
| 固定 WPT aspect-ratio 全257 ID、800×600 | 142 PASS/115 FAIL → **149 PASS/108 FAIL**、改善7・後退0、error/timeout0、同じIDを全件照合 |
| 独立のgeometry29例 | 5/29 → **29/29**（百分率、border-box、explicit min、scroll、row/column、basis0） |
| Chromium 153.0.8010.52 | 28例一致、1例に後述の既知差 |
| 負の余白での flex alignment | 5/5 |
| 既存 DOM | 23/23 |
| Acid3 | score100/100、inventory100/100、画素差0/480000 |
| Acid2 | 差873/120000、前の値と同じ。exact PASS とはしない |
| host build / amd64 production build | exit0、両logの `warning:` は0。amd64 の DT_NEEDED/SONAME 検査もexit0 |
| C全文の変更関数review、clang-format19.1.7、diff check | 追加した3関数と呼出し箇所を確認。style-check の既存50候補は50のまま、新しい関数の候補0 |
| record audit | `python3 -I plan/tools/record-audit/status.py` exit0、指摘なし |

改善した固定WPT: `flex-aspect-ratio-{002,004,047,048,052,053,054}.html`。

## コマンド

```sh
BROWSER_HOST_BUILD=build/ws074-20261018 sh plan/ws074/tests/host-build.sh plain
make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/dynamic/libbrowser.so
python3 plan/ws074/tests/run-flex-ratio-minimum-tests.py --program build/ws074-20261018/plain/browser --chromium
python3 plan/ws074/tests/run-flex-negative-justify-tests.py --program build/ws074-20261018/plain/browser
python3 plan/ws074/tests/run-dom-tests.py --program build/ws074-20261018/plain/browser
python3 plan/ws074/tests/run-wpt-reftests.py --program build/ws074-20261018/plain/browser --root css/css-sizing/aspect-ratio --limit 0 --out build/ws074-20261018/aspect-final
python3 plan/ws074/tests/run-acid-tests.py --program build/ws074-20261018/plain/browser --out build/ws074-20261018/acid
python3 plan/tools/style-check.py userland/desktop/libbrowser/layout/flex.c --summary
git diff --check
```

修正前は同じhost buildのbrowser/libbrowser.soを `build/ws074-20261018/before/` に保存。WPT は同じ command の `--program` と `--out` だけを替えた。既存のWPT symlink先が消失していたため、同じ固定commitを再取得し、LICENSE.mdのBSD表示を確認した。ホストのGCC14.2.0、clang-format19.1.7。source・binaryのSHA256はresults.json。build内のlog/画像はdisposable、追跡する証拠はこのdirectoryに保存する。

## 仕様と参照browserの差・残り

`row-content-cross-cap` は height:100px、max-height:40px、ratio1/2、内容の幅100px、basis0。仕様 [Flexbox §4.5](https://www.w3.org/TR/css-flexbox-1/#min-size-auto) の content suggestion の cross maximum による制限を採用し、幅20pxとした。Chromium153はこの条件だけ幅100pxを保つ。height:auto なら両者20pxになる。試験は仕様の期待と参照の違いを別に記録し、一致したとは報告しない。

stretch で cross size が決まる箱（009/045/046）、`flex-basis:content` とautoの区別（021/022）、画像の自動最小サイズ（055）、indefiniteなcross size、intrinsic-size/auto ratioのborder-boxの細部は残る。固定Interop2025の全3,805 IDの再測定は未実施で、この257件の結果を全体の達成率にしない。QEMU・実機・GPU経路も未実施。GitHubへの公開は保留、Master/共有Queueの投影はQ1にpending。
