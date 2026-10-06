<!-- awesome-plan project=zedbsd record=ws131-p026 -->

# ws131-p026: 公開の header の置き場所と名前の整理（userland/desktop/include/、<keiland/keiland.h>）

Status: in-progress（2026-10-06 q820、P1。host の確認まで済み、QEMU・FreeBSD の native build は T1 に依頼）
Disposition: normal
Parent: [WS131](../ws.md)
Queue: q820（P1）
依存: **ws131-p013・p014 が main に統合済み**（agent/p2 40b242a、[test-queue](../../test-queue.md) の TQ-1 の PASS の後に Q1 が merge）。統合前に始めない。
目安: 3〜4h。実行者は 1 人（他の担当が `userland/desktop/` の header を触っていない時に。WS131 の D8 の単独走行）。

## ユーザーの指示と決定

- 2026-10-04「ヘッダは<keiland/keiland.h>の方がいいです。…keiland/からkeiland-headers/にリネームするのがいいと思います。kui.hは<keiland/ui.h>にして、でもそれは内部実装であり、アプリからは<keiland/keiland.h>だけインクルードすれば足りるようにリファクタをお願いします。waylandヘッダも調べて整理してみてください。」
- 2026-10-04「truetype.hとbrowser.hは悩ましいのですが、Windows版ブラウザを作る計画も持っているので、<keiland/X>としないままにしておきましょう。ただし、今後決定が変わるかもしれないです。」→ **`<truetype.h>`・`<browser.h>` は今の名前のまま**（keiland-headers/ の直下に置く）。決定が変わったら別の Phase で動かす。
- 2026-10-04（議論の後）: 置き場所は `keiland-headers/` ではなく **`userland/desktop/include/`**（desktop の各 library の公開の header をまとめる。install 先 `usr/include/` と同じ形。「コンポーネントとして分離する傾向を強めるより、デスクトップシステムとして一体感のあるソースツリーにする」）。ユーザーの指定:
  「userland/desktop/include/ keiland/keiland.h ... これをincludeすればすべて解決するヘッダ。アプリはこれをincludeするルール。keiland/ui.h, compat.h ... 内部的。アプリに公開しない。compatはあとで消す。wayland/X ... 自前wayland wayland* ... 自前wayland truetype/truetype.h ... 念のためネームスペースを分けてtruetype/を作りましょう。browser/browser.h ... 念のためネームスペースを分けてbrowser/を作りましょう。」
  → 上の「truetype.h・browser.h は今の名前のまま」は **`<truetype/truetype.h>`・`<browser/browser.h>`** に置き換わった（`<keiland/…>` にはしない点は同じ）。
- Wayland の header（議論の結論、2026-10-04）: **Linux・FreeBSD でも自前の header を system より優先する（今の -I の順を保つ）。** header は link する実装と対にする。Keiland は Linux・FreeBSD でも自前の `libwayland-client.so` を使う（WS105 D19）ので、system の header（distro の libwayland の版の ABI と inline の関数）を読むと実装と食い違う。`header-check.sh`・`native-build-audit.py` が system の Wayland の header を読んだら FAIL にするのは意図した設計。system の header を使うのは、library も system の物にする時だけ（D19 を覆す別の判断）。この Phase では Wayland の header の中身と名前を変えない。

## 目標の形

```
userland/desktop/include/                 （desktop の公開の header だけ。sysroot の usr/include/ にそのまま写る）
  keiland/keiland.h      <keiland/keiland.h>     これを include すれば全て解決する。app はこれだけを include する（規則）
  keiland/ui.h           <keiland/ui.h>          内部。keiland.h だけが include（直接なら #error）。app に公開しない
  keiland/compat.h       <keiland/compat.h>      内部。旧名（kui_*・keiland_*）→ 新名の macro。p023 で消す
  wayland/*.h            <wayland/...>           自前の libwayland の実体（今のまま）
  wayland-client.h ほか中継の 7 本、wayland-egl.h、wayland-egl-core.h   自前（今のまま、標準の綴り）
  truetype/truetype.h    <truetype/truetype.h>   libtruetype（名前空間を分ける）
  browser/browser.h      <browser/browser.h>     libbrowser（名前空間を分ける）
userland/desktop/libkeiland/system/kl-system-protocol.h   （公開しない。compositor と libkeiland の内部）
userland/desktop/wallpapers/Lakeside.ppm, Birch-Lake.ppm, README.md   （header の置き場所から出す）
userland/desktop/libwayland/API-PROVENANCE.md             （同上）
```

