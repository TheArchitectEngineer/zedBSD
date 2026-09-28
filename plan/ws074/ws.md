<!-- awesome-plan project=zedbsd record=ws074 -->

# WS074: zedBSD の Web ブラウザ（`userland/desktop/browser`）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし（2026-09-27 のユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は統合する main）
Resume point: p001〜p005・p007・p010〜p017・p019〜p021・p022〜p026・p050・p052・p053・p058・p030・p045・p046・p048・p049・p051 cleared。p019・p020・p051・p021・p053・p052・p050・p058・p054・p055 cleared（2026-09-28）。次は p056 → p057（部品化、2026-09-28 main が番号を割り当て）。p017 で BUG-083（rtld の dlopen が /usr/lib を探さない）を直した。p016 を 2026-09-28 に同期の HTTP（p016）と非同期の loader（p050）に分けた。p013 を 2026-09-28 に p013（position）・p048（float）・p049（overflow と clip）に分けた。p030 の依存は p014・p046 に縮めた（理由は p030 の phase.md、WPT の runner は p047 へ）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー:「新しいWSを作ります。Webブラウザを作成します。userland/desktop/browserです。
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

- zdesktop で browser が起動し、URL を開き、HTML・CSS・画像・JavaScript を含む一般的な静的・動的な page を描いて操作できる
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
2026-09-28 ユーザー:「ブラウザは、現在の方法で進めてください。文字の表は、生成した表をコミットしていいてす。」→ D1〜D3・D5・D9〜D11 は
今の既定のまま。D4 は生成した表を commit する（2026-09-28 に切り替えた、p030 の phase.md）。

## Phase 一覧

p001 で分け直した（2026-09-27。p002〜p013 の案は実行前の案だったので、同じ番号を新しい分割に使う）。各 Phase は 1〜3 時間を目標にし、
着手の時に大きすぎれば分ける。

