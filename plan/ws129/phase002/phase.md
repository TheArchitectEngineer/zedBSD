<!-- awesome-plan project=zedbsd record=ws129-p002 -->
# ws129-p002: image の license の一覧

Status: in-progress（q668、P2、2026-10-04。一覧・script・host 試験・audit・足りない本文 G1〜G4 の直しは済み。remacs（D1）と i915-old の GPL の file（D2）はユーザーの判断待ち）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q668（Q1 の dispatch、2026-10-04）
目安: 3〜4h

## 範囲

1. release の image（当面は `config/ci/config-amd64.mk` とデモの config の和）に入る全ての component を列挙する: 自作の source（Zlib 等、SPDX の行から）、`userland/packages/` の外部 package（openssh・openssl・curl・zlib・expat・libcxx・clang・ca-certificates・font など）、
   firmware（i915・AX211・RTL8822B）、取り込んだ表（RTL8822B の `.inc`、browser の Unicode/WHATWG）、LLVM の runtime。
2. 各 component の license の本文の場所（image の中の path と source の中の path）を確かめ、足りないものを洗い出す。
3. 一覧を生成する script（`tools/release/license-inventory.py` など、置き場所は p001 と合わせる）: rootfs の staging と package の metadata から一覧（component・版・license・本文の path）を作り、image の `/usr/share/licenses/` に一覧と本文が揃うかを検査する。
4. `plan/tools/packages/audit-licenses.sh` を走らせ、結果を記録する。GPL 系が image に入っていないことを確かめる（範囲外の例外は guardrail の記録だけ）。

## 受け入れ

一覧（`plan/ws129/licenses.md` と生成物）、script の host の試験、audit の結果、足りない本文の一覧（直すのが他の WS・package なら main に依頼）。image に本文を入れる変更は package の Makefile ごとになるので、その差分は main と調整する。

## 所有 path

`plan/ws129/`、新しい script（`tools/release/` の下）。

## 依存

なし。release の config が p004 で決まったら p006 の前に再生成する。

## 未決の判断

なし。


2026-10-02 Q1: `plan/tools/packages/audit-licenses.sh` は openssl・openssh の tarball だけを見ている（ws115-p005 で判明）。この Phase で全外部 package（glib・pcre2・libffi 以降の GTK の依存、Emacs・vim・Python を含む）に広げる。

## 実施（2026-10-04、q668、P2）

- 一覧: [licenses.md](../licenses.md)（要約・足りない本文・判断・audit）、生成物 [licenses-generated.md](../licenses-generated.md)・[licenses-index.txt](../licenses-index.txt)。
- script: `tools/release/license-inventory.py` と `tools/release/license-components.json`（44 の component、image の分は 25）。make に image の中身を聞く
  （`--eval` の断片で、選んだ package・kernel の option・`/usr/share/licenses/` の file・外部の版と archive・source）。`--rootfs` で disk の上も確かめる。
- host 試験 [license-inventory-test.sh](../tests/license-inventory-test.sh) PASS（8 件）。T1 の CI の image の rootfs（`t1-full`）に `--rootfs` で当て、入っている本文は全て在る。
- audit: `plan/tools/packages/audit-licenses.sh` を全 archive に広げた（Q1 の 2026-10-02 の記録の指示）。main の distfiles（29 archive）で all known: yes。
- 残り: G1 zedBSD の LICENSE、G2 libc の regex（TRE の BSD-2・musl の MIT）、G3 i915 の Intel の MIT、G4 libvulkan の LICENSE-PROTOCOL を image に入れる
  （各 package の Makefile、main と持ち主に依頼）。D1 remacs（GPL）はユーザーの判断待ち（Q1 が上げた）。release の config が p004 で決まったら再生成。

2026-10-04（続き）: Q1 の委任で G1〜G4 を直した（licenses.md の「足りない本文」の節）。CI の config から clang・libcxx を除いた rootfs で `--rootfs` の確かめ:
残りは D1 だけ。host 試験の期待を「既知の残り 1 件（remacs）」に直して PASS。
