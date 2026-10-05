<!-- awesome-plan project=zedbsd record=ws140 -->

# WS140: ld.so の依存の数・object の数・handle の数を動的に伸ばす

<!-- awesome-plan-current:start -->
Status: completed（2026-10-05、Q1 の判定: p001・p002 は T1-164、p003 は T1-168 で cleared）
Primary Milestone: MG002
Related Milestones: MG006（GTK4 の起動、[ws115-p010](../ws115/phase010/phase.md)）
Objectives: O1
Parent: [Master](../master.md)
Queue: q729（P2）
Resume point: なし（完了）。GTK4 の起動の確認は ws115-p010 の再開の時。
<!-- awesome-plan-current:end -->

## 目標

2026-10-04 ユーザー:「F-071、F-072、F-070、はWSを立てて計画を作り、他の能力が低いセッションで処理できるようにしてください。」
[F-070](../future-work.md): GTK 4.18.6 で `RTLD_NEEDED_MAX`（16）と `RTLD_OBJECT_MAX`（32）を越えた。ld.so（`src/rtld/`）の数の上限を無くし、失敗してよいのは memory が取れない時だけにする。

## 結果

- **上限が無くなった物**: object ごとの依存の数、process の object の数、`dlopen` の handle の数、TLS の module の数と dtv、初期化の順、dlsym の訪問の印（32 bit の bitmask）。ユーザーの U3 の決定で、object ごとの TLSDESC の引数（64）、program header（64、今は `PN_XNUM` だけを拒む）、名前の長さ（64 → path の長さ 256）も。
- **作り**（設計の D1〜D8）:
  - object・handle・TLS module は chunk の列。最初の chunk は bss に置く。chunk は解放しないので要素の address は動かず、lock を取らない読み手（`__tls_get_addr`・`dl_iterate_phdr`）が安全に読める。数は release で公開し、acquire で読む。
  - 依存・program header・TLSDESC の引数は、object の中に 16 個まで持ち、越えたら `tls_map` の表に置く。program header の表は unmap せずに使い直す（`dl_iterate_phdr` が lock なしで読むため）。
  - dtv は `__tls_get_addr` の遅い道で、loader の lock の下で伸ばす。古い dtv は変えずに鎖に残し、thread の終わりにまとめて unmap する。
  - 初期化の順は双方向の list、dlsym は印と generation で訪問を記録する。
  - 失敗は memory が取れない時だけ。その時は今と同じ fatal（U2）。handle の chunk が取れない時だけは、`dlopen` が NULL と `"cannot allocate dynamic-loader handle"` を返す。
- **合わせて直した既存の欠陥**:
  - `dlpi_subs` が TLS を持たない object の unload で増えなかった（U5）。
  - `$ORIGIN` の長い suffix と、長い名前で path の buffer を越えて書けた（2 か所）。
  - 最初の load segment が後の segment の入らない隙間に置かれ、`cannot map shared object segment` で止まりえた（T1-163）。load segment の範囲を先に 1 度に取るようにした（`object_reserve_span`）。
- **数**: ld.so の bss は 217,944 → 94,076 byte。rtld.c の style の違反は 241 → 227（関数ごとに増えた所は無い）。
- **証拠**（QEMU）: T1-164（rtld-many の 16 段、tls-check の動的・静的、dyntest の `HANDLES-200`・`PLUGIN-TLS`、boot-test）、T1-166・168（Files の PDF の縮小表示 `dlopen("libpdf.so")` と files-p002）。実機は未実施。
- **完了の条件の扱い**:
  - 1〜3・5: 満たした。
  - 4（amd64 と arm64 の build）: amd64 だけ。arm64・i386・sparcv9 は未実施（sysroot が要る。Q1 2026-10-05: subagent は sysroot を作らず、amd64 の build と host・guest の試験で判定する）。
  - 6（GTK4 の起動）: この WS の条件の外。[ws115-p010](../ws115/phase010/phase.md) の再開の時に確かめる（手順 1 の定数の引き上げは不要になった）。

## 制限・移管

- 再帰の深さに上限が無い（`load_object`・`initialize_object`・`unload_object_locked`・`lookup_handle_graph`）。小さな stack の thread から深い依存を `dlopen` すると溢れうる（推測）。
- 信号の handler の中で、dtv に入っていない dynamic の TLS module に初めて触ると、loader の lock の取り途中の自分を待ちうる。割り込まれた速い道との組み合わせで、値が分かれることがある。
- 起動の時に 33 個を越える静的な TLS の module（2 つ目の chunk を通る `layout_static_tls`）は試験していない。
- `dl_iterate_phdr` と同時の `dlclose` では、`phnum` と `phdr` を別々に読むので食い違いうる（読むのは map されたままの memory だけで、fault はしない）。
- 試験は [plan/tools/rtld/](../tools/rtld/README.md) に移した（`rtld-many.sh`・`build-many.sh`・`config-amd64-rtld.mk` ほか）。Master の Tools 節への登録は Q1。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | object の chunk の列、依存の可変長、初期化の順の list、dlsym の印、U3（TLSDESC・program header・名前の長さ）、U5、多数の依存の試験 | cleared（T1-164） |
| p002 | handle と TLS module の chunk の列、dtv を伸ばす、静的な TLS の並び、dyntest の書き直し、TLS と handle の試験、segment の範囲の予約（T1-163 の直し） | cleared（T1-164） |
| p003 | 全文規約の見直し、amd64 の build、bss の記録、desktop の回帰（Files の PDF）、F-070 と ws115-p010 への反映 | cleared（T1-166・168） |

Phase の記録は git の履歴にある（最後の版は main b7578894 の `plan/ws140/phase00{1,2,3}/phase.md`）。