消すもの: `keiui.h`（旧）、直下の `keiland.h`・`keiland-ui.h`・`truetype.h`・`browser.h`（subdirectory へ移る）、`userland/desktop/include/`（空になる）。

## 手順（上から順に。各段の終わりに build を確かめて `git commit -m WIP`）

前置き: main の最新から作業の worktree を作り、`BUILD=build/ws131-p026` を使う。共有の `build/` を消さない。

### 段 0: 調べ直し（main が進んでいるので）

```sh
git grep -n 'desktop/keiland/' > build/ws131-p026/before-paths.txt
git grep -nE '#include *[<"](keiland|keiui|keiland-ui)\.h[>"]' > build/ws131-p026/before-includes.txt
git grep -n 'kl-system-protocol.h' > build/ws131-p026/before-proto.txt
```

件数を phase.md の「実施」に書く。以下の sed は、これらの一覧に出た file だけに当てる（`plan/history/`・`plan/*/evidence/`・`*.log`・`*.txt` の証拠は**直さない**。履歴だから）。

### 段 1: kl-system-protocol.h を内部へ

```sh
git mv userland/desktop/include/kl-system-protocol.h userland/desktop/libkeiland/system/kl-system-protocol.h
git grep -l 'userland/desktop/include/kl-system-protocol.h' -- userland plan/ws*/tests plan/tools \
  | xargs sed -i 's|userland/desktop/include/kl-system-protocol.h|userland/desktop/libkeiland/system/kl-system-protocol.h|g'
```

確かめ: `git grep -n 'keiland/kl-system-protocol'` が 0。build（段 6 の zedBSD と Linux）。

### 段 2: 壁紙と文書を header の置き場所の外へ

```sh
git mv userland/desktop/wallpapers/Lakeside.ppm   userland/desktop/wallpapers/
git mv userland/desktop/wallpapers/Birch-Lake.ppm userland/desktop/wallpapers/
git mv userland/desktop/wallpapers/README.md      userland/desktop/wallpapers/
git mv userland/desktop/libwayland/API-PROVENANCE.md userland/desktop/libwayland/API-PROVENANCE.md
git grep -l 'desktop/wallpapers' -- . ':!plan/history' ':!plan/*/evidence' \
  | xargs sed -i 's|desktop/wallpapers|desktop/wallpapers|g'
git grep -l 'keiland/wayland/API-PROVENANCE.md' -- . ':!plan/history' \
  | xargs sed -i 's|userland/desktop/libwayland/API-PROVENANCE.md|userland/desktop/libwayland/API-PROVENANCE.md|g'
```

注意: `userland/desktop/wallpapers/` には壁紙を生成する `Makefile`・`generate.py` が既にある。名前が重ならないことを `ls` で確かめる。`plan/ws075/demo/build-demo-image.sh` などの `--file` の元の path がこの sed で直ることを確かめる。

### 段 3: sysroot に写すのを `*.h` だけに（toolchain、Q1 の許可が要る）

`toolchain/llvm/sysroot.mk:93` の `find … -type f ! -name '*~'` を `find … -type f -name '*.h'` にする。toolchain の file なので、**この段の前に Q1 に許可を取り**、`plan/tools/toolchain-lock.sh unlock` → 変更 → `lock`。sysroot は一時の directory に毎回作り直す（sysroot.mk:140-160）ので、古い file は残らない。clang の package は manifest から `%.h` だけを取る（`userland/packages/lang/clang/Makefile:254-261`、BUG-084）ので変更不要。

### 段 4: `userland/desktop/include/` へ移す

