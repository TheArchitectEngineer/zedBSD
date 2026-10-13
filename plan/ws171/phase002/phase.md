<!-- awesome-plan project=zedbsd record=ws171-p002 -->
# ws171-p002: hal.h の契約の comment の差分の案

Status: in-progress（2026-10-14 P3 q966: 6 領域のうち 3（IRQ）と 4（task）の案を作った。1・2・5・6 は未）
Parent: [WS171](../ws.md)
Queue: q966（P3、2026-10-14。ユーザー: WS171 は優先度低め、やることが無い時に）
依存: p001（[inventory.md](../phase001/inventory.md)）

**include/hal/hal.h には適用しない。** comment だけの変更でも HAL の API の変更なので、ユーザーの review と承認を得てから p003 で当てる（Guardrail の HAL）。

## 作り方（再現できる形）

| file | 役目 |
| --- | --- |
| `spec/NN-area.txt` | 領域ごとの新しい comment。`@@ name`（行の頭が `name(` の宣言）か `@@ ^regex`（宣言の最初の行）の後に comment の block。その宣言の直上の comment を置き換える（無ければ挿入する） |
| `apply.py HEADER SPEC` | spec を 1 つ当てる |
| `check.py ORIG NEW` | comment を除いた token の列が同じか（宣言・型・macro が不変か）、複数行の comment の `/*`・`*/` が単独の行か（coding-style §10） |
| `gen.sh WORKDIR` | `include/hal/hal.h` を WORKDIR に写し、spec を名前の順に重ねて当て、`diff/NN-area.diff`（前の spec の上の差分）と `diff/all.diff`（全部、今の hal.h に対して）を書く。hal.h は触らない |

```
mkdir -p build/tmp && w=$(mktemp -d build/tmp/ws171.XXXX) && plan/ws171/phase002/gen.sh "$w"
git apply --check plan/ws171/phase002/diff/all.diff
```

review は `diff/NN-area.diff` を領域ごとに見る。承認された領域の spec だけを残して gen.sh を流せば、当てる差分ができる。

## 方針

- 雛形は p001 の通り（HAL v2 の comment）: 何をするか、呼んでよい文脈、他の HAL の操作との関係、返り値と各 error、対応しない machine の扱い。
- 契約は支えている arch（amd64・aarch64、おまけの i386）の実装を読んで、全部が満たす事を書く。合わない所は下の「実装との差」に出し、契約の文は案として示す（決めるのはユーザー）。sparcv9・m68k はサポート外（master の「対象 platform」）で、照合はしたが契約の根拠にしない。
- 既にある HAL v2 の contract の comment（hal_irq_set_wake・suspend・resume、hal_cpu_notify ほか）は変えない。hal_irq_alloc_msi・detach_msi_sync・free_msi は文を保ち、引数と返り値の段を足した。

## 領域と状態

| # | 領域 | 関数 | spec / diff | 状態 |
| --- | --- | --- | --- | --- |
| 1 | 文字列と console・assert・fatal | 11 | — | 未 |
| 2 | CPU・mask・idle・panic・halt・reset・poweroff | 20 | — | 未 |
| 3 | IRQ（と `HAL_IRQ_*`・`hal_irq_ack_t`・`hal_irq_handler_t`） | 19 | [spec](spec/03-irq.txt) / [diff](diff/03-irq.diff) | 案（2026-10-14） |
| 4 | task と context（と `hal_task_t`、debug の register） | 28 | [spec](spec/04-task.txt) / [diff](diff/04-task.diff) | 案（2026-10-14） |
| 5 | 空間・pmem・mmio・io・barrier・cache | 47 | — | 未 |
| 6 | rtc・entropy・get | 5 | — | 未 |

## 実装との差（ユーザーの判断か bug。契約の案は amd64 の動作に合わせた所が多い）

