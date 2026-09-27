<!-- awesome-plan project=zedbsd record=ws074 -->

# WS074: zedBSD の Web ブラウザ（`userland/base/zdesktop-browser`）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（全体の設計）から。着手の時期は main の計画で決める（2026-09-27 時点では未着手）
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

## 完了の条件（案、p001 で確定）

- zdesktop で zdesktop-browser が起動し、URL を開き、HTML・CSS・画像・JavaScript を含む一般的な静的・動的な page を描いて操作できる（段階の目標は p001）。
- CSS の準拠試験と Chrome との描画の比較で、段階ごとに決めた目標値を満たす。
- JavaScript と Wasm が共通の実行 engine の上で動く。
- 変更した source の規約の全文の確認（最後の Phase）。

## 制約と依存

- 新しい code は `plan/coding-style.md` の全文。外部の試験 suite（WPT、html5lib-tests、test262 等）は source tree に取り込まず、取得・検証して使う
  （ライセンスを確認）。Chrome の参照の描画は host で取る（host への導入は sudo で可。未導入）。
- 画面・窓: zdesktop の titlebar（WS070 の CONTROLS・TABS の presentation。browser は TABS mode の最初の本格的な使い手）、glass（zed_glass_v1）。
- 画像: `libpng-compat`・`libz-compat`（WS035 p040・p041 の範囲、WS071 p010）、`libjpeg-compat`（この WS）。
- 関係: WS035 の p029〜p032（Chromium の port の計画）とは別の到達目標（自前の engine）。両方を残す。

## Phase 一覧（案。p001 で分け直す）

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| ws074-p001 | 全体の設計: 構成（process・thread、module の分け方）、HTML の tokenizer・tree builder・DOM、CSS の parser・cascade・computed style、layout（block・inline・text、後で flex・grid・table）、描画（canvas → zdesktop）、network（URL、HTTP/1.1、TLS は OpenSSL、cache、cookie）、画像、JS・Wasm の共通の実行 engine（IR・bytecode・GC・値）、試験の戦略（html5lib-tests・WPT・test262・Wasm spec test の取得と実行、Chrome との描画の比較の仕組み）、段階の目標値、Phase の分割 | planning | — |
| ws074-p002 | HTML5 の tokenizer・tree builder と DOM（html5lib-tests の通過率の目標） | planning | p001 |
| ws074-p003 | CSS の parser・cascade・computed style | planning | p002 |
| ws074-p004 | layout（block・inline・text）と描画、zdesktop の窓（titlebar の TABS・CONTROLS）と操作（scroll、link） | planning | p003、WS070 の TABS |
| ws074-p005 | network: URL、HTTP/1.1、TLS（OpenSSL）、redirect、cache、cookie | planning | p001 |
| ws074-p006 | 画像: `libjpeg-compat`（新）と `libpng-compat` の使用 | planning | p004、WS071 p010 |
| ws074-p007 | JS・Wasm の共通の実行 engine の中核（IR・bytecode・値・GC・呼び出し） | planning | p001 |
| ws074-p008 | JavaScript: parser、bytecode への変換、interpreter、組み込み（test262 の部分集合の目標） | planning | p007 |
| ws074-p009 | DOM の binding、event、JS と DOM の接続 | planning | p004、p008 |
| ws074-p010 | Wasm: 共通の engine の上の decoder・validator・実行（spec test の目標） | planning | p007 |
| ws074-p011 | CSS の準拠の段階の目標（WPT の CSS の部分集合、目標値を段階的に上げる。複数の Phase に分ける） | planning | p004、p009 |
| ws074-p012 | `-webkit-` の拡張と Chrome との描画の比較（host の Chrome の参照と差分、目標値を段階的に） | planning | p011 |
| ws074-p013 | 変更した source の規約の全文との照合と回帰（最後） | planning | 全て |

## 後の WS・Future Work の候補

- base の libssl・libcrypto の互換品（OpenSSL の置き換え）。
- WebP、`libvorbis-compat`（ogg vorbis）と音声、動画（driver の後）、JS・Wasm の JIT。