```sh
git mv userland/desktop/include userland/desktop/include
mkdir -p userland/desktop/include/keiland userland/desktop/include/truetype userland/desktop/include/browser
git mv userland/desktop/include/keiland/keiland.h    userland/desktop/include/keiland/keiland.h
git mv userland/desktop/include/keiland-ui.h userland/desktop/include/keiland/ui.h
git mv userland/desktop/include/truetype/truetype.h   userland/desktop/include/truetype/truetype.h
git mv userland/desktop/include/browser/browser.h    userland/desktop/include/browser/browser.h
```

path の置き換え（段 1・2 で済んだ物の後なので、残りは全て header の置き場所の参照）:

```sh
git grep -l 'userland/desktop/include/' -- . ':!plan/history' ':!plan/*/evidence' ':!*.log' \
  | xargs sed -i 's|userland/desktop/include/|userland/desktop/include/|g'
git grep -lE -- '-Iuserland/desktop/include( |$)' | xargs sed -i -E 's#-Iuserland/desktop/include( |$)#-Iuserland/desktop/include\1#g'
# 個々の file の path（sed の後、移した 4 本の旧い path が残っていれば）
git grep -l 'desktop/include/\(keiland\|keiland-ui\|truetype\|browser\)\.h' -- . ':!plan/history' ':!plan/*/evidence' \
  | xargs sed -i -e 's|desktop/include/keiland\.h|desktop/include/keiland/keiland.h|g' \
                 -e 's|desktop/include/keiland-ui\.h|desktop/include/keiland/ui.h|g' \
                 -e 's|desktop/include/truetype\.h|desktop/include/truetype/truetype.h|g' \
                 -e 's|desktop/include/browser\.h|desktop/include/browser/browser.h|g'
```

その後、手で直す所（sed では直らない、または意味を変える所）:

- `toolchain/llvm/sysroot.mk:90,93,103,152`: `userland/desktop/include` → `userland/desktop/include`（`userland/desktop/include/%`・`userland/desktop/include/*` の形も）。段 3 と同じ許可の中で。写し先は `usr/include/` の下にそのまま（`keiland/keiland.h` → `usr/include/keiland/keiland.h`）。
- `userland/desktop/keiland-freebsd.mk:172-180`（FreeBSD の公開の表）: `keiland.h`・`keiui.h`・`keiland-ui.h`・`truetype.h` の行を `keiland/keiland.h keiland/ui.h keiland/compat.h truetype/truetype.h` に（install 先は `include/<同じ相対 path>`）。`*_RETIRED` の一覧（古い file を消す規則）に `include/keiland.h include/keiui.h include/keiland-ui.h include/truetype.h` を足す。
- `userland/desktop/libbrowser/Makefile:5,187`: package の header を `userland/desktop/include/browser/browser.h` に（install 先 `usr/include/browser/browser.h`）。
- `userland/desktop/libkeiland/exports.py`・`plan/ws131/tools/rename-map.py`: 読む header の path を `include/keiland/keiland.h`・`include/keiland/ui.h` に。
- `plan/tools/keiland-freebsd/native-build-audit.py:40-41`: install 先の `include/keiui.h` の存在の assert を `include/keiland/keiland.h`・`include/keiland/ui.h` の存在に。`include/keiui.h`・`include/keiland.h` が**無い**ことの assert を足す。
- `plan/tools/keiland-os-boundary/check.sh:54,95` 付近: header の名前の列挙と `<keiland.h>|<keiui.h>` の正規表現を `<keiland/keiland.h>` に。
- `plan/tools/files/host-build.sh:17-19`・`plan/tools/keiui/host-chooser.sh:12`・`plan/tools/keiland-linux/wsi-check.sh:16` と host の試験の script（段 0 の一覧）: header を写す・symlink する所は、`include/` と同じ相対 path（`keiland/keiland.h`・`keiland/ui.h`・`keiland/compat.h`・`truetype/truetype.h`）に置く形に。
- `docs/architecture/keiland.md:35,74`・`userland/desktop/libwayland/README.md:5`: 文の中の path。

### 段 5: include の書き換えと ui.h・compat.h

