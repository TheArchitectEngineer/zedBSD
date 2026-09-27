<!-- awesome-plan project=zedbsd record=ws074 -->

# WS074: zedBSD の Web ブラウザ（`userland/base/zdesktop-browser`）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（2026-09-27 のユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は統合する main）
Resume point: p001〜p003 cleared（2026-09-27）。次は p004（HTML の tokenizer）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー:「新しいWSを作ります。Webブラウザを作成します。userland/base/zdesktop-browserです。
HTML5のレイアウトエンジンを大まかに作ったあと、JavaScript実行エンジンをあまり最適化にこだわらないで作成し、接続します。そのあと、CSSの互換性を、
標準準拠テストで100%に近づけつつ、-webkitの拡張などはChromeと実際のレンダリング結果を比較しながら100%に近づけていきます。ただし、レイアウトを
100％準拠にするのは無理だし、Chromeと100％互換にするのも無理ですので、目標値を徐々に上げるのがいいと思います。また、JavaScript実行エンジンは、
Wasm実行エンジンと共通化することで、あとでJITコンパイルを導入するときにやりやすくなると思います。libpngは直接使わず、libpng-compatを使います。
JPEGもlibjpeg-compatを作りましょう。WebPはあとで対応します。SSLは、まずはOpenSSLのライブラリでいいですが、最終的には、libsslやlibcryptoの
互換品をbase/に入れます。動画再生はまだドライバがないので後回しでいいです。音声はogg vorbisくらいなら互換ライブラリをlibvorbis-compatとかとして
作れそうですが、これも後回しでいいです。」

zdesktop（Wayland）の上で動く、zedBSD 自前の Web ブラウザ。HTML5 の parser と DOM、CSS、layout と描画、network、画像、JavaScript と
Wasm を base の中に自前で書く（外部の browser engine は取り込まない）。

## 方針（ユーザーの指示から）

1. **順序**: HTML5 の layout engine を大まかに → JavaScript の実行 engine（最適化にこだわらない）を作って接続 → CSS の互換性を上げる。
2. **CSS の互換性**: 標準の準拠試験（WPT の CSS の部分集合など）の通過率を 100% に近づける。`-webkit-` の拡張などは Chrome の実際の描画と
   比べて近づける。100% は無理なので、**目標値を段階的に上げる**（Phase ごとに通過率・一致率の目標を決める）。
3. **JavaScript と Wasm の実行 engine を共通化**（共通の中間表現・bytecode・値の表現・GC・呼び出し規約）し、後の JIT の導入を容易にする。
4. **画像**: PNG は libpng を直接使わず `libpng-compat`（WS035 p041 の範囲、WS071 p010 で作る予定）。JPEG は `libjpeg-compat` を新しく作る。
   WebP は後。
5. **TLS**: 最初は OpenSSL のライブラリ（既存の package）。最終的に base に libssl・libcrypto の互換品を入れる（別の Phase か WS）。
6. **後回し**: 動画（driver が無い）、音声（ogg vorbis なら `libvorbis-compat` で作れそう）、WebP、JIT。

## 完了の条件（p001 で確定）

- zdesktop で zdesktop-browser が起動し、URL を開き、HTML・CSS・画像・JavaScript を含む一般的な静的・動的な page を描いて操作できる
  （使える page の目安は [design.md](design.md) §15）。
- CSS の準拠試験と Chrome との描画の比較で、段階 M4 までの目標値（design.md §15）を満たす。M5 以降の数は後の WS か Phase で決める。
- JavaScript と Wasm が共通の実行 engine（値・GC・bytecode・interpreter・呼び出し規約、design.md §11）の上で動く。
- `libjpeg-compat` が base の library として入り、browser が使う。
- 変更した source の規約の全文の確認（最後の Phase）。

## 制約と依存

- 新しい code は `plan/coding-style.md` の全文。外部の試験 suite（WPT、html5lib-tests、test262、Wasm の spec test）は source tree に
  取り込まず、commit の SHA を固定して取得・検証して使う（ライセンスを確認、design.md §14.2）。Chrome の参照の描画は host の Debian の
  `chromium`（2026-09-27 に導入、153.0.8010.52）。