**実行の順**（2026-09-27 ユーザー「正常系でワンパス通すのを優先する」、design.md §18）: p005 → p007 → p010 → p011 → p012 → p014（窓に
実際の page）→ p045（URL の欄と link）→ p022 → p023 → p024 → p025 → p026 → p046 → p030（JS の接続）→ p013 → p048 → p049 → p015 → p016 → p017 → p019 → p020 → p051 → p021 → p053 → p052 → p050 → p058 → p054 → p055 → p056 → p057 → p006 → p008 →
p009 → p018 → p027 → p028 → p029 → p047 → p031 → p032 → p033 → p034 → p035 以降。各 Phase は最小の範囲で通し、残りは phase.md の「後回し」へ。

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws074-p001](phase001/phase.md) | 全体の設計（[design.md](design.md)）、Chromium の導入、Phase の分割 | cleared | — |
| [ws074-p002](phase002/phase.md) | 骨組み: directory、build の登録（amd64）、`base/`（arena・配列・文字列 buffer・UTF-8/16・hash）、headless の mode の入口、host の build（ASan の組）、suite の取得の script（固定 SHA とライセンスの確認） | cleared | p001 |
| [ws074-p003](phase003/phase.md) | GC heap の核（非移動の mark-sweep、大きさの class の block、保守的な stack の走査、trace）、VM の string と atom | cleared | p002 |
| [ws074-p004](phase004/phase.md) | HTML tokenizer（全状態、文字参照の表の生成）、html5lib の tokenizer の runner。目標 ≥ 98% | cleared | p003 |
| [ws074-p005](phase005/phase.md) | DOM の核（GC の cell の Node・Element・Text・Comment・Document・DocumentType、属性）と tree builder 1（initial〜in body、adoption agency）、html5lib の tree の runner | cleared | p004 |
| ws074-p006 | tree builder 2（table・select・template・frameset・foreign content）、fragment parsing、serializer。目標 script-off ≥ 90% | planned | p005 |
| [ws074-p007](phase007/phase.md) | CSS の最小（ワンパス）: tokenizer・parser、selector（type・class・id・子孫・子・属性の基本）、cascade（origin・specificity・継承）、約 30 の property、UA stylesheet、`<style>`・`style` 属性 | cleared | p003 |
| ws074-p008 | CSS 2（後回し）: selector の残り（構造・状態・`:is`/`:not`/`:has`、pseudo-element）、rule の索引 | planned | p006、p007 |
| ws074-p009 | CSS 3（後回し）: property の表の拡張、`var()`・`calc()`、`@media`、file の `<link>`、shorthand の全部 | planned | p008 |
| [ws074-p010](phase010/phase.md) | font と text の最小: font の一覧（Inter、日本語は Droid の fallback）、libtruetype（関数を足すなら main に先に伝える）、advance、空白と CJK での改行 | cleared | p002 |
| [ws074-p011](phase011/phase.md) | layout の最小: box tree（anonymous box）、block（幅・高さ・margin の基本）、inline（line box・text run・baseline）、`--dump=layout` | cleared | p009、p010 |
| [ws074-p012](phase012/phase.md) | 描画の最小: display list（背景・border の solid・text）、CPU の参照の描画、`--render`（PPM → PNG）、画面の撮影 | cleared | p011 |
| [ws074-p013](phase013/phase.md) | layout 2a: position（relative・absolute・fixed）、offset、z-index の描画の順と hit test（2026-09-28 に float を p048、overflow を p049 へ分けた。list と marker は p011、単位は p007、replaced の大きさは p021） | cleared | p012 |
| [ws074-p048](phase048/phase.md) | layout 2b: float と clear（行の箱を float の横で短くする、block formatting context） | cleared | p013 |
| [ws074-p049](phase049/phase.md) | layout 2c: overflow と clip（display list の clip、CPU と GPU の描画）、overflow が作る block formatting context | cleared | p013 |
| [ws074-p014](phase014/phase.md) | 窓: Wayland と Vulkan（swapchain、display list の GPU の描画: instance の四角と glyph の atlas）、scroll、guest で実際の page を表示。GPU と CPU の描画の比較の試験（2026-09-27 に URL の欄と link を p045 へ分けた） | cleared | p012（p013 は後回しの順） |
| [ws074-p015](phase015/phase.md) | URL（WHATWG）、`data:`、WPT の urltestdata の runner（896/896、data-urls 72/72） | cleared | p002 |
| [ws074-p016](phase016/phase.md) | HTTP/1.1 の同期の client（chunked、redirect）、cookie、http の page と script、host の test server、guest の http（2026-09-28 に非同期の loader・resolver の thread・持続接続・memory の cache を p050 へ分けた） | cleared | p014、p015 |
| [ws074-p017](phase017/phase.md) | TLS（OpenSSL の `dlopen`、D2）、https、自前の CA の host の server、guest で実在の site（BUG-083 の rtld の dlopen の修正を含む） | cleared | p016 |
| ws074-p018 | encoding: 判定（BOM・HTTP・meta の prescan）、UTF-16・legacy の single-byte、Shift_JIS・EUC-JP・ISO-2022-JP（表の生成、D4） | planned | p006 |
| [ws074-p019](phase019/phase.md) | `libjpeg-compat` 1: baseline（huffman、任意の subsampling、restart、grayscale・YCbCr）、library の登録、host の試験（Pillow と比較）。2026-09-28 に base の group・全 platform、libpng-compat の header を `compat/png/` へ | cleared | p002 |
| [ws074-p020](phase020/phase.md) | `libjpeg-compat` 2: progressive、CMYK/YCCK、`jpeg_save_markers`（EXIF の向き）（`JCS_EXT_BGRA` は p019 で済み） | cleared | p019 |
| [ws074-p051](phase051/phase.md) | `libgif-compat`（2026-09-28 ユーザー、D5 の変更）: `userland/base/libgif-compat`、`include/libc/compat/gif_lib.h`（giflib 5.2 の decode の部分集合）、全 platform | cleared | p002 |
| [ws074-p021](phase021/phase.md) | browser の画像: `<img>`（replaced box）、JPEG・PNG・GIF（base の compat の library）、画像の表、CPU と GPU の描画（2026-09-28 に CSS の背景画像を p052 へ分けた） | cleared | p016、p020、p051、ws071-p010 |
| [ws074-p052](phase052/phase.md) | CSS の背景画像（p021 から分けた）: `background-image: url()`、repeat、position、size、`background` の shorthand、canvas の背景画像 | cleared | p021 |
| [ws074-p053](phase053/phase.md) | 部品としての browser の設計（design.md §19）: engine と shell・Wayland の依存の棚卸し、C API（create・destroy・load・resize・呼ぶ側の VkImage への描画・入力の event・callback）の header の案（[browser_view.h](phase053/browser_view.h)）。.so の分割はまだしない。shell の bind・net への直接の依存を page の API に | cleared | p021 |
| [ws074-p022](phase022/phase.md) | VM の核 2: 値（NaN-boxing）、object と shape、配列の elements、関数、realm の骨組み | cleared | p003 |
| [ws074-p023](phase023/phase.md) | 共通の bytecode と interpreter、呼び出し規約、例外の unwind、native 関数（手で組んだ JS 型と Wasm 型の命令の試験） | cleared | p022 |
| [ws074-p024](phase024/phase.md) | JS の lexer と parser（ES2024 の構文 → AST）、test262 の構文の試験（parse だけ。46876/47792） | cleared | p023 |
| [ws074-p025](phase025/phase.md) | JS の compiler（ES5 の核）、`--js` の shell、test262 の runner。最初の数（4992/47792、ES5 1457/8087） | cleared | p024 |
| [ws074-p026](phase026/phase.md) | 組み込み 1a: Object・Function（bind・Function の構築子）・Error の類（engine の誤りも object に）・Boolean・Number（自前の最短の十進表記と十進の読み取り、toFixed 等）・Math・global の関数、native の構築子（2026-09-28 に Array・String・JSON を p046 へ分けた。9327/47792、ES5 4822/8087） | cleared | p025 |
| ws074-p027 | RegExp の engine と String の regex の method | planned | p026 |
| ws074-p028 | ES2015 の意味 1: let・const・TDZ、arrow、class、destructuring、spread、template、Symbol、iterator、for-of、Map・Set・Weak* | planned | p026 |
| ws074-p029 | ES2015 の意味 2: generator、Promise と microtask、async・await、Proxy・Reflect、TypedArray・ArrayBuffer・DataView、Date、BigInt | planned | p028 |
| [ws074-p030](phase030/phase.md) | DOM の binding（interface の表、生成器は後回し）、window・document・Node・Element・Event の基本、console、`<script>` の実行、timer、event loop と microtask の queue（2026-09-28 に WPT の testharness の runner を p047 へ分けた） | cleared | p014、p046（p029 から縮めた: microtask の queue はこの Phase で作った） |
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
| [ws074-p046](phase046/phase.md) | 組み込み 1b（p026 から分けた）: Array・String（正規表現の要らない method、UCD 16.0.0 から生成する大文字・小文字の表）・JSON（14255/47792、ES5 6786/8087） | cleared | p026 |
| [ws074-p045](phase045/phase.md) | 窓 2（p014 から分けた）: CONTROLS の titlebar の URL の欄、link の click（`file:`）、戻る・進む・再読み込み | cleared | p014 |
| [ws074-p050](phase050/phase.md) | 非同期の loader の核（p016 から分けた）: non-blocking な socket と TLS、resolver の thread、redirect、page の画像と shell の navigation の非同期化、Esc の中止。部品化の手順 4（`page_net_*`、p053）。2026-09-28 に持続接続と cache を p058 へ分けた | cleared | p016、p017、p053 |
| [ws074-p058](phase058/phase.md) | 持続接続と memory の cache（p050 から分けた）: host ごとの接続の pool（6 本まで）、長さ・chunked での応答の終わり、Cache-Control（max-age・no-store）、ETag と If-None-Match の再検証、304 | cleared | p050 |
| [ws074-p054](phase054/phase.md) | 部品化 1（p053 の手順 1）: view の型。scroll・履歴・timer・題名の変化を shell から engine の view へ、shell と main の headless の mode は view の API だけを呼ぶ。headless の settle・CPU の描画・dump も view に（2026-09-28） | cleared | p053、p050 |
| [ws074-p055](phase055/phase.md) | 部品化 2（p053 の手順 2）: GPU の描画を `browser_target` と `_record`・`_draw` の形に（view 内の image view ごとの framebuffer）、present.c は swapchain と同期だけ。`--render-gpu` は engine の offscreen に `_draw` | cleared | p054 |
| ws074-p056 | 部品化 3（p053 の手順 3）: 入力を DOM の key・code・text の形に（evdev の変換は shell）、scroll・link・focus の既定の動作を engine へ | planned | p055 |
| ws074-p057 | 部品化 4（p053 の手順 5）: `libbrowser.so` への分割、公開の header、2 つ目の使い手の試作 | planned | p056 |
| ws074-p047 | WPT の testharness の runner（p030 から分けた: testharness.js は arrow・let・const・class と Promise を使う）、WPT dom/nodes の計測（M2 の目標 ≥ 40%） | planned | p028、p029、p030 |

## 後の WS・Future Work の候補

- base の libssl・libcrypto の互換品（OpenSSL の置き換え）。
- WebP、`libvorbis-compat`（ogg vorbis）と音声、動画（driver の後）、JS・Wasm の JIT。
