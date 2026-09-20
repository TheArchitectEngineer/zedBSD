# ws032-p001: 設計固め・版と入手元の確定・ライセンス監査方針

## 目的

後続 Phase が参照する正本を確定する。コードは書かない。

1. 外部設計（`plan/ws032/external-design.md`）を実ツリーと照合して確定する。
2. 3 パッケージ（＋ C++ ランタイム）の版・入手元・サイズ・SHA-256 を**実取得で**確定する。
3. ライセンスを機械監査し、判定と再実行手段を残す。
4. 以降の Phase が使うビルド前提（ホスト toolchain と sysroot）を用意する。

## 触れるもの / 触れないもの

- 触れる: `plan/ws032/**`、`build/distfiles/`（生成物）、`build/llvm`・`build/*/sysroot`（既存
  make 目標による生成）。
- 触れない: ツリー内のソース、`Makefile`、`toolchain/llvm/version.mk`、
  `src/drivers/gpu/i915/`、`plan/ws031/`、`platform/amd64/`。

## 受け入れ

- 3 件の tarball が取得でき、サイズ・SHA-256・アーカイブ安全性が記録されている。
- 各々の同一性が、upstream 公開ハッシュまたは独立系統との照合で裏づけられている
  （裏づけられない項目は「未実施」と明記する）。
- ライセンス監査が再実行可能な script として存在し、判定が記録されている。
- `build/llvm/bin/clang` と `build/amd64/sysroot` が揃い、クロスコンパイルが動く。
