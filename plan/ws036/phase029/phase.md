<!-- awesome-plan project=zedbsd record=ws036p029 -->

# ws036-p029: LLVM の package が toolchain の source tree を書き換えない

Phase ID: `ws036-p029`
Parent: [WS036](../ws.md)
Status: **cleared**（2026-09-27、WS036 の subagent）
Phase disposition: normal

## 経緯

main session が p026（LLVM の patch level を zedbsd6 → zedbsd7）を main に merge して確かめたとき、既存の tree の build が
`LLVM: extracted source differs from the verified release plus patch`（`toolchain/llvm/llvm.mk`）で止まった。
libcxx と clang の package が、toolchain が展開して manifest で検査する `build/llvm-source` そのものに自分の patch を当てていたため。
patch level が変わると toolchain は tree を展開し直し、検査の記録（`.zedbsd-source-verified-*`）を作り直す。その検査と package の
patch の順序は保証されず（並列の make で package の patch が先に当たりうる）、以前の tree では package の印が残る。
新しい checkout では検査が先に走るので現れず、patch level を上げたときの既存の tree で現れる。main session の依頼で (a) の形に直した。

## 変更

| 所在 | 変更 |
| --- | --- |
| `userland/packages/external.mk` | `ZEDBSD_EXTERNAL_LLVM_SOURCE`・`ZEDBSD_EXTERNAL_LLVM_VERIFIED` と `ZEDBSD_EXTERNAL_LLVM_COPY`: 検査済みの `build/llvm-source` を hard link で写し（領域を使わない）、写しに patch を当てる。GNU patch は変える file を置き換える（書き込まない）ので、toolchain の側の名前は検査済みの内容のまま |
| `userland/packages/devel/libcxx/Makefile` | source は `build/packages/libcxx/src`（写し）。記録は LLVM の版・patch level と libcxx の patch level を名に持つ `.zedbsd-libcxx-source-*`。検査の記録に依存する |
| `userland/packages/lang/clang/Makefile` | 同じく `build/packages/clang/src`。記録 `.zedbsd-clang-source-*` |

## 検証

| 試験 | 結果 |
| --- | --- |
| amd64 の `libcxx` の build（cross、`config/ci/config-amd64.mk`） | 成功（`libc++.so.1`・`libc++abi.so.1`・`libunwind.so.1` を stage） |
| clang の package の写しと patch（`.zedbsd-clang-source-23.1.0-zedbsd7-zedbsd1`） | 成功。lldb の zedBSD の plugin は写しにだけあり、`build/llvm-source` には無い |
| その後の `make llvm-source-verify`（記録を消して再検査） | 成功（toolchain の tree は release＋patch のまま） |
| clang の package 全体の cross build | 未実施（時間が大きい。WS044 の aarch64 の package の Phase で行う） |

既に package の patch が当たった古い `build/llvm-source` を持つ tree は、一度 `make llvm-source`（展開し直し）か、検査済みの tree への置き換えが要る
（main では main session が置き換えた）。