- 画面・窓: zdesktop の titlebar（WS070 の CONTROLS、ws070-p011 の後は TABS）。提示は wl_shm（design.md §8.4）。
- 画像: `libpng-compat`・`libz-compat`（ws071-p010、ws035-p040）、`libjpeg-compat`（この WS）。
- 関係: WS035 の p029〜p032（Chromium の port の計画）とは別の到達目標（自前の engine）。両方を残す。

## 判断が要る点（既定で進めた）

design.md §17 の D1〜D11（process の構成、TLS の `dlopen`、titlebar、仕様・Unicode の表の commit、GIF の置き場、wl_shm、
libpng-compat の `from_memory`、libtruetype の拡張、libjpeg-compat の API、既定の font、段階の目標値）。どれも戻せる既定で進める。

## Phase 一覧

p001 で分け直した（2026-09-27。p002〜p013 の案は実行前の案だったので、同じ番号を新しい分割に使う）。各 Phase は 1〜3 時間を目標にし、
着手の時に大きすぎれば分ける。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws074-p001](phase001/phase.md) | 全体の設計（[design.md](design.md)）、Chromium の導入、Phase の分割 | cleared | — |
| [ws074-p002](phase002/phase.md) | 骨組み: directory、build の登録（amd64）、`base/`（arena・配列・文字列 buffer・UTF-8/16・hash）、headless の mode の入口、host の build（ASan の組）、suite の取得の script（固定 SHA とライセンスの確認） | cleared | p001 |
| [ws074-p003](phase003/phase.md) | GC heap の核（非移動の mark-sweep、大きさの class の block、保守的な stack の走査、trace）、VM の string と atom | cleared | p002 |
| ws074-p004 | HTML tokenizer（全状態、文字参照の表の生成）、html5lib の tokenizer の runner。目標 ≥ 98% | planned | p003 |
| ws074-p005 | DOM の核（GC の cell の Node・Element・Text・Comment・Document・DocumentType、属性）と tree builder 1（initial〜in body、adoption agency）、html5lib の tree の runner | planned | p004 |
| ws074-p006 | tree builder 2（table・select・template・frameset・foreign content）、fragment parsing、serializer。目標 script-off ≥ 90% | planned | p005 |
| ws074-p007 | CSS の tokenizer と parser（stylesheet・rule・宣言・at-rule） | planned | p003 |
| ws074-p008 | selector の parse と照合、specificity、rule の索引 | planned | p006、p007 |
| ws074-p009 | cascade、property の表（最初の約 60）、computed value、UA stylesheet、継承、`var()`・`calc()`、`<style>`・`style`・file の `<link>` | planned | p008 |
| ws074-p010 | font と text: font の一覧と選択、libtruetype への関数の追加（D8、main に先に伝える）、kerning、fallback、改行、Unicode の表の生成 | planned | p002 |
| ws074-p011 | layout 1: box tree（anonymous box）、block（margin の相殺・幅）、inline（line box・text run・baseline）、`--dump=layout` | planned | p009、p010 |
| ws074-p012 | 描画: display list、stacking の順、rasterizer、背景・border・text、`--render`、WPT の reftest の runner（CSS2 の JS 無し）と Chrome の比較の最初の版。最初の数 | planned | p011 |
| ws074-p013 | layout 2: float・clear、position（relative・absolute・fixed）、overflow と clip、list と marker、replaced の大きさ、単位（em・rem・vw・%） | planned | p012 |
| ws074-p014 | 窓 1: wl_shm の presenter、CONTROLS の titlebar（戻る・進む・再読み込み・URL）、scroll、link（file:）、keyboard、guest の image と画面。**M1 の計測** | planned | p013 |
| ws074-p015 | URL（WHATWG）、`data:`、WPT の urltestdata の runner | planned | p002 |
| ws074-p016 | HTTP/1.1（非同期、持続接続、chunked、redirect）、resolver の thread、loader、cookie、memory の cache、host の test server、guest の http | planned | p014、p015 |
| ws074-p017 | TLS（OpenSSL の `dlopen`、D2）、https、自前の CA の host の server、guest で実在の site | planned | p016 |
| ws074-p018 | encoding: 判定（BOM・HTTP・meta の prescan）、UTF-16・legacy の single-byte、Shift_JIS・EUC-JP・ISO-2022-JP（表の生成、D4） | planned | p006 |
| ws074-p019 | `libjpeg-compat` 1: baseline（huffman、任意の subsampling、restart、grayscale・YCbCr）、library の登録、host の試験（Pillow と比較） | planned | p002 |
| ws074-p020 | `libjpeg-compat` 2: progressive、CMYK/YCCK、`jpeg_save_markers`（EXIF の向き）、`JCS_EXT_BGRA` | planned | p019 |
| ws074-p021 | browser の画像: `<img>`、CSS の背景画像、JPEG・PNG（libpng-compat）・GIF、画像の cache、固有の大きさ | planned | p016、p020、ws071-p010 |
| ws074-p022 | VM の核 2: 値（NaN-boxing）、object と shape、配列の elements、関数、realm の骨組み | planned | p003 |
| ws074-p023 | 共通の bytecode と interpreter、呼び出し規約、例外の unwind、native 関数（手で組んだ JS 型と Wasm 型の命令の試験） | planned | p022 |
| ws074-p024 | JS の lexer と parser（ES2024 の構文 → AST）、test262 の構文の試験（parse だけ） | planned | p023 |
| ws074-p025 | JS の compiler（ES5 の核）、`--js` の shell、test262 の runner。最初の数 | planned | p024 |
| ws074-p026 | 組み込み 1: Object・Function・Array・String・Number（最短の十進表記）・Boolean・Math・Error・JSON | planned | p025 |
| ws074-p027 | RegExp の engine と String の regex の method | planned | p026 |
| ws074-p028 | ES2015 の意味 1: let・const・TDZ、arrow、class、destructuring、spread、template、Symbol、iterator、for-of、Map・Set・Weak* | planned | p026 |
| ws074-p029 | ES2015 の意味 2: generator、Promise と microtask、async・await、Proxy・Reflect、TypedArray・ArrayBuffer・DataView、Date、BigInt | planned | p028 |
| ws074-p030 | WebIDL の binding の生成器、window・document・Node・Element の基本、console、`<script>` の実行、timer、event loop、WPT の testharness の runner | planned | p014、p029 |
| ws074-p031 | event（dispatch・入力）、innerHTML、querySelector、classList、CSSOM の inline style、getComputedStyle、geometry、変更の後の再計算 | planned | p030 |
| ws074-p032 | fetch・XHR（same-origin・CORS）、Location・History、form（control の描画と入力、送信）、localStorage | planned | p017、p031 |
| ws074-p033 | Wasm: decoder・validator・共通 bytecode への compiler、JS API、spec test の runner（wabt の wast2json） | planned | p029 |
| ws074-p034 | Wasm の MVP の後: bulk memory、reference types、multi-value、sign-ext、非 trap の変換、SIMD。**M2 の計測** | planned | p033、p032 |
| ws074-p035 | flexbox | planned | p013 |
| ws074-p036 | CSS の段階 M3-1: WPT CSS2・flexbox・backgrounds・values・selectors を測り、失敗の多い塊を直す | planned | p034、p035 |
| ws074-p037 | table の layout（CSS2 の table、border-collapse） | planned | p036 |
| ws074-p038 | transform（2D）、transition・animation、gradient、box-shadow、角丸の clip、opacity、`@font-face`（TTF/OTF） | planned | p036 |
| ws074-p039 | grid | planned | p036 |
| ws074-p040 | Chrome との比較の拡大（corpus と実在の site の保存、box と画素の指標）、`-webkit-` の表（別名、`-webkit-box`、`-webkit-line-clamp` 等）。**M3 の計測** | planned | p037〜p039 |
| ws074-p041 | shell 2: TABS の titlebar と窓の中の toolbar、System Menu、context menu、履歴、ページ内検索、zoom、view-source | planned | p032、ws070-p011 |
| ws074-p042 | CSS の段階 M4: 失敗の塊から機能を選んで直す | planned | p040 |
| ws074-p043 | JS・Chrome の段階 M4: test262 と比較の失敗の塊を直す。**M4 の計測** | planned | p042 |
| ws074-p044 | 変更した source の規約の全文との照合、fuzz（時間を区切って）、回帰、boot test（最後） | planned | 全て |

## 後の WS・Future Work の候補

- base の libssl・libcrypto の互換品（OpenSSL の置き換え）。
- WebP、`libvorbis-compat`（ogg vorbis）と音声、動画（driver の後）、JS・Wasm の JIT。
