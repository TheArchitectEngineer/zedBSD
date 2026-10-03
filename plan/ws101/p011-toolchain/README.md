# ws101-p011: toolchain の差分（Noct の OpenGL ES の accel を zedBSD の `/bin/noct` で有効にする）

D2（ユーザーの許可 2026-09-30、構成で選ぶ形）の実装の案。**WS101 は toolchain を変えず、build もしていない。** main がこの手順で当てる。

## file

| file | 当てる先 | 内容 |
| --- | --- | --- |
| `0004-accel-opengles-on-zedbsd.patch` | `userland/base/noct/patches/`（新しい file として置く） | Noct の `CMakeLists.txt` の OpenGL ES の backend の条件 2 か所（source の選択と `NOCT_ACCEL_BACKEND_OPENGLES` の定義）に `OR NOCT_TARGET_ZEDBSD` を足す。link の枝（`CMAKE_SYSTEM_NAME` が `Linux`・`FreeBSD` の pkg-config）は zedBSD（`CMAKE_SYSTEM_NAME zedBSD`）では入らないので変えない。検証済みの tarball に 0001〜0003 を当てた `CMakeLists.txt` から `diff -u` で作った |
| `toolchain.diff` | repo の root で `git apply` | `userland/base/noct/Makefile`・`version.mk`・`zedbsd.cmake` |
| `config.diff` | repo の root で `git apply` | `plan/ws075/demo/config-demo-hdmi.mk` と `config/ci/config-amd64.mk` に `ZEDBSD_NOCT_ACCEL := y`、`config/rootfs-options.list` に menuconfig の行（既定 n、amd64。要らなければこの hunk は外してよい） |

sha256（作った時点）:

```
b989cd1bd7c8ba9c1a4ce155c276c3b424b35fe17e3df6b8837e5ad1a969bd9f  0004-accel-opengles-on-zedbsd.patch
2e9ad008fd7b9d1fa5e745648e629137dd06f66aa8071ede51fb1687939430d0  toolchain.diff
a79841dfbefc05731f703bf80e0a7d121e516def0792daaa7a5e945dd22a5f91  config.diff
```

## 形（共有の host の Noct を作り直さない）

- **target だけの patch の一覧と level を分ける。** `ZEDBSD_NOCT_TARGET_PATCHES` = 今の `ZEDBSD_NOCT_PATCHES`（0001〜0003）+ 0004。
  `version.mk` に `ZEDBSD_NOCT_TARGET_PATCH_LEVEL := $(ZEDBSD_NOCT_PATCH_LEVEL)-t1`（= `zedbsd12-t1`）。
- target の tree（`userland/base/noct/noct`、checkout ごと、git の外）の stamp・identity・verify・build の stamp は target の level を使う。
  **host の tree（`build/NoctLang`）の stamp・identity・build の stamp（`build/host-noct-state/built-2.0.1-zedbsd12-process`）は変わらない**ので、
  host の Noct は展開も build もやり直されない。
- 展開の macro（`ZEDBSD_NOCT_EXTRACT_SOURCE`）は tree・patch の一覧・level・`replace` か `keep` を引数に取る。**target の tree は、
  同じ release（identity の commit が同じ）の別の level の tree なら消して展開し直す**（`replace`）。他の worktree が main を merge
  した後、手で消さなくても次の build で target の tree が 0004 付きになる（Noct の target の build が 1 回やり直しになる）。host の tree は今までどおり
  置き換えない（`keep`）。identity の無い tree、別の release の tree は今までどおり「refusing to replace」で止まる。
- `ZEDBSD_NOCT_ACCEL := y` の amd64 の構成だけ accel を ON（`NOCT_ZEDBSD_ACCEL`）。cmake には毎回 `-DNOCT_ENABLE_ACCEL=ON|OFF` を渡す（cache に
  前の ON を残さない）。build の stamp は accel のとき `-accel` が付き、`$(BUILD)/dynamic/libEGL.so`・`libGLESv2.so` に依存する。
  i386・arm64 と、`ZEDBSD_NOCT_ACCEL` を置かない構成の `/bin/noct` は今のまま（0004 は入るが accel は OFF。OFF では 0004 の条件は効かない）。
- `zedbsd.cmake` は `NOCT_ENABLE_ACCEL` のとき、build の `libEGL.so`・`libGLESv2.so` を `noctcli` に link し、`LINK_DEPENDS` にも足す
  （無ければ configure の error）。accel の `/bin/noct` は libEGL・libGLESv2 を DT_NEEDED に持つので、それらの無い image では起動しない。
  `ZEDBSD_NOCT_ACCEL := y` は libegl・libglesv2 を選ぶ構成（demo と CI の amd64）にだけ置いた。
