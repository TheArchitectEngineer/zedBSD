<!-- awesome-plan project=zedbsd record=ws074p001 -->

# ws074-p001: 全体の設計（構成、各 module、共通の実行 engine、試験、段階の目標、Phase の分割）

Phase ID: `ws074-p001`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は統合する main の session）

## 範囲

ユーザーの指示（[ws.md](../ws.md) の「目標」）から、process・thread と module の構成、HTML、DOM と GC、CSS、layout、text と font、
描画と窓、network（TLS は OpenSSL）、画像（libjpeg-compat を含む）、JS と Wasm の共通の実行 engine、試験の戦略（suite の取得と
ライセンス、host と guest、Chrome との比較）、段階の目標値、Phase の分割を決める。コードは変えない。Chrome（Chromium）の host への
導入は、簡単なら行う。

## 受け入れ

1. [design.md](../design.md) に上の全部があり、人間の判断が要る点が戻せる既定と共に §17 にある。
2. 段階の目標が具体的な数である（§15）。
3. ws.md の Phase の表が 1〜3 時間の大きさの Phase に分かれ、依存が書いてある。
4. worktree で build の準備ができる（`make sysroots`）。

## 結果（2026-09-27）

cleared。

- [design.md](../design.md): 構成（§1: 1 process、main thread と resolver の thread、タブ = agent、headless の mode）、source の
  置き場所（§2: `userland/desktop/browser/` の下に module ごとの directory、依存は下向き）、HTML（§3）、DOM は GC の cell で
  JS の object を兼ねる（§4）、CSS（§5、`-webkit-` の表）、layout（§6: 1/64 px の固定小数、formatting context ごとの file）、text と
  font（§7: libtruetype への関数の追加が要る）、描画と窓（§8: CPU の rasterizer、wl_shm、CONTROLS → TABS）、network（§9: OpenSSL を
  `dlopen`）、画像（§10: libjpeg-compat は IJG の古典的な API の部分集合）、共通の実行 engine（§11: JSC の方式の NaN-boxing、非移動の
  mark-sweep と保守的な stack の走査、register の bytecode に JS の動的な命令と Wasm の型付きの命令、Wasm は validation で bytecode へ
  変換、同じ frame の配置）、JS（§12、binding は自前の WebIDL から build のときに生成）、Wasm（§13）、試験（§14）、段階の目標値
  M1〜M4（§15）、後回し（§16）、判断が要る点 D1〜D11（§17）、Phase の分割（§18、ws.md の表 p002〜p044）。
- 調べた事実:
  - zdesktop の global に `wl_shm` がある（ws071 design の調査）。libkeiland の titlebar の API は TABS の関数（`add_tab` 等）まで
    header にある（compositor の側は ws070-p011）。CONTROLS と TABS は排他（titlebar-spec §5）。
  - libtruetype の API は整数の pixel の大きさと整数の advance だけで、kerning・名前の表・`unitsPerEm` が無い（browser には足りない）。
  - OpenSSL は package（`userland/packages/security/openssl`、3.5.8、`/usr/lib/libssl.so`・`libcrypto.so`、zlib 無し）、CA は
    ca-certificates の `/etc/ssl/cert.pem`（OpenSSL の既定）。libc に `dlopen` がある。base の既存の `fetch` は block する簡単な
    HTTP/1.1 の client（再利用しない）。
  - 基本の program は `-std` を指定しない clang（規約は ANSI C の書き方）、`-Os`、`-Werror`。host の試験は files と同じく
    同じ source を host の cc で build する方式が使える。
  - guest は QEMU の user network（host は 10.0.2.2）で、host の test server と外の site に届く。
  - host に Debian の `wabt`（wast2json）と `nodejs` の package が入れられる（未導入。wabt は p033 で入れる）。
- **Chromium を host に入れた**: `sudo apt-get install chromium fonts-dejavu-core`（Debian 13、chromium 153.0.8010.52-1~deb13u1）。
  `plan/ws074/tests/chrome-shot.sh` で headless の screenshot（320×200、device scale 1）を撮り、緑の div と白の背景の画素を
  確かめた（`build/ws074-chrome/t.png`、(0,128,0) と (255,255,255)）。
- `make sysroots` が通った（worktree の `build/llvm` は `build/llvm-zedbsd8` への symlink）。

## 判断が要る点

design.md §17 の D1〜D11。既定で進める。main へ報告した（2026-09-27）。
