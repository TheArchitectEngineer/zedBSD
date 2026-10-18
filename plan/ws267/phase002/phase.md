<!-- awesome-plan project=zedbsd record=ws267-p002 -->

# ws267-p002 規約の全文の見直し

Status: cleared（2026-10-19 Q1 判定: WS267 が変えた C と HAL・受け渡しの規約の見直しを P2 が済ませた（p001 と同じ commit の範囲、build warning 0））（旧: in-progress（2026-10-19 P2 実施済み、Q1 の判定待ち））

- 親: [WS267](../ws.md)。範囲: p001 が書いた・書き換えた C と assembly（`src/hal/sparcv9/` の全 file、`src/hal/sparcv9/bsp-sun4u/`、`src/drivers/platform/sun4u/`、`src/kern/platform/sun4u.c`、`bootloader/sparcv9/handoff.c` の追加分、`src/libc/format.c`・`userland/base/libc/posix.c`・`include/libc/stdint.h` の変更の行）。

## 見たこと（plan/coding-style.md §14 の checklist）

- file の並び、static の前方宣言、型・file-scope の変数の役割の comment、public の関数の動詞の comment、宣言の位置、`for` の初期化、条件の中の関数の呼び出し（機械で走査 0 件）、関数の結果の直の return（0 件）、3 つ以上の条件の行分け（space.c の 1 か所を直した）、critical section（interrupt を閉じる区間）の空行、多行の comment の形、最後の文の成功の return、`goto` 無し、条件演算子無し。
- assembly の 3 file（context.S・locore.S・trap-table.S）の先頭を copyright と説明の形に揃えた。
- 出典は該当の block ごとの `Values referenced:`、file の先頭には書いていない。source の中に開発の管理情報（WS・Phase の番号、試験の話）は無い（旧 HAL の「PASS」の印と自己試験は p001 で除いた）。

## 結果

- build: `build/ws267/hdd-image.img` rc=0、warning 0。`git diff --check` 差なし。
- 範囲外のまま: `src/hal/cpu-up.c`・`src/hal/idle-suspend-unsupported.c`（共有、変えていない）、loader の他の file（WS262）。