- `noct-target-clean`（target の tree だけを消す）を足した。`noct-clean` は今までどおり host の tree も消すので使わないこと。

## 当てる手順（main）

toolchain の lock（`plan/tools/toolchain-lock.sh`）の対象は `build/llvm*` と `build/NoctLang` で、この変更はそれらに書かない（source の file と
checkout の中の target の tree だけ）。lock を外す必要は無い見込み。外す運用なら、下の 1〜4 の間だけ外す。

```sh
# 1. 差分を当てる（repo の root）
cp plan/ws101/p011-toolchain/0004-accel-opengles-on-zedbsd.patch userland/base/noct/patches/
git apply plan/ws101/p011-toolchain/toolchain.diff plan/ws101/p011-toolchain/config.diff

# 2. host の Noct が変わらないこと（stamp が同じ、何も作り直さない）
ls build/host-noct-state/built-2.0.1-zedbsd12-process build/NoctLang/.zedbsd-source-2.0.1-zedbsd12
make -n ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/noct 2>&1 | grep -c 'build/NoctLang' # 0 のはず

# 3. target の build（accel ON）。target の tree は zedbsd12-t1 に展開し直され（「replacing the source tree of another
#    patch level」と出る）、noct は libEGL・libGLESv2 を link する
make ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/noct
#    （demo の image なら plan/ws075/demo/build-demo-image.sh でよい）

# 4. 確かめる
cat userland/base/noct/noct/.zedbsd-source-identity             # patch-level=zedbsd12-t1
grep -c 'OR NOCT_TARGET_ZEDBSD)' userland/base/noct/noct/CMakeLists.txt   # 2
ls userland/base/noct/noct/build-zedbsd-amd64/.zedbsd-built-2.0.1-zedbsd12-t1-accel
build/llvm/bin/llvm-readelf -d build/amd64/bin/noct | grep NEEDED # libEGL.so と libGLESv2.so がある
build/llvm/bin/llvm-nm -D --undefined-only build/amd64/bin/noct | grep -c 'glDispatchCompute\|eglGetPlatformDisplay'   # 2 以上
```

5. accel の無い構成が変わらないこと（例: `ZEDBSD_CONFIG=config/ci/config-pcat.mk` の `/bin/noct` は `-DNOCT_ENABLE_ACCEL=OFF`、
   stamp は `-accel` なし）。`make -p -n ... | grep NOCT_ZEDBSD_CMAKE_OPTIONS` で見られる。

うまくいかなかったとき: 1 は `git apply -R` と 0004 の削除で戻る。target の tree は次の build で元の level に展開し直される（`replace`）。

## 作った側の確認（WS101、toolchain は変えていない）

- 検証済みの tarball を scratch に展開し、0001〜0004 を `patch --batch --forward --fuzz=0 -p1` で当てた: 全て当たる。
- `git apply --check` で 2 つの差分が今の tree に当たる。
- 使い捨ての `git worktree`（scratch、共有の `build/` を持たない）に当て、
  - `make -n -p ZEDBSD_CONFIG=config/ci/config-amd64.mk`: `NOCT_ZEDBSD_ACCEL := y`、`-DNOCT_ENABLE_ACCEL=ON`、target の stamp は
    `zedbsd12-t1`（build の stamp は `-t1-accel`）、**host の stamp は `zedbsd12` のまま**。
  - `config/ci/config-pcat.mk`: `-DNOCT_ENABLE_ACCEL=OFF`、stamp に `-accel` なし。
  - `make noct-target-source-verify`（target の tree の展開と manifest の検査だけ。Noct の build はしていない）: `zedbsd12-t1` で展開し、
    `OR NOCT_TARGET_ZEDBSD)` が 2 か所。identity を `zedbsd12` に書き換えた tree は「replacing the source tree of another patch level」と出て
    置き換えられた。
  - 使い捨ての worktree は消した。
- accel の source（`src/accel/*.c` の common と `accel_opengles.c`）を zedBSD の target の clang（`build/llvm-zedbsd8`）で `-fsyntax-only`
  （`NOCT_USE_ACCEL`・`NOCT_ACCEL_BACKEND_OPENGLES`、zedBSD の sysroot の header）: error なし（build の一覧に入らない dx12・vulkan だけ header が無い）。
- **未確認**: link（main の build で分かる）、実行。