| ID | 関数 | 差 | 案 |
| --- | --- | --- | --- |
| H1 | hal_irq_mask・unmask | 無い IRQ: amd64・i386 は黙って無視、aarch64 は HAL_FATAL。amd64 は IRQ 0（tick）を無視、i386 は IRQ 0（PIT の tick）も mask する | 契約は「IRQ は存在すること」だけ（どちらの扱いも書かない） |
| H2 | hal_irq_register | 既に handler がある IRQ: amd64・i386 は HAL_ERR_BUSY、aarch64 は黙って置き換える | 契約は BUSY。aarch64 は bug 候補（src/hal の直しは承認不要） |
| H3 | hal_irq_unregister | amd64 は mask し、他の CPU で走る handler の終わりを待つ。i386 は mask し、走っていれば BUSY。aarch64 は mask せず待たない（Pi 4 の SMP（WS208）で効く） | 契約は「mask し、HAL_OK なら走っている呼び出しは無い」。aarch64 は bug 候補 |
| H4 | hal_irq_unmask・handler の無い IRQ | handler の無い IRQ が来た時、amd64・i386 は mask して EOI、aarch64 は EOI と表示だけ（level の IRQ なら続けて来る） | 契約は「acknowledge して mask し直す」。aarch64 は bug 候補 |
| H5 | hal_irq_set_affinity | 届けられる CPU が mask に無い: amd64 は INVALID、i386 は UNSUPPORTED。aarch64 は CPU 0 を含む mask だけ受け、いつも CPU 0 | 契約は INVALID。i386 は bug 候補（おまけの arch） |
| H6 | hal_irq_get_affinity | effective: amd64 は route した 1 つの CPU、aarch64 は CPU 0、i386 は requested の mask そのまま（route は最初の 1 つなので、複数の CPU の mask では違う）。NULL の結果: amd64 は INVALID、i386・aarch64 は飛ばす | 契約は「NULL を渡さない」。i386 の effective は bug 候補 |
| H7 | hal_irq_set_mode | amd64 は自分で mask して戻す、aarch64 は unmask の IRQ を BUSY で断る | 契約は元の文（呼び手が mask する）に「BUSY で断ってよい」を足した |
| H8 | hal_irq_send_eoi | 順の検査: amd64 は厳密な stack、aarch64 は 1 つだけ、i386 は IRQ ごと | 契約は「この CPU が持つ ack でなければ HAL_FATAL」と「取った逆の順に終える」 |
| H9 | hal_task_create | 新しい task の CPU: amd64・i386 は作った CPU、aarch64 は CPU 0 | 契約は「HAL が最初に決めた CPU、hal_task_transfer() まで」。差は kernel が transfer してから switch するので無害 |
| H10 | hal_task_get_tls | 走っている task: amd64 は MSR の今の値、aarch64・i386 は保存した値（aarch64 は利用者が TPIDR_EL0 を直に書ける） | 契約は値の出所を書かない。aarch64 の走っている task の値は bug 候補 |
| H11 | hal_task_transfer | i386 は target の CPU が online かを見ない（STATE を返さない） | おまけの arch、記録だけ |
| H12 | hal_task_signal_enter の restorer | aarch64 は x30 に入れる、amd64・i386 は使わない（kernel が user の stack に置く） | 契約に両方を書いた |
| H13 | hal_task_get/set_user_*regs・single_step・debug_points | i386 は定義が無く、`include/hal/arch/i386.h` に HAL_DEBUG_POINT_MAX も無い（kern/ptrace.c が i386 で build されるかは p003 で確かめる） | 記録だけ |
| H14 | sparcv9・m68k の hal_irq_get_affinity | 宣言と違う形（`struct hal_irq_affinity *`）で、hal_irq_register・unregister が無い（`hal_irq_set_handler` だけ） | サポート外、記録だけ |
| H15 | exec・fork・signal・get_user_context・register の関数 | 返り値は HAL_OK などではなく 0 と -1 | 契約にそのまま書いた（型を変えるのは API の変更） |

## 確認

- 2026-10-14 `gen.sh`: 各 spec の後と全部で `declarations unchanged: yes`、`comment delimiter problems: none`。`git apply --check diff/all.diff` 成功。hal.h は不変（worktree の include/hal/hal.h は変えていない）。
- build は不要（comment だけ。p003 で当てた後に amd64・aarch64・i386 の vmunix を build する）。

## 残り

- 領域 1・2・5・6 の spec（p001 の数で 83 関数）。
- 全部の案がそろったら、ユーザーへ review を頼む（Q1 経由）。