1. `keiland/keiland.h` の末尾:
   - `#include <keiland-ui.h>` を次に置き換える:
     ```c
     #define KEILAND_INSIDE_KEILAND_H
     #include <keiland/ui.h>
     #undef KEILAND_INSIDE_KEILAND_H
     ```
   - p014 の `#ifndef KL_COMPAT` 〜 `#endif /* KL_COMPAT */` の block（旧 keiland_* → kl_* の macro）を切り出して `keiland/compat.h` へ移し、その場所に次を置く:
     ```c
     #ifndef KL_COMPAT
     #define KEILAND_INSIDE_KEILAND_H
     #include <keiland/compat.h>
     #undef KEILAND_INSIDE_KEILAND_H
     #endif
     ```
2. `keiland/compat.h`: 上で移した block に、旧 `keiui.h` の `#define kui_* kl_*` の行と `#define KUI_VERSION 12U` を足す。guard は `KEILAND_COMPAT_H`。先頭に直接の include を拒む行:
   ```c
   #ifndef KEILAND_INSIDE_KEILAND_H
   #error "include <keiland/keiland.h>, not <keiland/compat.h>"
   #endif
   ```
   comment に「p023 で消す（app が新しい名前に移った後）」と書く。
3. `keiland/ui.h`: guard を `KEILAND_UI_H` のまま、先頭に同じ `#error` の行（`<keiland/ui.h>` の名前で）。
4. `git rm userland/desktop/include/keiui.h`。
5. tree の中の include を書き換える（段 0 の一覧の file だけ。`plan/history` は除く）:
   ```sh
   git grep -lE '#include *<(keiland|keiui)\.h>' -- userland plan/tools plan/ws*/tests \
     | xargs sed -i -E 's#\#include *<(keiland|keiui)\.h>#\#include <keiland/keiland.h>#'
   ```
   truetype と browser:
   ```sh
   git grep -l '#include *<truetype\.h>' -- userland plan/tools plan/ws*/tests | xargs sed -i 's#\#include *<truetype\.h>#\#include <truetype/truetype.h>#'
   git grep -l '#include *<browser\.h>'  -- userland plan/tools plan/ws*/tests | xargs sed -i 's#\#include *<browser\.h>#\#include <browser/browser.h>#'
   ```
   `keiland/keiland.h` の中で truetype.h を include している所があれば `<truetype/truetype.h>` に（段 0 の一覧で確かめる）。
   同じ file に 2 行できた時（`<keiland.h>` と `<keiui.h>` の両方を include していた file）は、2 行目を消す:
   ```sh
   for f in $(git grep -l '#include <keiland/keiland.h>'); do
     awk '!(/^#include <keiland\/keiland.h>/ && seen++)' "$f" > "$f.tmp" && mv "$f.tmp" "$f"; done
   ```
   `#include "keiland.h"` の形が段 0 の一覧にあれば、それも手で `<keiland/keiland.h>` に。
6. 確かめ: `git grep -nE '#include *[<"](keiui|keiland-ui|keiland|truetype|browser)\.h[>"]' -- userland plan/tools plan/ws*/tests` が 0。`git grep -n '<keiland/ui.h>\|<keiland/compat.h>' -- userland` が keiland.h の中だけ。

### 段 6: build と確かめ（各段の後に短く、最後に全部）

- zedBSD: `make BUILD=build/ws131-p026 -j16` の rootfs（CI の config）が exit 0、自前の `warning:` 0。`build/ws131-p026/sysroot*/usr/include/keiland/keiland.h`・`usr/include/truetype/truetype.h`・`usr/include/browser/browser.h`・`usr/include/wayland/wayland-client.h`・`usr/include/wayland-client.h` が在り、`usr/include/keiui.h`・`usr/include/keiland.h`・`usr/include/truetype.h`・`usr/include/browser.h`・`usr/include/kl-system-protocol.h`・`usr/include/wallpapers` が**無い**。
- `nm -D build/ws131-p026/…/libkeiland.so` が p014 の後と同じ（数を phase.md に）。
- Linux: `make keiland-linux`（gcc と clang）exit 0・warning 0、`plan/tools/keiland-linux/elf-check.sh`・`header-check.sh`・`makefile-sync.sh` PASS。
- FreeBSD: `make -f userland/desktop/keiland-freebsd.mk -n` が通る（native build は試験の担当へ）。
- `plan/tools/keiland-os-boundary/check.sh` PASS、`rename-map.py check-keiland`・`check-ui` PASS。
- host の試験（段 0 で header を写していた script を全部）PASS。
- `<keiland/ui.h>` を直接 include する小さな file を一時に作って compile し、`#error` で止まることを確かめる（試験の後に消す）。

