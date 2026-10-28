<!-- awesome-plan project=zedbsd record=ws307-p003 -->

# ws307-p003 build: `make installer-image`、manifest、unit、index、live の tree、installer の image

- 親: [WS307](../ws.md)。仕様: [spec.md](../spec.md) §6・§11.2。Queue: q1665 / q1665-i02（担当 P4、2026-10-29、worktree p4-ws307f）。
- Status: in-progress（段 1（選択と file の持ち主の manifest）を commit。段 2 以降は下の「段」）
- 承認: 2026-10-29 ユーザー「P4にWS307」（Q1 の中継）。toolchain（`toolchain/llvm/sysroot.mk`）の編集は Q1 の許可（P4/queue.md、自分の build/ の下だけ）。段 1 は sysroot.mk を変えずに済んだ（下）。

## 段（実装の順）

1. **選択と manifest**（済み、下の「結果」）: `ZEDBSD_IMAGE_PROFILE=installer` の選択、file の持ち主（`$(BUILD)/installer/full.owners`）、manifest（`tools/build/installer-manifest.py`）。
2. `installer-image` の入口: 再帰の `$(MAKE) ZEDBSD_IMAGE_PROFILE=installer`、amd64＋`ZEDBSD_VARIANT=native` 以外と他の image の goal との併用は error、`$(BUILD)/installer/full` の tree（今の root の rule を出力の directory を引数にして使う、`$(BUILD)/rootfs` は使わない）。installer の menu（SELECTABLE n）と、その外の build での error。
3. unit の tarball（ustar＋deflate、0:0、manifest の mode、pax の `x` header か拒み）と boot の file、index（spec §6.3: unit・表示名・group・版・大きさ・file の数・SHA-256・依存、boot の file、set-user-ID の一覧、firmware の license、S7 の項目と unit の対応）。依存は P の requires を unit に写し、循環は拒む。
4. install-admin の `-D` の index の hash、live の tree（manifest の base・desktop・firmware と installer・install-admin・`/opt/installer`・`/etc/keiland/autologin`、試験の hook `ZEDBSD_INSTALLER_TEST_EXTRAS`）、live の tree が「full の部分集合＋足した物」と一致する検査。
5. image（ESP＋ZEDBSD の FAT32＋root＋swap、zedimage-host に PARTUUID を渡すか出させて cfg に `rootpart=PARTUUID=`）、root の大きさの 2 段の量り（16 GiB・65536 inode の上限）、`check-amd64-native-image.py` の installer の形。
6. UFS の inode の密度（p005 の残り、unit の file の数で決める）。

## 結果 段 1（2026-10-29、P4、commit ca677376f）

- `Makefile`: `ZEDBSD_IMAGE_PROFILE=installer` の時、`ZEDBSD_USER_PROGRAMS` を「menu が base・desktop・firmware・packages/* の selectable な program で platform に合う物」に置き換える（依存の展開の前）。CI の config で 282（普通は 254）、tests・x11 は 0。
- `platform/amd64/installer-image.mk`（新規、`platform/amd64/vmunix.mk` が include）: target `installer-owners` → `$(BUILD)/installer/full.owners`。技術の判断 T27: 各 package の Makefile は `ZEDBSD_PACKAGE_FILES += $(if $(filter NAME,$(ZEDBSD_USER_PROGRAMS)),…)` の形で root の一覧に足すので、recipe の展開の時に選択を 1 つの program に絞って一覧を展開し直し（`$(eval override …)`、後で戻す）、空の選択との差をその program の物とする。package の Makefile（約 40）と toolchain の sysroot.mk を変えずに持ち主が付く。block: B（空の選択）、R（root の rule の `$(AMD64_ARCH_FILES)`: command・account・data・runtime）、P（program ごと: menu・path・class・mode・requires、C command、D data、W 一覧の語）。`$(file)` で書くので command line の長さの上限に当たらない。
- `tools/build/installer-manifest.py`（新規）: owners と staged の tree から manifest（path・unit・program・f/l・mode）。unit は menu（base・desktop・firmware）と package の path（`network/curl` → `network-curl`）。B と root の rule の固定の file は base、R は program の主張が無い時だけ base、sysroot から写った `usr/include`・`usr/lib` は base（`userland/desktop/include` にある header は desktop）。2 つの program が同じ path を主張したら base を優先、次に他方が依存する方、他は error。拒む物: unit の無い file、tests・X11 の program の file、登録が求めていない set-user-ID・set-group-ID、root に無い `--file`。

## 確かめ 段 1（2026-10-29、P4）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws307-main installer-owners`（1 分 31 秒、package の stage を含む）→ rc 0、254 program の block。`build/ws307-main/rootfs/.stamp` を staged にして manifest の道具を当てた → 残る問題は CI の config が選ぶ tests の program（mview・zgears）の 16 file だけ（installer の選択には入らない、正しく拒んだ）。
- host: `sh plan/ws307/tests/host/installer-manifest-test.sh` → `installer-manifest-test: PASS`（18 checks: base・desktop・package の unit、--subtree・--link・data、desktop の header、2 つの主張で依存される側、mode 4755 の保持、unit 無し・set-user-ID・tests の program の拒み）。
- 未実施: installer の選択（282）での全部の build と manifest（clang など全 package の build が要る）。

## 残り

- 段 2〜6（上）。
