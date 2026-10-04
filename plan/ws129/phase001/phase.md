<!-- awesome-plan project=zedbsd record=ws129-p001 -->
# ws129-p001: ベータ1 の release の定義

Status: in-progress（q672 / q672-i01、P2、2026-10-04。[release.md](../release.md) の案（第 2 版、design-reviewer の review を反映）とユーザーへの質問 U1〜U15 を Q1 に返した。受け入れの判定は Q1）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q672 / q672-i01（P2）
目安: 2h（設計、source は変えない）

## 範囲

`plan/ws129/release.md` に次を書く:

1. 版の付け方の案: 名前（例: `Kei 0.1 Beta 1` と `zedBSD 0.1-BETA1` の関係、OS の名前の「Kei」と「zedBSD」のどちらを前に出すか）、tag の名前（例 `v0.1.0-beta1`）、uname の release/version の形、一つの源の置き場所（Makefile の変数と生成の header/file）。
2. 配布物: `zedbsd-amd64.img.gz`（名前に版を入れるか）、`Kei-nightly.zip` に相当する Windows の QEMU の zip を出すか、Keiland の deb（WS108/WS112）を載せるか、SHA-256 の file、license の一覧。
3. CI の release の設計: 既存の nightly の job を保ち、tag の push（または `workflow_dispatch`）でだけ動く release の job、`prerelease: true` の要否、本文は repository の release notes の file から読む。WS112 の package の job との合流の仕方。
4. release の image の config の方針（`config/ci/config-amd64.mk` との差: Settings・audiod・インストーラ・AX211 の扱い、`ZEDBSD_ROOTFS_DEVELOPMENT` の要否、demo の利用者・自動 login を入れないこと）。
5. 文書の置き場所（release notes・既知の問題・利用の手引き・license の一覧）。
6. 凍結・RC・最終回帰・実機の確認・公開の日程の案と、各 WS の締切（ws005・ws033・ws118・ws119 の実機の Phase を同じ日にまとめる案）。
7. 最終回帰の中身の案（p006）と実機の確認の一覧の案（p007）。

## 受け入れ

release.md とユーザーへの質問の一覧を main に返す。

## 所有 path

`plan/ws129/`。読むだけ: `.github/workflows/ci.yml`、`config/ci/`、`tools/release/`、`Makefile`、`plan/known-bugs.md`、各 WS の ws.md。

## 依存

なし。

## 未決の判断（ユーザーへ）

- 版の名前・tag・OS の名前の出し方。
- 配布物の範囲（Windows の zip、Linux の deb を載せるか）。
- 機能の凍結の日と RC の日。
- release を GitHub で prerelease にするか。

## 結果（q672-i01、2026-10-04、P2）

- [release.md](../release.md): 今の main の事実（§1）、版（§2）、配布物（§3）、release の config（§4）、CI の release の job（§5、別 file の release.yml、rc の build と最終の tag の promote）、文書の置き場所（§6）、日程（§7）、最終回帰と実機（§8）、ユーザーの判断 U1〜U15（§9）。
- design-reviewer の review（高 4・中 6・低 5）を全て反映（§11）。
- 公開の前に要る判断と現存の問題: 固定の password と root の SSH の login が有効な image（U4・U10）、Windows の zip の GPL の source の commit の未記載（今の nightly も同じ、U6）。
- code は変えていない。
