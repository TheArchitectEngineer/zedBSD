<!-- awesome-plan project=zedbsd record=ws066p002 -->

# ws066-p002: libc.so などの -Bsymbolic-functions と、base の program の GNU hash（amd64）

Status: cleared（2026-10-05 Q1: T1-170: bin-true の平均 618 µs（≤700）・sh-c 903 µs（≤950）、rtld-many・tls-check・boot PASS、libc の symbol の再配置 515 → 25（QEMU、amd64））。以前: in-progress（実装済み、T1 の測定と回帰待ち）
Disposition: normal
Parent: [WS066](../ws.md)
Queue: Q1（2026-10-05）
依存: [p001](../phase001/phase.md)（設計・受け入れ）
受け入れ（p001、Q1 了承 2026-10-05）: 同じ条件の T1 の `startup-measure.sh` の平均で `/bin/true` ≤ 700 µs、`sh -c :` ≤ 950 µs。dyntest・rtld-many・run-tls-check・boot-test、Terminal と Files の起動が変わらない。
範囲: amd64 だけ（Q1 2026-10-05 の決め (a)。arm64・pcat・pc98・sparcv9 の規則は変えない）。toolchain（clang の driver の既定）は変えない。

## 変えた所（`platform/amd64/vmunix.mk` と新しい list）

1. `libc.so`: `-Bsymbolic-functions --export-dynamic-symbol-list=userland/base/libc/interpose.list`。list は malloc の族と strdup・strndup の 9 個（`userland/base/libc/interpose.list`）。規則の依存に list を足した。
   - 設計（p001）では `--dynamic-list` と書いたが、lld の `--dynamic-list` は `-Bsymbolic` を伴い、data（`environ`・`stdin`・`optind` など）まで libc の中に結んでしまう（copy の再配置を使う program と食い違う）。試しに link して、data の再配置が消えることで分かった。`--export-dynamic-symbol-list` は、list の symbol を置き換え可能に残すだけで、data は今のまま置き換え可能。
2. 他の base の共有 library 15 個（libutil・libwayland-client・libtruetype・libz-compat・libpng-compat・libjpeg-compat・libpdf・libgif-compat・libwayland-egl・libEGL・libGLESv2・libkeiland・libvulkan・libbrowser・libGL）: `-Bsymbolic-functions`（data は置き換え可能のまま）。
3. base の program の link 39 か所（`AMD64_APP_LINK` と各 program の規則）: `--hash-style=sysv` → `gnu`。`dyntest` は loader の SysV の道を試験に残すため sysv のまま。`ld.so` も変えない。
4. `tools/build/check-dynamic-elf.py` は変えていない（application・program の role は `.hash` か `.gnu.hash` のどちらかを求める）。

