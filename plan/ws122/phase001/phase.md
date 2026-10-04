<!-- awesome-plan project=zedbsd record=ws122-p001 -->

# ws122-p001: 要件・設計、libavcodec の package と license の監査

Status: cleared（2026-10-05 Q1: libavcodec の package（FFmpeg 9.0.2、署名を確かめ、LGPL の構成）が build でき、release の license の gate が rc=0（22e61acc）、player の試験（T1-133）で decode が動く）。以前: in-progress（2026-10-05、P2。package の build まで済み、判定は Q1）
Disposition: normal
Parent: [WS122](../ws.md)
Queue: Q1 の指示（2026-10-05、ベータ1 に libavcodec と簡単な player を入れるユーザーの決定の後）

## 要件（ベータ1、ユーザーの決定 2026-10-05）

- 外部 package `userland/packages/multimedia/libavcodec/`（FFmpeg の libavutil・libswresample・libavcodec・libavformat・libswscale）を image に入れる。
- 簡単な動画 player（開く・再生・停止・シーク）をベータ1（RC 10/13）に入れる。→ [p002](../phase002/phase.md)。
- ベータ2 の libavcodec を外す経路（独自の container の読み込み・GPU の decode）と dlopen の add-in は後の Phase（ws.md の段 2・3）。

## package（`userland/packages/multimedia/libavcodec/Makefile`）

- FFmpeg **9.0.2**（2026-10-05 の最新）。`https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz`、12040788 byte、SHA-256 `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`。署名 `ffmpeg-9.0.2.tar.xz.asc` は FFmpeg release signing key（`FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8`、ffmpeg.org/ffmpeg-devel.asc）で Good signature を確かめた。patch は無い（zedbsd0）。
- configure: `--target-os=none --enable-cross-compile`（zedBSD は FFmpeg の system の一覧に無い。ELF・POSIX threads は probe で見つかる）、cross の道具は `environment.sh` の名前、`--disable-autodetect`（build の machine の library を拾わない）、shared だけ、`--enable-pthreads`、x86 の assembly は nasm（build の machine にある、libjpeg-turbo も使う）。作らない物: encoder・muxer（書かない）、libavfilter・libavdevice、network の protocol（file だけ）、hwaccel（VA-API は無い、GPU の decode は WS083）、program、doc、debug。
- symbol の版: zedBSD の loader は symbol の版を読まないので（libpng・pcre2 と同じ）、version script の版の名前を消し（`VERSION_SCRIPT_POSTPROCESS_CMD` の sed、global は av*・swr*・sws* のまま）、`--disable-symver`。`check-dynamic-elf.py` が版の定義の無いことを確かめる。
- 出来る物（stage）: `libavutil.so.61`（0.99 MB）、`libswresample.so.7`（0.2 MB）、`libavcodec.so.63`（15.6 MB）、`libavformat.so.63`（1.7 MB）、`libswscale.so.10`（2.0 MB）。NEEDED は互いと `libc.so` だけ（libm は libc に含まれる）。image には soname の名前の library と license の本文（`/usr/share/licenses/ffmpeg/COPYING.LGPLv2.1`・`LICENSE.md`）。header と unversioned の link は stage に残し、player はそれで build する（glib と同じ runtime だけの形）。
- libc の直し（build の途中で分かった）: `include/libc/assert.h` に C11・C17 の `static_assert`（`_Static_assert` の別名）が無く、FFmpeg の configure が「C11 の static assertion が無い」で止まった。C 規格の通りに足した（C23 と C++ では定義しない）。worktree の sysroot を作り直して確かめた。

## license の監査

- configure の表示 `License: LGPL version 2.1 or later`（`config.h` の `FFMPEG_LICENSE`）。package の Makefile が configure の後に確かめ、違えば止まる。`--enable-gpl`・`--enable-version3`・`--enable-nonfree` は使わない。
- 外部の library は何も link しない（`--disable-autodetect`）。GPL の部品（x264・x265・postproc など）は入らない。
- LGPL の義務: shared library（利用者が置き換えられる）、license の本文を image に、対応する source は upstream の tarball（patch 無し）と Makefile の configure の option。`tools/release/license-components.json` に `ffmpeg`（LGPL-2.1-or-later、`status: decided`・`decision` に 2026-10-05 のユーザーの決定）を足した。`decided` は license-inventory.py に足した値（`decision` は決定待ちで gate を止める。決定済みは `decided` と決定の出典）。release notes での表示は ws129-p005。
- 特許: H.264・HEVC などの decoder の特許の扱いは記録だけ（ユーザーの決定で入れる）。
- ベータ2 の dlopen の add-in（header を使わず `dlsym` で呼ぶ）の license の扱いは、その Phase で調べて記録する（今の player は普通に dynamic link する。LGPL の library への dynamic link は LGPL の範囲）。

## 確認

- `make ... libavcodec`（clang 抜きの CI の config、worktree の build）exit 0。警告は FFmpeg の source のもの（2083、外部）。`check-dynamic-elf.py` の 2 つの確かめ PASS。
- 未実施: image に入れての動作（p002 の player と一緒に T1）。CI（`.github/workflows/ci.yml`・`release.yml`）の apt に `nasm` が要る（Q1 に依頼）。config（CI・release）に `libavcodec` と player を足すのは Q1 と。

## Q1 の判定（2026-10-05）

libavcodec の package（FFmpeg 9.0.2、署名を確かめ、LGPL の構成）が build でき、release の license の gate が rc=0（22e61acc）、player の試験（T1-133）で decode が動く。**cleared**。実機は未実施。
