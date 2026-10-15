<!-- awesome-plan project=zedbsd record=ws248-p002 -->

# ws248-p002: AX211 の driver の規約の全文の見直し

Status: cleared（2026-10-15、q1201 の後半で P2）
Parent: [WS248](../ws.md)
Approval: 2026-10-15 ユーザー（ws.md の原文）

## 範囲

不要な関数の前方宣言の削除（intel-ax211.c ほか）と、plan/coding-style.md の全文への適合。書き方だけで動作は変えない。

## 決定（2026-10-15）

- 前方宣言: ユーザーの回答（Q1 経由、クリック）「規約どおり（P2 の案 1）」。intel-ax211.c の前半で定義の直前に 1 つずつ置かれた前方宣言（8 個）は、file の前方宣言の block にまとめる。後半の約 120 個の前方宣言の block は残す。header の未定義・未使用の宣言は消す、とした（ただし調べ直すと intel_ax211_dma_host_scrub、intel_ax211_firmware_loader_host_parse・intel_ax211_firmware_loader_host_inspect_pnvm、drv_intel_ax211_ring_available の 3 宣言は host 試験が使っていたので残した。Q1 了承）。「Begin/End consolidated」のつなぎ目は、目印を残して内側の 2 つ目の mode 行と著作権の block だけ整理した（prepare.py が目印を使うため、Q1 指示）。
- その後（2026-10-15）: ユーザーが main で整形（8a9de2294）し、目印・mode 行を消して include・型・宣言を移したため prepare.py が止まった。ユーザーの回答（Q1 経由、クリック）「file を 2 つに分ける」により、intel-ax211.c を firmware・descriptor の部分（intel-ax211.c）と PCI の部分（intel-ax211-pci.c、新規）に分け、vmunix.mk に 1 行足し、host 試験 6 本の script は core を直に compile する形にし、driver-fragments/source-map.tsv から AX211 の 43 行を外した（a0c9dd2ae、振る舞いを変えない移動の 1 commit）。
- 根拠: plan/coding-style.md §3「Every non-`static inline` static function has a forward declaration.」と §14 の checklist。依頼の当初の案（定義の順で足りる物は消す）は §3 に反するため、Q1 に確かめて案 1 になった。

## 確かめ

style-check.py、build warning 0、AX211 の host 試験、preprocess の後の比較で差が宣言の削除・並べ替えなど意図した物だけ。

## 結果

対象: `src/drivers/wifi/intel-ax211/` の全 44 file（分割で 1 file 増えた）と `include/drivers/wifi/intel-ax211/pci-intel-ax211.h`。書き方だけを変え、動作は変えていない。

- 前方宣言: intel-ax211.c の前半で定義の直前に散っていた 8 個を前方宣言の block にまとめた（分割後は intel-ax211.c の block）。PCI 部の約 120 個は §3 どおり残した。header の 3 宣言は host 試験が使うので残した。
- 規約の主な書き換え（全 file）: 条件の中の関数呼び出しを外へ（短絡の順は保持）、`?:` を if へ（短く対称な物を除く）、Boolean の式の代入・return を if へ、`error = f(); return error;` と最後の `return result;` を失敗・成功の 2 つの return に分割、定型の comment（「Returns the computed result.」「Handles the … condition.」「Checks the operation result.」「Supports the ax211 …」「Implements the drv …」「Failed.」など）を段落の意味の comment に書き直し、段落の comment・閉じ括弧の後の空行、`_local` の名前（index_local・index_local1 → api_index・capability_index）、型・表・file scope の変数の役割の comment、header の prototype の形、80 桁を超える comment の折り返し。intel-ax211-pci.c に重複していた同じ値の #define の block（silicon revision・radio identity）1 つを削除。
- style-check.py: 690（p002 開始の 9f94ae7b1、全 file の合計）→ 8。残る 8 は全部 conditional で、短く対称な選択として残した: bss.c `receive_tsf_valid`、firmware.c `amount`（min）、runtime.c `firmware_regulatory`・`ltr_enabled`（0/1 への正規化）、rx.c の rssi の 2 つと max、tx-ring.c `num_tbs`。
- 定型の comment（自前の grep、上の文言）: 3,432 → 0。
- 読んで残した物: `return result;` のうち状態を分類して返す関数（assoc の accept・cancel、scan・scan-session の 4 箇所、pci の release 失敗の末尾）は §11 の「分類を返す関数は拒否と答えを分ける」の形にしたか、引数・状態をそのまま返す物で、関数の呼び出しの結果を直に返してはいない。既存の 80 桁を超える code の行（約 160、主に長い定数名の式）は今回の範囲（comment と書き方）で直していない。
- 確かめ（各区切りで）: vmunix（config/ci/config-amd64.mk）の build rc 0・warning 0。host 試験 6 本（transport・core・boot・runtime-start・dma・firmware-loader、ASan/UBSan・analyzer・amd64/i386 syntax 込み）が全部 rc 0。preprocess の後の差（comment と空白を除いた code）を各区切りで全部読み、条件の分割・`?:` の if 化・return の分割・変数の追加と改名だけであることを確かめた（短絡の順と評価の順は保持、副作用のある呼び出し（ax211_pci_log_rejection など）は元と同じ条件の下でだけ呼ぶ）。main の取り込み 2 回（09296cd74・a3903b1e8）と分割（a0c9dd2ae）は、関数単位の 3-way 比較と preprocess の行の多重集合の比較で、ユーザーの整形と私の変更が重ならないこと・分割の前後で関数 150 個の集合と中身が同じことを確かめた。
- 未実施: QEMU・実機の試験（実装の担当は QEMU を起動しない）。host 試験の無い file（key・rx・tx・tx-ring・bss・scan・scan-session・mmio・pci-mmio・transport-backend・assoc・intel-ax211-pci.c）は build と preprocess の diff の読みだけ。T1 に run-intel-ax211-vfio-qemu.sh（VFIO の AX211 で attach・scan・association）と実機（5330 の AX211）の scan・接続を依頼することを提案する。run-intelax211-firmware-package-test.sh は network からの取得と script の rm を含み、driver の変更と無関係なので流していない。