## 確かめ（host、2026-10-05）

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws140-p002 …/ld.so …/libc.so …/dyntest …/bin/{sh,true,ls,wayland,terminal,files}`: warning 0。`dynamic-userland-check`: PASS。
- libc.so の symbol の再配置: 515 → **25**。残りは data 17 個（`environ`・`stdin`・`optind` ほか、置き換え可能のまま）、`__tls_get_addr`・`__rtld_exports`、libc の中から呼ばれる list の 6 個（malloc・free・calloc・realloc・reallocarray・strdup）。export する関数の数は 1,454 のまま。
- program の hash: `true`・`sh`・`ls`・`wayland` は `.gnu.hash` だけ、`dyntest` は `.hash`。
- `reloc-model.py`（新しい build の object で作った root）の探索の数（前 → 後）:

| program | 探索 | 名前の比較 |
| --- | ---: | ---: |
| `/bin/true` | 517 → 27 | 859 → 27 |
| `/bin/sh` | 639 → 149 | 1,494 → 149 |
| `/bin/ls` | 568 → 78 | 1,256 → 78 |
| `/bin/terminal` | 1,740 → 847 | 4,112 → 847 |
| `/bin/files` | 1,885 → 986 | 4,639 → 986 |
| `/bin/wayland` | 1,772 → 900 | 4,142 → 900 |

- 未実施: arm64・pcat・pc98・sparcv9（範囲の外）。package の program（openssh・clang ほか、clang の driver で link）は変わらない。guest の時間と回帰は T1。

## T1 への依頼

image は、この commit の後の main で作る。

1. `plan/tools/guest/test-image.sh plan/tools/rtld/config-amd64-rtld.mk BUILD`（SSH の image）と guest の起動。BUILD/sysroot の symlink（run-tls-check のため）。
2. `GUEST_RUNTIME=… sh plan/ws066/tests/startup-measure.sh BUILD` → `bin-true` と `sh-c` の平均（受け入れ: 700・950 µs 以下）。
3. `rtld-many.sh BUILD`、`run-tls-check.sh BUILD`（dyntest の HANDLES-200・PLUGIN-TLS も）、guest を止めて `boot-test.sh BUILD/hdd-image.img`。
4. Terminal と Files の起動は、T1-168 と同じ files の image を新しい main で作り直して `files-p002.sh` を流せば足りる（まとめてよい）。

## 結果

T1-170（2026-10-05、QEMU、SSH の image、clang 無し、各 2,000 回の平均）:

| 測った物 | T1-165（前） | T1-170（後） |
| --- | ---: | ---: |
| `true-static` | 517 | 608 |
| image の `/bin/true` | 783 | **618** |
| `sh -c :` | 1,074 | **903** |

- 受け入れ（`/bin/true` ≤ 700、`sh -c :` ≤ 950）を満たした。dyntest・rtld-many・run-tls-check・boot-test・Terminal と Files の起動も PASS（Q1 の判定）。
- **測定の回の間のばらつきが大きい**: 何も変えていない `true-static` が 517 → 608 µs と 91 µs 動いた。回をまたいだ比べは ±100 µs 程度の誤差を含むと見る。**同じ回の中の比べ**では、静的との差は 266 µs（T1-165: 517 対 783）→ **10 µs**（T1-170: 608 対 618）になった。

## WS066 の目標の文の案（2026-10-05、P2。決めは Q1・ユーザー）

今の WS の受け入れの文: 「`sh -c :`・`/bin/true`・`cc t.c -o t` の起動が今より縮み、静的 link との差（0.28 ms）の半分以上を取り戻す（仮の目標）」。

- `/bin/true`: 満たした。同じ回の中で差は 266 → 10 µs で、96% を取り戻した（半分を大きく越える）。
- `sh -c :`: 静的に link した sh を測っていないので、「差の半分」は判定できない。平均は 1,074 → 903 µs（171 µs 減）。元の差 0.28 ms は F-020 の静的 sh の測定（2026-09-26）なので、171 µs はその半分（140 µs）を越えるが、回をまたいだ比べで誤差を含む。
- `cc t.c -o t`: 測れていない（clang 入りの image が無い）。

案:
1. 受け入れの文を「`/bin/true` と `sh -c :` の起動の、静的 link との差を同じ測定の回の中で半分以下にする（`startup-measure.sh` の平均）」に改め、`/bin/true` は T1-170 で満たしたとする。`sh -c :` は、`build-measure.sh` に静的な sh を足して同じ回で 1 度測り、確かめる（小さな追加。T1 の 1 回）。
2. `cc t.c -o t` は WS066 の完了の条件から外し、clang 入りの image が作れる時の p003（ld.so の symbol の値の cache、clang・ld.lld の探索の 24〜29% 減の見込み）の判断に回す（F-017 の続き）。
3. 以後の速さの受け入れは、回をまたいだ値ではなく、同じ回の中の比べ（静的版や前の版を同じ回で測る）で書く。

## Q1 の判定（2026-10-05）

T1-170: bin-true の平均 618 µs（≤700）・sh-c 903 µs（≤950）、rtld-many・tls-check・boot PASS、libc の symbol の再配置 515 → 25（QEMU、amd64）。**cleared**。