### 段 7: 試験の依頼（QEMU は自分で起動しない）

[test-queue](../../test-queue.md) に「TQ-n: ws131-p026」を Q1 に足してもらう。中身は TQ-1 と同じ組（zedBSD の boot-test・textinput-p013・viewers-p008・demo-s8-s9・files-regress・titlebar-p010・menu-p003、Linux の app の PNG、FreeBSD の backend-test）と、FreeBSD の install に `include/keiui.h` が無いこと。

## 実施（2026-10-06、P1）

### 計画からの変更（Q1 の了承 2026-10-06）

- **`keiland/ui.h`・`keiland/compat.h` は作らない。** p023 で `keiland-ui.h` を `keiland.h` に統合し互換の macro も除いたので、公開の header は `include/keiland/keiland.h` 一つ。ユーザーの「アプリは `<keiland/keiland.h>` だけ」（2026-10-04）と「公開の header を一つに」（2026-10-03）の両方を満たす。段 5 の 1〜4（`KEILAND_INSIDE_KEILAND_H`・`#error`）と段 6 の `#error` の確かめは不要になった。
- 段 3 の find を `-name '*.h'` にするのはやめた（Q1: include/libc の Vulkan・EGL の header の license の notice、`LICENSE-API`・`API-PROVENANCE.md` を sysroot に残す）。`toolchain/llvm/sysroot.mk` は path の 3 か所と comment だけ（Q1 の許可で P1 の worktree に当てた、main へは p026 の merge と同時に）。`userland/desktop/include/` には `.h` しか無いので、sysroot に写る物は header だけ。
- 壁紙は今 `.png`（`.ppm` ではない）。`Lakeside.png`・`Birch-Lake.png`・`README.md` を `userland/desktop/wallpapers/` へ。

### 段 0（main 96f3b4056＋p023 の後）

`desktop/keiland/` の参照 471 行、旧い綴りの include 161 行、`kl-system-protocol.h` 43 行（一覧は `build/ws131-p026/before-*.txt`、一時）。

### 段 1・2・4・5

- 段 1: `kl-system-protocol.h` を `libkeiland/system/` へ（include 12 と comment の path）。
- 段 2: 壁紙 3 と `API-PROVENANCE.md` を移し、path を直した（82 file、image を作る script・config を含む）。
- 段 4: `git mv` で `userland/desktop/include/` に（`keiland/keiland.h`・`truetype/truetype.h`・`browser/browser.h`、wayland の header はそのまま）。path の置き換えは `plan/history`・証拠（`evidence`・`*.log`・`*.txt`・`*.json`）を除く 108 file。header を写す試験の script 40 本は `include/` と同じ相対 path（`keiland/keiland.h` など）に置く形に直した。FreeBSD の公開の表は `keiland/keiland.h`・`truetype/truetype.h`、RETIRED に `include/keiland.h`・`include/truetype.h`。`native-build-audit.py` は新しい path があり旧い 4 本が無いことを確かめる。`exports.py` は `include/keiland/keiland.h` を読む（exports.map は同じ）。
- 段 5: `#include <keiland.h>`・`<truetype.h>`・`<browser.h>` を `<keiland/keiland.h>`・`<truetype/truetype.h>`・`<browser/browser.h>` に（118 file、libbrowser の include の行を含む）。comment の中の旧い綴りも直した（`plan/ws131/tools/rename-map.py` と履歴の文を除く）。
- checker: B5 に旧い綴り（`<keiland.h>`・`<truetype.h>`・`<browser.h>`、script の `userland/desktop/keiland`）を足した。B1 は `<keiland/keiland.h>` も見る。
- 取りこぼしの直し: `wayland/Makefile.linux` に `keiland-linux-sync: skip userland/desktop/wayland/shot.c`（ws173-p002 の zedBSD だけの画面の撮影。makefile-sync が前から FAIL だった）。

