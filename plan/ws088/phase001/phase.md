<!-- awesome-plan project=zedbsd record=ws088-p001 -->

# ws088-p001: 元の zip の整理（license・source の案内・不要な exe・README）

Status: uncleared（作業は済み。ユーザーの確認 2 点待ち: fork の commit と zip の中身）
Disposition: normal
Parent: [WS088](../ws.md)
Queue: main の依頼（2026-09-29、worktree `wt/ws088`）
Approval: ユーザー了承済み（main 経由）: `qemu-built.exe`・`qemu-system-x86_64w.exe` を外す、README.txt の Linux の例を直す、license を揃える。
Release への upload・GitHub への書き込み・push はしない（p002、ユーザーの承認の後に main）。

## 範囲と受け入れ

- 元の zip（`~/Kei-nightly.zip`、SHA-256 `2a66519e0f9153b50600cb5f9703d084e6536f4e51f0eb3c3638fa0e976a3ca0`、108,841,861 byte）から、
  image を含まない base の zip を作る。`data/hdd-image.img` は入れない。
- 使わない exe（`qemu-built.exe`・`qemu-system-x86_64w.exe`）を外す。
- README.txt の Linux の例の誤り（`C:\Work\2hdd-image.img`、pflash の行の `\` 抜け）を直す。
- `LICENSES/`（各 license の本文）と `THIRD-PARTY.txt`（component・版・license・source の入手先）を足す。
- 同じ入力から同じ zip ができる手順を repository に置き、SHA-256 を記録する。
- ユーザーが中身を確認する。

## 作ったもの

| file | 内容 |
| --- | --- |
| `tools/release/make-kei-nightly-base.py` | base の zip を作る。元の zip の SHA-256 を確かめ、pin した入力を取得・検証し（無ければ download）、2 つの exe を外し、README を差し替え、THIRD-PARTY.txt と LICENSES/ を足す。同梱の MSYS2 の DLL 19 個が pin した package の DLL と byte 単位で一致することを毎回確かめる。entry は名前順、元の entry は元の時刻、足した entry は 2026-09-29 00:00、deflate 9。fork の commit は `--qemu-commit`・`--virglrenderer-commit`（40 桁）で入れ、無いときは `--draft` でだけ作れる |
| `tools/release/kei-nightly/inputs.txt` | pin した入力（名前・SHA-256・URL）: QEMU 11.0.0 の source release、virglrenderer の fork の COPYING（e80354b8）、MSYS2 UCRT64 の package 16 個 |
| `tools/release/kei-nightly/licenses.txt` | `LICENSES/` に写す license の本文（45 file。これに winq-emu の README を足して 46）と、その出どころ（入力と member） |
| `tools/release/kei-nightly/README.txt` | zip の README（Linux の例を直し、fork と license の節を足した。zip では CRLF） |
| `tools/release/kei-nightly/THIRD-PARTY.txt` | component・file・版・license・source（`@QEMU_COMMIT@`・`@VIRGLRENDERER_COMMIT@` を script が置き換える） |
| `tools/release/kei-nightly/winq-emu-license.txt` | `winq-emu.ico` の出どころ（cmspam/winq-emu d1408884、README が launcher・installer を MIT と書く）と MIT の本文 |

## 調べたこと（2026-09-29）

- 同梱の DLL の出どころ: MSYS2 の UCRT64 の package を `repo.msys2.org` から取り、DLL の SHA-256 で照合した。19 個すべてが一致:
  SDL2 2.32.10-1、bzip2 1.0.8-3、libepoxy 1.5.10-7、dtc 1.7.2-3（libfdt）、libffi 3.5.2-1、gcc-libs 15.2.0-14（libgcc_s_seh）、glib2 2.88.0-1（4 個）、
  libiconv 1.19-1、gettext-runtime 1.0-1（libintl）、ncurses 6.6-4、pcre2 10.47-1、pixman 0.46.4-1、libslirp 4.9.1-2、libwinpthread 14.0.0.r14.g4761eabdd-1、
  zstd 1.5.7-1、zlib 1.3.2-2。どの版の source package も `repo.msys2.org/mingw/sources/` にある（gettext-runtime は `mingw-w64-gettext-1.0-1`、
  libwinpthread は `mingw-w64-winpthreads-…`）。
- `share/` の firmware・keymaps と `data/edk2-x86_64-code.fd` は MSYS2 の `qemu-11.0.0-1`（= QEMU 11.0.0 の release の pc-bios/）と SHA-256 が一致。
  違うのは `share/firmware/*.json`（path が `C:/msys64/ucrt64/share//` の source build の install）と `share/keymaps/meson.build`（build の file）だけで、
  source build（fork）の install の出力と分かる。firmware の source は `qemu-11.0.0.tar.xz` の `roms/` にある（SHA-256 `c04ca360…`）。
- `data/ovmf-vars.fd` は template の `edk2-i386-vars.fd` と違い、使った後の変数の store（`Boot0000`〜`0003`、`Grub Bootloader`、`UEFI QEMU NVMe Ctrl kei-boot 1` 等）。
  個人の情報らしいものは見当たらない。変えていない（範囲外。報告で提案）。
- fork の binary: `qemu-system-x86_64.exe`（2026-09-29 17:09、GCC 16.2.0）・`libvirglrenderer-1.dll`（2026-09-29 18:00、GCC 16.2.0）・`qemu-img.exe`
  （2026-04-27、GCC 15.2.0）は MSYS2 の package と一致しない（fork の build）。GitHub の `awemorris/qemu-win32-vulkan` の alpha10 は `4dd4da5f`、
  `awemorris/virglrenderer` の alpha10 は `e80354b8`（どちらも upstream の cmspam の alpha10 と同じ commit）。一方 WS085 の記録（ws085-p001）では、
  この 2 つは `vendor/` の fork に WS085 の変更（usb-multitouch、mapped blob scanout、proxy の修正）を入れた build で、vendor の commit・push は
  ユーザーが行う。**配布する binary に対応する source（commit）が公開されているかは未確認**。THIRD-PARTY.txt は repository の URL を書き、
  commit は置き換えの印のまま（draft では「not yet confirmed」）。
- `winq-emu.ico` は cmspam/winq-emu の `launcher/winq-emu.ico` と SHA-256 が一致。QEMU の exe はこの file を読まない（icon は exe の resource）。
- boot.ps1 は `-L share` と `data/` の 3 file と exe・DLL だけを使う。外した 2 つの exe は参照されない。

## 確認

| 確認 | 結果 |
| --- | --- |
| `python3 tools/release/make-kei-nightly-base.py --original ~/Kei-nightly.zip --cache build/ws088/cache --output build/ws088/kei-nightly-base-winq-a10-1.zip --draft` | 成功（約 23 秒）。DLL 19 個の照合 PASS |
| 同じ入力で 2 回作って `cmp` | 一致（再現可能） |
| `unzip -tq` | No errors |
| 元の zip との差（python の zipfile で全 entry を比較） | 外した: `qemu-built.exe`・`qemu-system-x86_64w.exe`。変わった: `README.txt` だけ。足した: `THIRD-PARTY.txt` と `LICENSES/`（file 46・dir 33）。`data/hdd-image.img` は無い |
| Windows での起動 | 未実施（ユーザー） |

成果物（draft、fork の commit は未記入）: `build/ws088/kei-nightly-base-winq-a10-1.zip`（worktree の中）、56,342,849 byte、
SHA-256 `81120981aabc80de6dbdc526b7dfa314ee84a8041a1c3b137081e078a81f665c`。entry 211（file 173・dir 38）。元は entry 133（file 128）で、file 2 を外し 47 を足した。
**この draft は upload しない。** commit を入れて作り直すと SHA-256 は変わる。

## 残り・再開の条件

1. ユーザーが、配布する `qemu-system-x86_64.exe`・`qemu-img.exe`・`libvirglrenderer-1.dll` を build した fork の commit を公開（push）し、その 40 桁の
   commit を main に伝える（qemu-img.exe が別の commit なら THIRD-PARTY.txt の書き方を変える）。
2. `--qemu-commit <sha> --virglrenderer-commit <sha>` で作り直し、SHA-256 を記録する。
3. ユーザーが zip の中身（README・THIRD-PARTY・LICENSES）を確認する。→ p001 cleared、p002（upload）へ。

## Resume point

draft の base の zip まで完了。fork の commit（上の 1）とユーザーの確認を待つ。再開は 2 の command から。