### 確認（host、2026-10-06）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（zedBSD amd64） | exit 0、自前の warning 0 |
| sysroot | `usr/include/keiland/keiland.h`・`truetype/truetype.h`・`browser/browser.h`・`wayland/wayland-client.h`・`wayland-client.h` がある。直下の `keiland.h`・`keiui.h`・`keiland-ui.h`・`truetype.h`・`browser.h`・`kl-system-protocol.h`、`wallpapers` が無い |
| image 外の 9 本（monitor・keiland-ime・videoplayer・kuidemo・titlebar-probe・menu-probe・popup-probe・wlshm・browser-probe） | exit 0、warning 0 |
| `nm -D --defined-only libkeiland.so` | 370（exports.py の check が一致） |
| `make keiland-linux`（gcc）・`CC=clang` | 両方 exit 0、warning 0 |
| `header-check.sh`・`makefile-sync.sh`・`elf-check.sh`（`make keiland-linux-install` の stage） | PASS（449 sources）・PASS・PASS（24 ELF） |
| FreeBSD: `keiland-freebsd.mk` の `-n install-headers`・`print-sources` | 通る。公開の表と RETIRED は上のとおり |
| `keiland-os-boundary/check.sh` | PASS（B5 を含む） |
| grep: `userland/desktop/keiland` の参照（履歴・証拠を除く）、旧い綴りの `#include` | 0、0 |

## 受け入れ

- 段 6 の全て。tree の中（`plan/history`・証拠を除く）で `userland/desktop/include/` の参照が 0、`userland/desktop/include/` が無い。app の Keiland の include が `<keiland/keiland.h>` だけ（`<keiland/ui.h>`・`<keiland/compat.h>` を app が include しない）。
- QEMU・Linux・FreeBSD の回帰（段 7）PASS。

## 危険と戻し方

- path の sed が `plan/history` や証拠の file に当たると履歴が変わる。sed の前に `git grep -l` の一覧を見て、除く。
- 作業中に main が進むと、新しく足された file が古い path を使う。merge の前に段 0 の grep を流し直す。
- 戻すときは、段ごとの commit を後ろから revert する。

## Wayland の header（調べ、2026-10-04、main 7244b70）

- **中継の header（wrapper）**: `include/`（旧 `keiland/`）の直下の `wayland-client.h`・`wayland-client-core.h`・`wayland-client-protocol.h`・`wayland-util.h`・`xdg-shell-client-protocol.h`・`primary-selection-unstable-v1-client-protocol.h`・`tablet-unstable-v2-client-protocol.h` の 7 本は、中身が `#include <wayland/同じ名前>` だけの 15 行の file。実体（宣言）は `wayland/` の下にある。
- 役目: 普通の Wayland の program は `#include <wayland-client.h>` と書く（Linux の libwayland の綴り）。自前の libwayland の header は `wayland/` の下に置いているので、標準の綴りでも見つかるように直下に中継を置いている。in-tree の app は直下の綴りを使い（`<wayland-client.h>` 40 か所、`<xdg-shell-…>` 23 か所）、libwayland の中は `<wayland/…>` を使う。`wayland-util.h`・`wayland-client-core.h`・`wayland-client-protocol.h` の直下の中継は tree の中に直接の利用者は無いが、移植する外の program（GTK 4 など）が標準の綴りで include するので残す。
- system の libwayland の header は使っていない（-I の順で自前が先。`header-check.sh:20`・`native-build-audit.py:21,34` が監視）。名前は system の物と同じなので、-I の順に頼っている点は残る（この Phase の範囲の外）。
- 結論: この Phase では Wayland の header の中身と名前を変えない。置き場所の改名で path が変わるだけ。`input-method`・`text-input`・`virtual-keyboard` には中継が無い（`<wayland/…>` だけ）が、利用者は ime と libkeiland の中だけなので足さない。
